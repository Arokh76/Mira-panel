// Mira Panel native ESP-IDF Jeedom HTTP compatibility (Waveshare 4.3C).
// Preserve the installed Jeedom plugin contract; no Arduino dependency.
// Reads/writes the existing LVGL JSON without erasing or formatting SPIFFS.
// Incoming state updates are PASSIVE, never forwarded to MQTT or an actuator.
#include "mira_jeedom.h"
#include "mira_full.h"
#include "mira_camera.h"
#include "mira_setup_access.h"
#include "mira_legacy_cipher.h"
#include "mira_mqtt.h"
#include "mira_beta_web.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <math.h>
#include <limits.h>
#include <inttypes.h>
#include "cJSON.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#define MIRA_JSON_PATH "/spiffs/miralvgl.json"
#define MIRA_JSON_TMP  "/spiffs/miralvgl.new"
#define MIRA_JSON_MAX 49152
#define MIRA_FORM_MAX 110000
#define MIRA_KEY_MAX 96

static const char *TAG="mira-idf-jeedom";
// Legacy HTTP is the default. MQTT is an explicitly selected advanced mode.
// Beta GetParamBool("Jeedom") defaults to false when unset.
static volatile bool jeedom_mode_enabled=false;
bool mira_jeedom_actions_enabled(void){return jeedom_mode_enabled;}
/* Only one event leaves the screen. If MQTT is not available, execute
 * the configured beta GenericCmd URL asynchronously (never on LVGL).
 */
static bool mira_other_transport(const char *name,const char *type,
                                 const char *value){
    if(mira_mqtt_publish_action(name,value))return true;
    return mira_generic_queue_local_action(name,type,value);
}

// Request async synchronization after Jeedom changes its command table.
static void request_binding_sync(void);
static uint32_t ui_config_hash(const char *s);

// URL/form decoding is deliberately bounded, including malformed percent escapes.
static int hexdigit(char c) {
    if(c>='0'&&c<='9')return c-'0';
    if(c>='a'&&c<='f')return c-'a'+10;
    if(c>='A'&&c<='F')return c-'A'+10;
    return -1;
}
static bool decode_url(char *value) {
    char *src=value,*dst=value;
    while(*src) {
        if(*src=='%') {
            if(!src[1]||!src[2])return false;
            int hi=hexdigit(src[1]),lo=hexdigit(src[2]);
            if(hi<0||lo<0 || (hi==0&&lo==0))return false;
            *dst++=(char)((hi<<4)|lo);
            src+=3;
        } else if(*src=='+') {
            *dst++=' ';src++;
        } else *dst++=*src++;
    }
    *dst=0;
    return true;
}
static bool query_param(httpd_req_t *req,const char *name,char *dst,size_t cap) {
    // Free-layout widgets can carry coordinates, labels and URL-encoded values.
    char query[2048];
    if(httpd_req_get_url_query_str(req,query,sizeof(query))!=ESP_OK)return false;
    char encoded[1024];
    if(httpd_query_key_value(query,name,encoded,sizeof(encoded))!=ESP_OK)return false;
    if(!decode_url(encoded)||strlen(encoded)>=cap)return false;
    snprintf(dst,cap,"%s",encoded);
    return true;
}
static bool read_exact_request(httpd_req_t *req,char *buf,size_t length) {
    size_t done=0;
    while(done<length) {
        int n=httpd_req_recv(req,buf+done,length-done);
        if(n<=0)return false;
        done+=(size_t)n;
    }
    buf[length]=0;
    return true;
}
static bool read_setup_key(char *out,size_t cap) {
    nvs_handle_t nvs;
    if(nvs_open("mira_idf_auth",NVS_READONLY,&nvs)!=ESP_OK)return false;
    size_t len=cap;
    esp_err_t err=nvs_get_str(nvs,"jeedom_key",out,&len);
    nvs_close(nvs);
    return err==ESP_OK && len>1 && out[0]!=0;
}
static bool correct_key(const char *supplied) {
    char expected[MIRA_KEY_MAX+1]={0};
    if(!supplied||!read_setup_key(expected,sizeof(expected)))return false;
    size_t n=strlen(expected),m=strlen(supplied);
    if(n!=m||n==0)return false;
    unsigned char diff=0;
    for(size_t i=0;i<n;i++)diff|=(unsigned char)(expected[i]^supplied[i]);
    return diff==0;
}
static bool require_key(httpd_req_t *req) {
    char supplied[256];
    if(query_param(req,"key",supplied,sizeof(supplied))&&correct_key(supplied))return true;
    httpd_resp_set_status(req,"403 Forbidden");
    httpd_resp_sendstr(req,"Cle Mira invalide (provisionner la meme cle que dans Jeedom via l'AP)");
    return false;
}
static char *read_config(size_t *out_length) {
    FILE *f=fopen(MIRA_JSON_PATH,"rb");
    if(!f)return NULL;
    if(fseek(f,0,SEEK_END)!=0){fclose(f);return NULL;}
    long length=ftell(f);
    rewind(f);
    if(length<2||length>MIRA_JSON_MAX){fclose(f);return NULL;}
    char *buf=malloc((size_t)length+1);
    if(!buf){fclose(f);return NULL;}
    size_t n=fread(buf,1,(size_t)length,f);
    fclose(f);
    if(n!=(size_t)length){free(buf);return NULL;}
    buf[n]=0;
    if(out_length)*out_length=n;
    return buf;
}
static cJSON *append_action(cJSON *arr,int *id,const char *type,const char *name) {
    cJSON *entry=cJSON_CreateObject();
    if(!entry)return NULL;
    cJSON_AddNumberToObject(entry,"id",(*id)++);
    cJSON_AddStringToObject(entry,"nom",name);
    cJSON_AddStringToObject(entry,"type",type);
    // Match the beta IndexAddButton/IndexAddInfo default JSON metadata.
    if(!strcmp(type,"button"))
        cJSON_AddStringToObject(entry,"return","true");
    if(!strcmp(type,"info")){
        cJSON_AddStringToObject(entry,"value","");
        cJSON_AddStringToObject(entry,"size","60");
        cJSON_AddStringToObject(entry,"unit","");
    }
    cJSON_AddItemToArray(arr,entry);
    return entry;
}
static void trim(char *text) {
    size_t n=strlen(text);
    while(n&&isspace((unsigned char)text[n-1]))text[--n]=0;
    char *start=text;
    while(*start&&isspace((unsigned char)*start))start++;
    if(start!=text)memmove(text,start,strlen(start)+1);
}
static const char *jstr(cJSON *obj,const char *key,const char *fallback) {
    cJSON *v=cJSON_GetObjectItemCaseSensitive(obj,key);
    return cJSON_IsString(v)&&v->valuestring?v->valuestring:fallback;
}
static int jint(cJSON *obj,const char *key,int fallback) {
    cJSON *v=cJSON_GetObjectItemCaseSensitive(obj,key);
    if(cJSON_IsNumber(v))return v->valueint;
    if(cJSON_IsString(v)&&v->valuestring)return atoi(v->valuestring);
    return fallback;
}
static void add_choices(cJSON *arr,int *id,const char *name,const char *options) {
    if(!options)return;
    char choices[1024];
    snprintf(choices,sizeof(choices),"%s",options);
    // Arduino beta accepts literal \\n in thermostat modes and caps
    // the selectable items at MAX_LIST_ITEMS=12.
    for(char *p=choices;*p;p++){
        if(p[0]=='\\'&&p[1]=='n'){
            *p='|';
            memmove(p+1,p+2,strlen(p+2)+1);
        }else if(*p=='\n')*p='|';
    }
    char *save=NULL;
    int count=0;
    const int first_id=*id;
    for(char *option=strtok_r(choices,"|",&save);option&&count<12;
        option=strtok_r(NULL,"|",&save)){
        trim(option);
        if(!*option)continue;
        cJSON *entry=append_action(arr,id,"list",option);
        if(!entry)return;
        cJSON_AddStringToObject(entry,"nomliste",name);
        cJSON_AddStringToObject(entry,"option",option);
        // IndexAddList defaults to the first choice ID for the entire group.
        if(count==0)cJSON_AddNumberToObject(entry,"value",first_id);
        else{
            char text[16];
            snprintf(text,sizeof(text),"%d",first_id);
            cJSON_AddStringToObject(entry,"value",text);
        }
        count++;
    }
}
// Match the exact signatures expected by Mira Jeedom beta autosync:
// info, button, toggle, range, color and one 'list' row per selectable option.
static cJSON *index_from_config(const char *json) {
    cJSON *root=cJSON_Parse(json);
    if(!root)return NULL;
    const cJSON *widgets=cJSON_GetObjectItemCaseSensitive(root,"lvgl");
    if(!cJSON_IsArray(widgets)){cJSON_Delete(root);return NULL;}
    cJSON *out=cJSON_CreateObject(), *arr=cJSON_CreateArray();
    if(!out||!arr){cJSON_Delete(root);cJSON_Delete(out);cJSON_Delete(arr);return NULL;}
    cJSON_AddItemToObject(out,"actions",arr);
    int id=1;
    bool weather_registered=false;
    static const char *const energy_fields[]={"solar","home","grid","battery","soc","soc2","soc3"};
    static const char *const media_fields[]={"title","artist","album","state","progress","volume","cover","device","devices"};
    static const char *const media_actions[]={"previous","play","next"};
    const cJSON *w=NULL;
    cJSON_ArrayForEach(w,widgets){
        const char *type=jstr((cJSON*)w,"type","");
        const char *name=jstr((cJSON*)w,"name","");
        // The clock can be unnamed in Mira beta exports; its three
        // internal weather infos still need historical Jeedom bindings.
        if(!*type||(!*name&&strcmp(type,"datetime")))continue;
        if(!strcmp(type,"info")||!strcmp(type,"button")){
            cJSON *a=append_action(arr,&id,type,name);
            if(a && !strcmp(type,"info")){
                /* IndexAddInfo() starts with an empty Jeedom state. The
                 * widget's JSON default is NOT a received sensor value.
                 */
                cJSON_ReplaceItemInObjectCaseSensitive(a,"unit",
                    cJSON_CreateString(jstr((cJSON*)w,"unit","")));
            }
        }else if(!strcmp(type,"toggle")){
            cJSON *a=cJSON_CreateObject();if(!a)continue;
            cJSON_AddNumberToObject(a,"id0",id++);
            cJSON_AddNumberToObject(a,"id1",id++);
            cJSON_AddStringToObject(a,"nom",name);
            cJSON_AddStringToObject(a,"type","toggle");
            cJSON_AddStringToObject(a,"value0","Off");
            cJSON_AddStringToObject(a,"value1","On");
            cJSON_AddStringToObject(a,"state","Off");
            cJSON_AddItemToArray(arr,a);
        }else if(!strcmp(type,"range")||!strcmp(type,"roundrange")){
            /* MiraPanelUI.cpp normalizes malformed min/max and clamps the
             * default BEFORE registering its IndexAddRange() command.
             * Jeedom must see exactly the same range for autosync.
             */
            int lo=jint((cJSON*)w,"min",0),hi=jint((cJSON*)w,"max",100);
            if(hi<=lo)hi=lo>INT_MAX-100?INT_MAX:lo+100;
            int current=jint((cJSON*)w,"value",lo);
            if(current<lo)current=lo;
            if(current>hi)current=hi;
            cJSON *a=append_action(arr,&id,"range",name);
            if(a){
                cJSON_AddNumberToObject(a,"min",lo);
                cJSON_AddNumberToObject(a,"max",hi);
                cJSON_AddNumberToObject(a,"value",current);
            }
        }else if(!strcmp(type,"color")){
            cJSON *a=append_action(arr,&id,"color",name);
            if(a){
                cJSON_AddStringToObject(a,"r","50");
                cJSON_AddStringToObject(a,"g","120");
                cJSON_AddStringToObject(a,"b","180");
            }
        }else if(!strcmp(type,"list")||!strcmp(type,"listbutton")){
            add_choices(arr,&id,name,jstr((cJSON*)w,"value",""));
        }else if(!strcmp(type,"thermostat")){
            char compound[200];
            snprintf(compound,sizeof(compound),"%s__temperature",name);
            cJSON *a=append_action(arr,&id,"info",compound);
            if(a)cJSON_ReplaceItemInObjectCaseSensitive(a,"unit",cJSON_CreateString("°C"));
            snprintf(compound,sizeof(compound),"%s__consigne",name);
            /* Beta thermo min [-20,60], max [min+1,80], half-degree
             * setpoint then round to the integer IndexAddRange default.
             */
            int lo=jint((cJSON*)w,"min",5),hi=jint((cJSON*)w,"max",30);
            if(lo< -20)lo= -20;
            if(lo>60)lo=60;
            if(hi<lo+1)hi=lo+1;
            if(hi>80)hi=80;
            const cJSON *setval=cJSON_GetObjectItemCaseSensitive(w,"value");
            const char *raw=jstr((cJSON*)w,"value","20");
            char *endptr=NULL;
            float setpoint=cJSON_IsNumber(setval)?
                (float)setval->valuedouble:strtof(raw,&endptr);
            if(!cJSON_IsNumber(setval) &&
               (endptr==raw||*endptr))setpoint=20.0f;
            if(!isfinite(setpoint))setpoint=20.0f;
            setpoint=roundf(setpoint*2.0f)*0.5f;
            if(setpoint<lo)setpoint=(float)lo;
            if(setpoint>hi)setpoint=(float)hi;
            a=append_action(arr,&id,"range",compound);
            if(a){
                cJSON_AddNumberToObject(a,"min",lo);
                cJSON_AddNumberToObject(a,"max",hi);
                cJSON_AddNumberToObject(a,"value",(int)roundf(setpoint));
            }
            snprintf(compound,sizeof(compound),"%s__mode",name);
            const int before_modes=id;
            add_choices(arr,&id,compound,jstr((cJSON*)w,"modes","Confort|Éco|Hors gel|Arrêt"));
            if(id==before_modes)add_choices(arr,&id,compound,"Confort");
        }else if(!strcmp(type,"multiaction")){
            static const char *const triggers[]={"Multi_Clic_","Multi_Double_","Multi_Triple_","Multi_Long_"};
            for(size_t n=0;n<4;n++){
                char compound[192];snprintf(compound,sizeof(compound),"%s%.95s",triggers[n],name);
                append_action(arr,&id,"button",compound);
            }
        }else if(!strcmp(type,"camera")){
            char compound[192];snprintf(compound,sizeof(compound),"Camera_%.95s",name);
            append_action(arr,&id,"info",compound);
        }else if(!strcmp(type,"energy")){
            for(size_t n=0;n<7;n++){
                char compound[192];snprintf(compound,sizeof(compound),"%.95s__%s",name,energy_fields[n]);
                append_action(arr,&id,"info",compound);
            }
        }else if(!strcmp(type,"media")){
            for(size_t n=0;n<9;n++){
                char compound[192];snprintf(compound,sizeof(compound),"%.95s__%s",name,media_fields[n]);
                cJSON *a=append_action(arr,&id,"info",compound);
                /* Mirror MiraPanelUI.cpp: progress and volume carry %
                 * units in the historical Jeedom /getindex table.
                 */
                if(a && (n==4||n==5))cJSON_ReplaceItemInObjectCaseSensitive(a,"unit",cJSON_CreateString("%"));
            }
            for(size_t n=0;n<3;n++){
                char compound[192];snprintf(compound,sizeof(compound),"%.95s__%s",name,media_actions[n]);
                append_action(arr,&id,"button",compound);
            }
            char compound[192];
            snprintf(compound,sizeof(compound),"%.95s__pause",name);
            append_action(arr,&id,"button",compound);
            snprintf(compound,sizeof(compound),"%.95s__volume_cmd",name);
            cJSON *a=append_action(arr,&id,"range",compound);
            if(a){
                cJSON_AddNumberToObject(a,"min",0);
                cJSON_AddNumberToObject(a,"max",100);
                cJSON_AddNumberToObject(a,"value",40);
            }
            snprintf(compound,sizeof(compound),"%.95s__device_cmd",name);
            a=append_action(arr,&id,"range",compound);
            if(a){
                cJSON_AddNumberToObject(a,"min",0);
                cJSON_AddNumberToObject(a,"max",11);
                cJSON_AddNumberToObject(a,"value",0);
            }
        }else if(!strcmp(type,"datetime")&&!weather_registered){
            weather_registered=true;
            cJSON *weather=append_action(arr,&id,"info","__mira_weather_temperature");
            if(weather)cJSON_ReplaceItemInObjectCaseSensitive(weather,"unit",cJSON_CreateString("°C"));
            append_action(arr,&id,"info","__mira_weather_condition");
            append_action(arr,&id,"info","__mira_weather_code");
        }
    }
    cJSON_Delete(root);
    return out;
}
static bool decimal_id(const char *str);
// HTML part of the original beta home page. Use the SAME command ids from
// index_from_config() that the Jeedom plugin sees, no parallel ID mapping.
static esp_err_t beta_html(httpd_req_t *req,const char *s){
    return httpd_resp_send_chunk(req,s,HTTPD_RESP_USE_STRLEN);
}
static esp_err_t beta_html_escape(httpd_req_t *req,const char *s){
    if(!s)return ESP_OK;
    const char *start=s;
    for(const char *p=s;;p++){
        const char *entity=NULL;
        if(*p=='&')entity="&amp;";
        else if(*p=='<')entity="&lt;";
        else if(*p=='>')entity="&gt;";
        else if(*p=='"')entity="&quot;";
        else if(*p=='\'')entity="&#39;";
        if(entity||!*p){
            if(p>start&&httpd_resp_send_chunk(req,start,p-start)!=ESP_OK)return ESP_FAIL;
            if(!*p)break;
            if(beta_html(req,entity)!=ESP_OK)return ESP_FAIL;
            start=p+1;
        }
    }
    return ESP_OK;
}
static esp_err_t beta_html_id(httpd_req_t *req,int id){
    char str[20];snprintf(str,sizeof(str),"%d",id);
    return beta_html(req,str);
}
static esp_err_t beta_html_value(httpd_req_t *req,const char *name,const char *fallback){
    char cached[160]={0};
    return beta_html_escape(req,mira_full_cached_state(name,cached,sizeof(cached))?cached:fallback);
}
esp_err_t mira_jeedom_render_beta_actions(httpd_req_t *req){
    char *raw=read_config(NULL);
    cJSON *index=raw?index_from_config(raw):NULL;
    free(raw);
    if(!index)return beta_html(req,"<p>Aucune programmation Mira Panel.</p>");
    const cJSON *actions=cJSON_GetObjectItemCaseSensitive(index,"actions");
    const cJSON *item=NULL;
    bool any=false;
    esp_err_t status=ESP_OK;
    char seen_groups[16][96]={{0}};
    unsigned group_count=0;
    cJSON_ArrayForEach(item,actions){
        const char *type=jstr((cJSON*)item,"type","");
        const char *name=jstr((cJSON*)item,"nom","");
        const int id=jint((cJSON*)item,"id",-1);
        if(!*name)continue;
        if((!strcmp(type,"button")||!strcmp(type,"little_button"))&&id>0){
            any=true;
            if((status=beta_html(req,!strcmp(type,"little_button")?
                 "<button class=\"LittleBouton\" id=\"":"<button class=\"Bouton\" id=\""))!=ESP_OK||
               (status=beta_html_id(req,id))!=ESP_OK||
               (status=beta_html(req,"\" onclick=\"Click(this)\">"))!=ESP_OK||
               (status=beta_html_escape(req,name))!=ESP_OK||
               (status=beta_html(req,"</button>"))!=ESP_OK)break;
        }else if((!strcmp(type,"title")||!strcmp(type,"text_index"))){
            int size=jint((cJSON*)item,"size",20);
            if(size<10)size=10;
            if(size>72)size=72;
            char style[90];
            snprintf(style,sizeof(style),
                     "<div style=\"padding:10px 0;font-size:%dpx\">",size);
            any=true;
            if((status=beta_html(req,style))!=ESP_OK||
               (status=beta_html_escape(req,name))!=ESP_OK||
               (status=beta_html(req,"</div>"))!=ESP_OK)break;
        }else if(!strcmp(type,"inputbox")&&id>0){
            any=true;
            if((status=beta_html(req,"<label>"))!=ESP_OK||
               (status=beta_html_escape(req,name))!=ESP_OK||
               (status=beta_html(req,"</label><input class=\"inputbox\" type=\"text\" id=\""))!=ESP_OK||
               (status=beta_html_id(req,id))!=ESP_OK||
               (status=beta_html(req,"\" value=\""))!=ESP_OK||
               (status=beta_html_value(req,name,jstr((cJSON*)item,"value","")))!=ESP_OK||
               (status=beta_html(req,"\" onblur=\"InputBoxValidate(this)\"><br>"))!=ESP_OK)break;
        }else if(!strcmp(type,"infofa")&&id>0){
            any=true;
            if((status=beta_html(req,"<div class=\"mira-info\">"))!=ESP_OK||
               (status=beta_html_escape(req,name))!=ESP_OK||
               (status=beta_html(req,": <i id=\""))!=ESP_OK||
               (status=beta_html_id(req,id))!=ESP_OK||
               (status=beta_html(req,"\" class=\""))!=ESP_OK||
               (status=beta_html_value(req,name,jstr((cJSON*)item,"value","")))!=ESP_OK||
               (status=beta_html(req,"\"></i></div>"))!=ESP_OK)break;
        }else if(!strcmp(type,"color")&&id>0){
            int red=jint((cJSON*)item,"r",0),green=jint((cJSON*)item,"g",0),blue=jint((cJSON*)item,"b",0);
            if(red<0||red>255||green<0||green>255||blue<0||blue>255)continue;
            char color[16];snprintf(color,sizeof(color),"#%02x%02x%02x",red,green,blue);
            any=true;
            if((status=beta_html(req,"<div class=\"FondBlanc_Color\">"))!=ESP_OK||
               (status=beta_html_escape(req,name))!=ESP_OK||
               (status=beta_html(req,"<br><input class=\"input_color\" type=\"color\" id=\""))!=ESP_OK||
               (status=beta_html_id(req,id))!=ESP_OK||
               (status=beta_html(req,"\" value=\""))!=ESP_OK||
               (status=beta_html(req,color))!=ESP_OK||
               (status=beta_html(req,"\" onchange=\"printColor(this,event)\"></div>"))!=ESP_OK)break;
        }else if(!strcmp(type,"toggle")){
            int off=jint((cJSON*)item,"id0",-1),on=jint((cJSON*)item,"id1",-1);
            if(off<1||on<1)continue;
            char current[160]={0};
            bool known=mira_full_cached_state(name,current,sizeof(current));
            bool checked=known&&(!strcasecmp(current,"On")||!strcmp(current,"1")||
                                  !strcasecmp(current,"true"));
            any=true;
            if((status=beta_html(req,"<div class=\"div_toogle\"><div class=\"div_toogle_name\">"))!=ESP_OK||
               (status=beta_html_escape(req,name))!=ESP_OK||
               (status=beta_html(req,"</div><label class=\"switch\"><input type=\"checkbox\" id=\""))!=ESP_OK||
               (status=beta_html_id(req,off))!=ESP_OK||
               (status=beta_html(req,"\" onclick=\"ClickToogle(this,'Off','On','"))!=ESP_OK||
               (status=beta_html_id(req,off))!=ESP_OK||
               (status=beta_html(req,"','"))!=ESP_OK||
               (status=beta_html_id(req,on))!=ESP_OK||
               (status=beta_html(req,"')\""))!=ESP_OK||
               (checked&&(status=beta_html(req," checked"))!=ESP_OK)||
               (status=beta_html(req,"><span class=\"slider round\"></span></label></div>"))!=ESP_OK)break;
        }else if(!strcmp(type,"info")&&id>0){
            any=true;
            if((status=beta_html(req,"<div class=\"mira-info\">"))!=ESP_OK||
               (status=beta_html_escape(req,name))!=ESP_OK||
               (status=beta_html(req,": <b><span id=\""))!=ESP_OK||
               (status=beta_html_id(req,id))!=ESP_OK||
               (status=beta_html(req,"\">"))!=ESP_OK||
               (status=beta_html_value(req,name,jstr((cJSON*)item,"value","--")))!=ESP_OK||
               (status=beta_html(req,"</span></b> "))!=ESP_OK||
               (status=beta_html_escape(req,jstr((cJSON*)item,"unit","")))!=ESP_OK||
               (status=beta_html(req,"</div>"))!=ESP_OK)break;
        }else if(!strcmp(type,"range")&&id>0){
            any=true;
            int lo=jint((cJSON*)item,"min",0),hi=jint((cJSON*)item,"max",100);
            if(lo>=hi||hi>100000||lo<-100000)continue;
            char bound[160];
            if((status=beta_html(req,"<div><label for=\""))!=ESP_OK||
               (status=beta_html_id(req,id))!=ESP_OK||
               (status=beta_html(req,"\">"))!=ESP_OK||
               (status=beta_html_escape(req,name))!=ESP_OK||
               (status=beta_html(req,"</label><input type=\"range\" id=\""))!=ESP_OK||
               (status=beta_html_id(req,id))!=ESP_OK)break;
            snprintf(bound,sizeof(bound),"\" min=\"%d\" max=\"%d\" value=\"%d\" onchange=\"ValidRange(this)\"></div>",
                     lo,hi,jint((cJSON*)item,"value",lo));
            if((status=beta_html(req,bound))!=ESP_OK)break;
        }else if(!strcmp(type,"list")&&id>0){
            const char *group=jstr((cJSON*)item,"nomliste","");
            if(!*group)continue;
            bool seen=false;
            for(unsigned k=0;k<group_count;k++)if(!strcmp(seen_groups[k],group)){seen=true;break;}
            if(seen||group_count>=16)continue;
            snprintf(seen_groups[group_count++],sizeof(seen_groups[0]),"%s",group);
            any=true;
            if((status=beta_html(req,"<div><label>"))!=ESP_OK||
               (status=beta_html_escape(req,group))!=ESP_OK||
               (status=beta_html(req,"</label><select class=\"box\" id=\""))!=ESP_OK||
               (status=beta_html_escape(req,group))!=ESP_OK||
               (status=beta_html(req,"\" onchange=\"Liste(this)\">"))!=ESP_OK)break;
            const cJSON *opt=NULL;
            cJSON_ArrayForEach(opt,actions){
                if(strcmp(jstr((cJSON*)opt,"type",""),"list")||
                   strcmp(jstr((cJSON*)opt,"nomliste",""),group))continue;
                int option_id=jint((cJSON*)opt,"id",-1);
                if(option_id<1)continue;
                if((status=beta_html(req,"<option value=\""))!=ESP_OK||
                   (status=beta_html_id(req,option_id))!=ESP_OK||
                   (status=beta_html(req,"\">"))!=ESP_OK||
                   (status=beta_html_escape(req,jstr((cJSON*)opt,"nom","")))!=ESP_OK||
                   (status=beta_html(req,"</option>"))!=ESP_OK)break;
            }
            if(status!=ESP_OK|| (status=beta_html(req,"</select></div>"))!=ESP_OK)break;
        }
    }
    cJSON_Delete(index);
    if(status!=ESP_OK)return status;
    if(!any)return beta_html(req,"<p>Aucune commande programmée.</p>");
    // The beta's live states are delivered by /events, which sends a bounded
    // snapshot and lets EventSource reconnect. No HTTP worker remains blocked.
    // Regenerate listeners from the same command index, never from the DOM.
    raw=read_config(NULL);
    index=raw?index_from_config(raw):NULL;
    free(raw);
    if(!index)return ESP_OK;
    actions=cJSON_GetObjectItemCaseSensitive(index,"actions");
    status=beta_html(req,"<script>(function(){if(!window.EventSource)return;"
                              "var source=new EventSource('/events');");
    cJSON_ArrayForEach(item,actions){
        if(status!=ESP_OK)break;
        const char *type=jstr((cJSON*)item,"type","");
        char js[640];
        if(!strcmp(type,"toggle")){
            int off=jint((cJSON*)item,"id0",-1),on=jint((cJSON*)item,"id1",-1);
            if(off<1||on<1)continue;
            snprintf(js,sizeof(js),
                "source.addEventListener('%d',function(){var e=document.getElementById('%d');if(e)e.checked=false;});"
                "source.addEventListener('%d',function(){var e=document.getElementById('%d');if(e)e.checked=true;});",
                off,off,on,off);
        }else if(!strcmp(type,"info")||!strcmp(type,"range")||
                  !strcmp(type,"inputbox")||!strcmp(type,"infofa")){
            int id=jint((cJSON*)item,"id",-1);
            if(id<1)continue;
            snprintf(js,sizeof(js),
                "source.addEventListener('%d',function(v){var e=document.getElementById('%d');"
                "if(e)e.%s=v.data;});",id,id,
                !strcmp(type,"info")?"textContent":(!strcmp(type,"infofa")?"className":"value"));
        }else if(!strcmp(type,"list")){
            int id=jint((cJSON*)item,"id",-1);
            if(id<1)continue;
            // List selectors do not expose raw names to JavaScript: find by
            // option id, then assign their parent select's selected value.
            snprintf(js,sizeof(js),
                "source.addEventListener('%d',function(){var o=document.querySelector('option[value=\"%d\"]');"
                "if(o&&o.parentElement)o.parentElement.value='%d';});",id,id,id);
        }else continue;
        status=beta_html(req,js);
    }
    if(status==ESP_OK)status=beta_html(req,"})();</script>");
    cJSON_Delete(index);
    return status;
}
static esp_err_t beta_events_get(httpd_req_t *req){
    if(!mira_beta_web_authorized(req))return ESP_FAIL;
    char *raw=read_config(NULL);
    cJSON *index=raw?index_from_config(raw):NULL;
    free(raw);
    httpd_resp_set_type(req,"text/event-stream; charset=utf-8");
    httpd_resp_set_hdr(req,"Cache-Control","no-cache");
    esp_err_t err=httpd_resp_send_chunk(req,"retry: 3000\n\n",HTTPD_RESP_USE_STRLEN);
    if(index){
        const cJSON *actions=cJSON_GetObjectItemCaseSensitive(index,"actions");
        const cJSON *item=NULL;
        cJSON_ArrayForEach(item,actions){
            if(err!=ESP_OK)break;
            const char *type=jstr((cJSON*)item,"type","");
            const char *name=jstr((cJSON*)item,"nom","");
            if(!*name)continue;
            char value[160]={0};
            int id=jint((cJSON*)item,"id",-1);
            if(!strcmp(type,"list")){
                const char *group=jstr((cJSON*)item,"nomliste","");
                if(!mira_full_cached_state(group,value,sizeof(value))||
                   strcmp(value,name))continue;
            }else if(!mira_full_cached_state(name,value,sizeof(value)))continue;
            if(!strcmp(type,"toggle")){
                const bool on=!strcmp(value,"1")||!strcasecmp(value,"On")||
                              !strcasecmp(value,"true");
                id=jint((cJSON*)item,on?"id1":"id0",-1);
            }else if(strcmp(type,"info")&&strcmp(type,"range")&&strcmp(type,"list")&&
                     strcmp(type,"inputbox")&&strcmp(type,"infofa"))
                continue;
            if(id<1||id>4096)continue;
            // Prevent an incoming MQTT newline from becoming a forged SSE event.
            for(char *p=value;*p;p++)if(*p=='\r'||*p=='\n')*p=' ';
            char event[30];
            snprintf(event,sizeof(event),"event: %d\ndata: ",id);
            if((err=beta_html(req,event))!=ESP_OK||
               (err=beta_html(req,value))!=ESP_OK||
               (err=beta_html(req,"\n\n"))!=ESP_OK)break;
        }
        cJSON_Delete(index);
    }
    if(err!=ESP_OK)return err;
    return httpd_resp_send_chunk(req,NULL,0);
}
// Browser home commands on POST /update?send=ID, send_range, send_list, etc.
// Only local user interactions can produce outbound actions; state updates
// never invoke this endpoint.
esp_err_t mira_jeedom_beta_web_action(httpd_req_t *req){
    char idtext[24]={0},payload[160]="1",kind[24]={0};
    static const char *const selectors[]={"send","send_range","send_list","send_color","send_textbox"};
    for(size_t i=0;i<sizeof(selectors)/sizeof(selectors[0]);i++)
        if(query_param(req,selectors[i],idtext,sizeof(idtext))){
            snprintf(kind,sizeof(kind),"%s",selectors[i]);break;
        }
    if(!*kind||!decimal_id(idtext)){
        httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Commande historique invalide");
        return ESP_FAIL;
    }
    long id=strtol(idtext,NULL,10);
    if(id<1||id>4096){
        httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Identifiant de commande invalide");
        return ESP_FAIL;
    }
    char *raw=read_config(NULL);
    cJSON *index=raw?index_from_config(raw):NULL;
    free(raw);
    if(!index){
        httpd_resp_send_err(req,HTTPD_404_NOT_FOUND,"Programmation Mira absente");
        return ESP_FAIL;
    }
    const cJSON *array=cJSON_GetObjectItemCaseSensitive(index,"actions"),*entry=NULL;
    char action_name[96]={0},action_type[24]={0};
    bool matched=false;
    cJSON_ArrayForEach(entry,array){
        const char *type=jstr((cJSON*)entry,"type","");
        const char *name=jstr((cJSON*)entry,"nom","");
        if(!strcmp(kind,"send")&&
           !strcmp(type,"toggle")&&
           (jint((cJSON*)entry,"id0",-1)==id||jint((cJSON*)entry,"id1",-1)==id)){
            snprintf(action_name,sizeof(action_name),"%s",name);
            snprintf(action_type,sizeof(action_type),"toggle");
            snprintf(payload,sizeof(payload),"%d",jint((cJSON*)entry,"id1",-1)==id);
            matched=true;break;
        }
        if(jint((cJSON*)entry,"id",-1)!=id)continue;
        if(!strcmp(kind,"send")&&(!strcmp(type,"button")||!strcmp(type,"little_button"))){
            snprintf(action_name,sizeof(action_name),"%s",name);
            snprintf(action_type,sizeof(action_type),"button");
            matched=true;break;
        }
        if(!strcmp(kind,"send_list")&&!strcmp(type,"list")){
            char name_given[96]={0};
            if(!query_param(req,"list",name_given,sizeof(name_given))||
               strcmp(name_given,jstr((cJSON*)entry,"nomliste","")))break;
            snprintf(action_name,sizeof(action_name),"%s",name_given);
            snprintf(action_type,sizeof(action_type),"list");
            snprintf(payload,sizeof(payload),"%s",name);
            matched=true;break;
        }
        if(!strcmp(kind,"send_textbox")&&!strcmp(type,"inputbox")){
            if(!query_param(req,"text",payload,sizeof(payload)))break;
            snprintf(action_name,sizeof(action_name),"%s",name);
            snprintf(action_type,sizeof(action_type),"inputbox");
            matched=true;break;
        }
        if(!strcmp(kind,"send_range")&&!strcmp(type,"range")){
            if(!query_param(req,"value",payload,sizeof(payload)))break;
            char *end=NULL;long number=strtol(payload,&end,10);
            if(!*payload||!end||*end||
               number<jint((cJSON*)entry,"min",0)||
               number>jint((cJSON*)entry,"max",100))break;
            snprintf(action_name,sizeof(action_name),"%s",name);
            snprintf(action_type,sizeof(action_type),"range");
            matched=true;break;
        }
        if(!strcmp(kind,"send_color")&&!strcmp(type,"color")){
            char channels[3][8]={{0}};
            const char *const fields[]={"r","g","b"};
            bool valid=true;
            for(int j=0;j<3;j++){
                if(!query_param(req,fields[j],channels[j],sizeof(channels[j]))){
                    valid=false;break;
                }
                char *end=NULL;long channel=strtol(channels[j],&end,10);
                if(!*channels[j]||!end||*end||channel<0||channel>255){
                    valid=false;break;
                }
            }
            if(!valid)break;
            snprintf(payload,sizeof(payload),"%s,%s,%s",
                     channels[0],channels[1],channels[2]);
            snprintf(action_name,sizeof(action_name),"%s",name);
            snprintf(action_type,sizeof(action_type),"color");
            matched=true;break;
        }
    }
    cJSON_Delete(index);
    if(!matched||!action_name[0]){
        httpd_resp_send_err(req,HTTPD_404_NOT_FOUND,"Commande non programmee");
        return ESP_FAIL;
    }
    bool success=mira_jeedom_actions_enabled()?
        mira_jeedom_queue_local_action(action_name,action_type,payload):
        mira_other_transport(action_name,action_type,payload);
    if(!success){
        httpd_resp_set_status(req,"503 Service Unavailable");
        return httpd_resp_sendstr(req,"Transport inactif");
    }
    httpd_resp_set_type(req,"text/plain");
    return httpd_resp_sendstr(req,"OK");
}
/* Native equivalent of the beta GET /update?key=... command endpoint.
 * Never confuse remote state updates (info, infofa) with physical user
 * actions: only explicitly requested command kinds enter the action queue.
 * All identifiers come from the same Jeedom index as /getindex and the web UI.
 */
esp_err_t mira_jeedom_beta_legacy_get_update(httpd_req_t *req){
    if(!require_key(req))return ESP_FAIL;
    static const char *const kinds[]={
        "button","httpaction","toggle","toogle","info","infofa",
        "range","color","inputbox","list","listvalue"
    };
    char kind[24]={0},target[128]={0},value[160]={0};
    for(size_t k=0;k<sizeof(kinds)/sizeof(kinds[0]);k++){
        if(query_param(req,kinds[k],target,sizeof(target))){
            snprintf(kind,sizeof(kind),"%s",kinds[k]);
            break;
        }
    }
    if(!kind[0]){
        httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,
                            "Type de commande HTTP historique absent");
        return ESP_FAIL;
    }
    const bool list_group=!strcmp(kind,"list")||!strcmp(kind,"listvalue");
    long id=-1;
    if(!list_group){
        if(!decimal_id(target)){
            httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"ID HTTP invalide");
            return ESP_FAIL;
        }
        id=strtol(target,NULL,10);
        if(id<1||id>4096){
            httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"ID HTTP hors limites");
            return ESP_FAIL;
        }
    }
    char *raw=read_config(NULL);
    cJSON *index=raw?index_from_config(raw):NULL;
    free(raw);
    if(!index){
        httpd_resp_send_err(req,HTTPD_404_NOT_FOUND,"Programmation Mira absente");
        return ESP_FAIL;
    }
    const cJSON *arr=cJSON_GetObjectItemCaseSensitive(index,"actions");
    const cJSON *item=NULL;
    char name[160]={0},type[24]={0};
    bool passive=false,matched=false;
    if(!strcmp(kind,"list")||!strcmp(kind,"listvalue")){
        char selected[160]={0};
        if(!query_param(req,"value",selected,sizeof(selected))){
            cJSON_Delete(index);
            httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Valeur de liste absente");
            return ESP_FAIL;
        }
        cJSON_ArrayForEach(item,arr){
            if(strcmp(jstr((cJSON*)item,"type",""),"list")||
               strcmp(jstr((cJSON*)item,"nomliste",""),target))continue;
            char opt_id[20];
            snprintf(opt_id,sizeof(opt_id),"%d",jint((cJSON*)item,"id",-1));
            if((!strcmp(kind,"list")&&strcmp(opt_id,selected))||
               (!strcmp(kind,"listvalue")&&strcmp(jstr((cJSON*)item,"nom",""),selected)))continue;
            snprintf(name,sizeof(name),"%s",target);
            snprintf(type,sizeof(type),"%s","list");
            snprintf(value,sizeof(value),"%s",jstr((cJSON*)item,"nom",""));
            matched=true;break;
        }
    }else{
        cJSON_ArrayForEach(item,arr){
            const char *row_kind=jstr((cJSON*)item,"type","");
            const char *row_name=jstr((cJSON*)item,"nom","");
            if(!strcmp(kind,"toggle")||!strcmp(kind,"toogle")){
                if(strcmp(row_kind,"toggle"))continue;
                int off=jint((cJSON*)item,"id0",-1);
                int on=jint((cJSON*)item,"id1",-1);
                if(id!=off&&id!=on)continue;
                snprintf(type,sizeof(type),"toggle");
                snprintf(value,sizeof(value),"%d",id==on);
            }else{
                if(jint((cJSON*)item,"id",-1)!=id)continue;
                if(!strcmp(kind,"button")&&strcmp(row_kind,"button"))continue;
                if(!strcmp(kind,"httpaction")&&strcmp(row_kind,"httpaction"))continue;
                if(!strcmp(kind,"info")&&strcmp(row_kind,"info"))continue;
                if(!strcmp(kind,"infofa")&&strcmp(row_kind,"infofa"))continue;
                if(!strcmp(kind,"inputbox")&&strcmp(row_kind,"inputbox"))continue;
                if(!strcmp(kind,"range")&&strcmp(row_kind,"range"))continue;
                if(!strcmp(kind,"color")&&strcmp(row_kind,"color"))continue;
                snprintf(type,sizeof(type),"%s",row_kind);
                if(!strcmp(kind,"info")||!strcmp(kind,"infofa")||
                   !strcmp(kind,"inputbox")||!strcmp(kind,"range")){
                    if(!query_param(req,"value",value,sizeof(value)))break;
                    passive=!strcmp(kind,"info")||!strcmp(kind,"infofa");
                    if(!strcmp(kind,"range")){
                        char *tail=NULL;
                        double n=strtod(value,&tail);
                        if(tail==value||*tail||!isfinite(n)||
                           n<jint((cJSON*)item,"min",0)||
                           n>jint((cJSON*)item,"max",100))break;
                    }
                }else if(!strcmp(kind,"color")){
                    char parts[3][8]={{0}};
                    const char *const keys[]={"r","g","b"};
                    unsigned channels[3]={0};
                    bool okay=true;
                    for(int k=0;k<3;k++){
                        if(!query_param(req,keys[k],parts[k],sizeof(parts[k]))||
                           !decimal_id(parts[k])){okay=false;break;}
                        channels[k]=(unsigned)atoi(parts[k]);
                        if(channels[k]>255){okay=false;break;}
                    }
                    if(!okay)break;
                    snprintf(value,sizeof(value),"#%02x%02x%02x",
                             channels[0],channels[1],channels[2]);
                }else snprintf(value,sizeof(value),"1");
            }
            snprintf(name,sizeof(name),"%s",row_name);
            matched=true;break;
        }
    }
    cJSON_Delete(index);
    if(!matched||!name[0]){
        httpd_resp_send_err(req,HTTPD_404_NOT_FOUND,
                            "Commande HTTP non trouvee dans la programmation Mira");
        return ESP_FAIL;
    }
    bool success;
    if(passive){
        success=mira_full_enqueue_state(name,value);
    }else{
        /* Exactly one transport: Jeedom HTTP OR optional MQTT. A user-initiated
         * GET command can send an action; passive HTTP 'info' never does.
         */
        success=mira_jeedom_actions_enabled()?
            mira_jeedom_queue_local_action(name,type,value):
            mira_other_transport(name,type,value);
        if(success && (!strcmp(type,"range")||!strcmp(type,"color")||
                       !strcmp(type,"list")||!strcmp(type,"inputbox")))
            (void)mira_full_enqueue_state(name,value);
    }
    if(!success){
        httpd_resp_set_status(req,"503 Service Unavailable");
        return httpd_resp_sendstr(req,"Transport Mira indisponible");
    }
    httpd_resp_set_type(req,"text/plain; charset=utf-8");
    return httpd_resp_sendstr(req,"OK");
}

/* The original /load?nav=id enumerates web-command IDs for every programmed
 * control. Reuse the real Jeedom index; do not invent IDs or expose the local
 * admin secret in clickable URLs or HTML.
 */
esp_err_t mira_jeedom_render_http_reference(httpd_req_t *req){
    if(!mira_beta_web_authorized(req))return ESP_FAIL;
    httpd_resp_set_type(req,"text/html; charset=utf-8");
    httpd_resp_set_hdr(req,"Cache-Control","no-store");
    if(beta_html(req,
      "<!DOCTYPE html><html lang=\"fr\"><meta charset=\"utf-8\">"
      "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
      "<title>Mira Panel - Requêtes HTTP</title><style>"
      "body{margin:0;background:#7193A0;font-family:Arial,sans-serif;color:#263238}"
      "header{background:#1E5064;color:white;padding:20px;font-size:22px;font-weight:700}"
      "main{max-width:900px;margin:30px auto;padding:20px;background:white;border-radius:18px}"
      "article{padding:14px 0;border-bottom:1px solid #d9e2e6}"
      "code{overflow-wrap:anywhere;background:#f5f8f9;padding:5px}"
      "a{color:#1E5064}p{line-height:1.5}</style>"
      "<header>Mira Panel · Requêtes HTTP</header><main>"
      "<p><a href=\"/load?nav=params\">Retour aux paramètres</a></p>"
      "<p>Identifiants issus de la programmation Mira active. Les URL utilisent "
      "<code>VOTRE_CLE</code> comme emplacement à remplacer : la clé réelle "
      "n'est jamais affichée dans cette page.</p>")!=ESP_OK)return ESP_FAIL;
    char *raw=read_config(NULL);
    cJSON *index=raw?index_from_config(raw):NULL;
    free(raw);
    if(!index){
        if(beta_html(req,"<p>Aucune programmation Jeedom disponible.</p></main></html>")!=ESP_OK)
            return ESP_FAIL;
        return httpd_resp_send_chunk(req,NULL,0);
    }
    const cJSON *arr=cJSON_GetObjectItemCaseSensitive(index,"actions");
    const cJSON *item=NULL;
    unsigned entries=0;
    esp_err_t status=ESP_OK;
    cJSON_ArrayForEach(item,arr){
        const char *type=jstr((cJSON*)item,"type","");
        const char *name=jstr((cJSON*)item,"nom","");
        int id=jint((cJSON*)item,"id",-1);
        if(!*name)continue;
        char example[256]={0};
        if(!strcmp(type,"toggle")){
            int off=jint((cJSON*)item,"id0",-1);
            int on=jint((cJSON*)item,"id1",-1);
            if(off<1||on<1)continue;
            snprintf(example,sizeof(example),
                     "/update?key=VOTRE_CLE&amp;toggle=%d (OFF) / %d (ON)",off,on);
        }else if(!strcmp(type,"button")&&id>0){
            snprintf(example,sizeof(example),
                     "/update?key=VOTRE_CLE&amp;button=%d",id);
        }else if(!strcmp(type,"info")&&id>0){
            snprintf(example,sizeof(example),
                     "/update?key=VOTRE_CLE&amp;info=%d&amp;value=25",id);
        }else if(!strcmp(type,"range")&&id>0){
            snprintf(example,sizeof(example),
                     "/update?key=VOTRE_CLE&amp;range=%d&amp;value=50",id);
        }else if(!strcmp(type,"color")&&id>0){
            snprintf(example,sizeof(example),
                     "/update?key=VOTRE_CLE&amp;color=%d&amp;r=255&amp;g=0&amp;b=0",id);
        }else if(!strcmp(type,"list")&&id>0){
            snprintf(example,sizeof(example),
                     "/update?key=VOTRE_CLE&amp;listvalue=...&amp;value=...");
        }else continue;
        entries++;
        if((status=beta_html(req,"<article><strong>"))!=ESP_OK||
           (status=beta_html_escape(req,name))!=ESP_OK||
           (status=beta_html(req,"</strong> · "))!=ESP_OK||
           (status=beta_html_escape(req,type))!=ESP_OK||
           (status=beta_html(req,"<p><code>"))!=ESP_OK||
           (status=beta_html(req,example))!=ESP_OK||
           (status=beta_html(req,"</code></p></article>"))!=ESP_OK)break;
    }
    cJSON_Delete(index);
    if(status!=ESP_OK)return status;
    if(!entries&&beta_html(req,"<p>Aucune commande HTTP programmée.</p>")!=ESP_OK)
        return ESP_FAIL;
    if(beta_html(req,"</main></html>")!=ESP_OK)return ESP_FAIL;
    return httpd_resp_send_chunk(req,NULL,0);
}

static esp_err_t getlvgl(httpd_req_t *req) {
    if(!require_key(req))return ESP_FAIL;
    size_t n=0;char *buf=read_config(&n);
    if(!buf){httpd_resp_send_err(req,HTTPD_404_NOT_FOUND,"Configuration Mira absente");return ESP_FAIL;}
    httpd_resp_set_type(req,"application/json");
    esp_err_t err=httpd_resp_send(req,buf,(ssize_t)n);
    free(buf);return err;
}
static esp_err_t getindex(httpd_req_t *req) {
    if(!require_key(req))return ESP_FAIL;
    char *raw=read_config(NULL);
    if(!raw){httpd_resp_send_err(req,HTTPD_404_NOT_FOUND,"Configuration Mira absente");return ESP_FAIL;}
    cJSON *index=index_from_config(raw);free(raw);
    if(!index){httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Index impossible");return ESP_FAIL;}
    char *out=cJSON_PrintUnformatted(index);cJSON_Delete(index);
    if(!out){httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Memoire insuffisante");return ESP_FAIL;}
    httpd_resp_set_type(req,"application/json");
    esp_err_t err=httpd_resp_sendstr(req,out);cJSON_free(out);return err;
}
static esp_err_t getmodule(httpd_req_t *req) {
    if(!require_key(req))return ESP_FAIL;
    httpd_resp_set_type(req,"text/plain");
    return httpd_resp_sendstr(req,"Mira Panel");
}
static esp_err_t testmodule(httpd_req_t *req) {
    if(!require_key(req))return ESP_FAIL;
    httpd_resp_set_type(req,"text/plain");
    return httpd_resp_sendstr(req,"Tout est OK");
}
static esp_err_t displayinfo(httpd_req_t *req) {
    if(!require_key(req))return ESP_FAIL;
    httpd_resp_set_type(req,"application/json");
#ifdef MIRA_PANEL_7_INCH
    const char *profile="waveshare-esp32s3-touch-lcd-7";
#else
    const char *profile="waveshare-esp32s3-touch-lcd-4.3c";
#endif
    char response[512];
    snprintf(response,sizeof(response),
             "{\"profile\":\"%s\",\"width\":800,\"height\":480,"
             "\"contentWidth\":800,\"contentHeight\":430,\"navHeight\":50,"
             "\"rotation\":0,\"logicalWidth\":1000,\"logicalHeight\":1000,"
             "\"httpPrimary\":true,\"mqttOptional\":true,\"freeLayout\":true,"
             "\"widgetLabelModes\":\"auto,text,icon,both\","
             "\"widgetIcons\":\"auto,temperature,humidity,pressure,light,energy,plug,door\"}",profile);
    return httpd_resp_sendstr(req,response);
}
// Arduino beta's AddListLVGL()/RemoveListLVGL()/SaveListLVGL() contract.
// A series of Jeedom GET requests edits a *staged* list, without touching the
// on-flash configuration until /savelistlvgl or /screensave. The existing
// root JSON object and its other keys are retained byte-semantically.
static cJSON *beta_lvgl_stage;
static bool beta_lvgl_dirty;

static bool beta_lvgl_ensure_stage(void){
    if(beta_lvgl_stage)return true;
    char *raw=read_config(NULL);
    if(raw){
        beta_lvgl_stage=cJSON_Parse(raw);
        free(raw);
        if(!beta_lvgl_stage ||
           !cJSON_IsObject(beta_lvgl_stage) ||
           !cJSON_IsArray(cJSON_GetObjectItemCaseSensitive(beta_lvgl_stage,"lvgl"))){
            cJSON_Delete(beta_lvgl_stage);
            beta_lvgl_stage=NULL;
            return false; // Never overwrite a malformed existing backup.
        }
    }else{
        beta_lvgl_stage=cJSON_CreateObject();
        if(!beta_lvgl_stage)return false;
        if(!cJSON_AddArrayToObject(beta_lvgl_stage,"lvgl")){
            cJSON_Delete(beta_lvgl_stage);
            beta_lvgl_stage=NULL;
            return false;
        }
    }
    return true;
}
static bool beta_lvgl_put(cJSON *obj,const char *name,const char *value){
    return cJSON_AddStringToObject(obj,name,value)!=NULL;
}
static bool beta_lvgl_optional(httpd_req_t *req,const char *field,char *out,
                               size_t length,const char *fallback){
    if(query_param(req,field,out,length))return true;
    snprintf(out,length,"%s",fallback?fallback:"");
    return true;
}
static esp_err_t beta_lvgl_respond(httpd_req_t *req,const char *message){
    httpd_resp_set_type(req,"text/plain; charset=utf-8");
    return httpd_resp_sendstr(req,message);
}
static esp_err_t beta_lvgl_clear(httpd_req_t *req){
    if(!require_key(req))return ESP_FAIL;
    if(!beta_lvgl_ensure_stage()){
        httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Configuration LVGL invalide");
        return ESP_FAIL;
    }
    cJSON_DeleteItemFromObjectCaseSensitive(beta_lvgl_stage,"lvgl");
    if(!cJSON_AddArrayToObject(beta_lvgl_stage,"lvgl")){
        cJSON_Delete(beta_lvgl_stage);beta_lvgl_stage=NULL;
        httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Memoire LVGL insuffisante");
        return ESP_FAIL;
    }
    beta_lvgl_dirty=true;
    return beta_lvgl_respond(req,!strncmp(req->uri,"/screenformat",13)?"Liste vidée!":"OK");
}
static esp_err_t beta_lvgl_append(httpd_req_t *req){
    if(!require_key(req))return ESP_FAIL;
    char type[40]={0},name[97]={0},location[24]={0};
    char minimum[40]={0},maximum[40]={0},value[160]={0},unit[64]={0};
    if(!query_param(req,"type",type,sizeof(type))||
       !query_param(req,"name",name,sizeof(name))||
       !query_param(req,"loc",location,sizeof(location))||
       !query_param(req,"min",minimum,sizeof(minimum))||
       !query_param(req,"max",maximum,sizeof(maximum))||
       !query_param(req,"value",value,sizeof(value))||
       !query_param(req,"unit",unit,sizeof(unit))||
       !*type){
        httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Champs LVGL type/name/loc/min/max/value/unit requis");
        return ESP_FAIL;
    }
    char style[32],layout[16],label[12],icon[32],display_name[97];
    beta_lvgl_optional(req,"style",style,sizeof(style),"standard");
    beta_lvgl_optional(req,"layout",layout,sizeof(layout),"auto");
    beta_lvgl_optional(req,"labelMode",label,sizeof(label),"auto");
    beta_lvgl_optional(req,"icon",icon,sizeof(icon),"auto");
    beta_lvgl_optional(req,"displayName",display_name,sizeof(display_name),"");
    if(strcmp(layout,"tile")&&strcmp(layout,"wide")&&
       strcmp(layout,"hero")&&strcmp(layout,"free"))snprintf(layout,sizeof(layout),"auto");
    if(strcmp(label,"text")&&strcmp(label,"icon")&&strcmp(label,"both"))
        snprintf(label,sizeof(label),"auto");
    if(!*style)snprintf(style,sizeof(style),"standard");
    if(strlen(icon)>24)snprintf(icon,sizeof(icon),"auto");
    int position[4]={0};
    const char *const fields[]={"lx","ly","lw","lh"};
    if(!strcmp(layout,"free")){
        bool geometry_valid=true;
        for(int i=0;i<4;i++){
            char number[20]={0};char *end=NULL;
            if(!query_param(req,fields[i],number,sizeof(number))||!*number){
                geometry_valid=false;break;
            }
            long n=strtol(number,&end,10);
            if(*end||n<0||n>1000){geometry_valid=false;break;}
            position[i]=(int)n;
        }
        if(position[2]<=0||position[3]<=0)geometry_valid=false;
        if(!geometry_valid)snprintf(layout,sizeof(layout),"auto");
    }
    if(!beta_lvgl_ensure_stage()){
        httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Configuration LVGL invalide");
        return ESP_FAIL;
    }
    cJSON *array=cJSON_GetObjectItemCaseSensitive(beta_lvgl_stage,"lvgl");
    if(cJSON_GetArraySize(array)>=64){
        httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Maximum 64 widgets LVGL");
        return ESP_FAIL;
    }
    cJSON *item=cJSON_CreateObject();
    if(!item){
        httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Memoire LVGL insuffisante");
        return ESP_FAIL;
    }
    bool valid=beta_lvgl_put(item,"type",type)&&
        beta_lvgl_put(item,"name",name)&&
        beta_lvgl_put(item,"emplacement",location)&&
        beta_lvgl_put(item,"min",minimum)&&beta_lvgl_put(item,"max",maximum)&&
        beta_lvgl_put(item,"value",value)&&beta_lvgl_put(item,"unit",unit)&&
        beta_lvgl_put(item,"style",style)&&beta_lvgl_put(item,"layout",layout)&&
        beta_lvgl_put(item,"labelMode",label)&&beta_lvgl_put(item,"icon",icon)&&
        beta_lvgl_put(item,"displayName",display_name);
    if(valid&&!strcmp(layout,"free")){
        const char *const coords[]={"lx","ly","lw","lh"};
        for(int i=0;i<4&&valid;i++)
            valid=cJSON_AddNumberToObject(item,coords[i],position[i])!=NULL;
    }
    if(valid&&strcmp(type,"info")&&strcmp(type,"datetime"))
        valid=beta_lvgl_put(item,"url","");
    if(!valid){
        cJSON_Delete(item);
        httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Allocation widget impossible");
        return ESP_FAIL;
    }
    cJSON_AddItemToArray(array,item);
    beta_lvgl_dirty=true;
    if(!strncmp(req->uri,"/screenadd",10)){
        char *preview=cJSON_Print(beta_lvgl_stage);
        if(!preview){
            httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Apercu JSON impossible");
            return ESP_FAIL;
        }
        httpd_resp_set_type(req,"text/plain; charset=utf-8");
        esp_err_t result=httpd_resp_sendstr(req,preview);
        cJSON_free(preview);
        return result;
    }
    return beta_lvgl_respond(req,"OK");
}
static esp_err_t beta_lvgl_save(httpd_req_t *req){
    if(!require_key(req))return ESP_FAIL;
    if(!beta_lvgl_dirty)return beta_lvgl_respond(req,
            !strncmp(req->uri,"/screensave",11)?"Liste sauvegardée!":"OK");
    if(!beta_lvgl_stage){
        httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Aucune liste LVGL en attente");
        return ESP_FAIL;
    }
    char *out=cJSON_PrintUnformatted(beta_lvgl_stage);
    if(!out){
        httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"JSON LVGL impossible");
        return ESP_FAIL;
    }
    size_t len=strlen(out);
    if(len>MIRA_JSON_MAX){
        cJSON_free(out);
        httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Liste LVGL trop volumineuse");
        return ESP_FAIL;
    }
    FILE *file=fopen(MIRA_JSON_TMP,"wb");
    if(!file){
        cJSON_free(out);
        httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"SPIFFS non accessible");
        return ESP_FAIL;
    }
    size_t bytes=fwrite(out,1,len,file);
    int closed=fclose(file);
    cJSON_free(out);
    if(bytes!=len||closed!=0||rename(MIRA_JSON_TMP,MIRA_JSON_PATH)!=0){
        remove(MIRA_JSON_TMP);
        httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Sauvegarde LVGL impossible");
        return ESP_FAIL;
    }
    cJSON_Delete(beta_lvgl_stage);beta_lvgl_stage=NULL;
    beta_lvgl_dirty=false;
    mira_full_request_reload(); // always reload within the LVGL task
    request_binding_sync();      // command IDs from Jeedom may have changed
    return beta_lvgl_respond(req,
            !strncmp(req->uri,"/screensave",11)?"Liste sauvegardée!":"OK");
}
static esp_err_t programui(httpd_req_t *req) {
    const size_t n=req->content_len;
    if(n<10||n>MIRA_FORM_MAX) {
        httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Programmation trop volumineuse");
        return ESP_FAIL;
    }
    char *body=malloc(n+1);
    if(!body){httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"RAM insuffisante");return ESP_FAIL;}
    if(!read_exact_request(req,body,n)){
        free(body);httpd_resp_send_err(req,HTTPD_408_REQ_TIMEOUT,"Requete incomplete");return ESP_FAIL;
    }
    // POST body is application/x-www-form-urlencoded: key=...&json=...
    // Extract the field slices without assuming order; decode only each field's value.
    char *encoded_key=NULL,*encoded_json=NULL;
    for(char *p=body;p&&*p;){
        char *next=strchr(p,'&');
        if(next)*next++=0;
        char *sep=strchr(p,'=');
        if(sep){
            *sep++=0;
            if(strcmp(p,"key")==0)encoded_key=sep;
            if(strcmp(p,"json")==0)encoded_json=sep;
        }
        p=next;
    }
    if(!encoded_key||!encoded_json||!decode_url(encoded_key)||!correct_key(encoded_key)){
        free(body);httpd_resp_send_err(req,HTTPD_403_FORBIDDEN,"Cle Mira invalide");return ESP_FAIL;
    }
    if(!decode_url(encoded_json)||strlen(encoded_json)>MIRA_JSON_MAX){
        free(body);httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"JSON invalide ou trop gros");return ESP_FAIL;
    }
    size_t len=strlen(encoded_json);
    cJSON *root=cJSON_ParseWithLength(encoded_json,len);
    const cJSON *widgets=root?cJSON_GetObjectItemCaseSensitive(root,"lvgl"):NULL;
    bool valid=cJSON_IsArray(widgets);
    const int count=valid?cJSON_GetArraySize(widgets):0;
    if(count>64)valid=false;  // Same bound as Mira's native LVGL importer.
    if(valid){
        const cJSON *entry=NULL;
        cJSON_ArrayForEach(entry,widgets){
            if(!cJSON_IsObject(entry)){valid=false;break;}
        }
    }
    if(root)cJSON_Delete(root);
    if(!valid){
        free(body);
        httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,
                            "Programmation Mira mal formee ou trop de widgets");
        return ESP_FAIL;
    }
    FILE *out=fopen(MIRA_JSON_TMP,"wb");
    if(!out){free(body);httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"SPIFFS non accessible");return ESP_FAIL;}
    size_t written=fwrite(encoded_json,1,len,out);
    int close_error=fclose(out);
    free(body);
    if(written!=len||close_error!=0||rename(MIRA_JSON_TMP,MIRA_JSON_PATH)!=0){
        remove(MIRA_JSON_TMP);
        httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Enregistrement Mira echoue");return ESP_FAIL;
    }
    cJSON_Delete(beta_lvgl_stage);beta_lvgl_stage=NULL;beta_lvgl_dirty=false;
    mira_full_request_reload();
    request_binding_sync(); // get fresh Jeedom IDs; ignore stale index until plugin re-synchronizes
    ESP_LOGI(TAG,"Programmation Jeedom enregistree (%u octets), UI reload en file",(unsigned)len);
    httpd_resp_set_type(req,"text/plain");
    return httpd_resp_sendstr(req,"OK");
}
// Maps Jeedom's numeric action IDs into local PASSIVE widget state updates.
// Remote "button" does not create a new outgoing MQTT/Jeedom action.
static esp_err_t updatenr(httpd_req_t *req) {
    if(!require_key(req))return ESP_FAIL;
    const char *types[]={"info","toggle","toogle","range","list","color","button"};
    char selected[24]={0},action_id[16]={0},value[769]={0};
    for(size_t i=0;i<sizeof(types)/sizeof(types[0]);i++){
        if(query_param(req,types[i],action_id,sizeof(action_id))){
            snprintf(selected,sizeof(selected),"%s",types[i]);break;
        }
    }
    if(!*selected){httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Type d'action absent");return ESP_FAIL;}
    char *end=NULL;long id=strtol(action_id,&end,10);
    if(end==action_id||*end!=0||id<1||id>4096){
        httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"ID invalide");return ESP_FAIL;
    }
    char *raw=read_config(NULL);
    cJSON *index=raw?index_from_config(raw):NULL;
    free(raw);
    if(!index){httpd_resp_send_err(req,HTTPD_404_NOT_FOUND,"Index absent");return ESP_FAIL;}
    const cJSON *array=cJSON_GetObjectItemCaseSensitive(index,"actions"),*entry=NULL;
    char name[96]={0};bool found=false;
    cJSON_ArrayForEach(entry,array){
        const char *kind=jstr((cJSON*)entry,"type","");
        if((!strcmp(selected,"toggle")||!strcmp(selected,"toogle")) && !strcmp(kind,"toggle")){
            int off=jint((cJSON*)entry,"id0",-1),on=jint((cJSON*)entry,"id1",-1);
            if(id==off||id==on){
                snprintf(name,sizeof(name),"%s",jstr((cJSON*)entry,"nom",""));
                snprintf(value,sizeof(value),"%s",id==on?"1":"0");
                found=true;break;
            }
        }else if(!strcmp(kind,selected) && jint((cJSON*)entry,"id",-1)==id){
            if(!strcmp(kind,"list")){
                snprintf(name,sizeof(name),"%s",jstr((cJSON*)entry,"nomliste",""));
                snprintf(value,sizeof(value),"%s",jstr((cJSON*)entry,"nom",""));
            } else {
                snprintf(name,sizeof(name),"%s",jstr((cJSON*)entry,"nom",""));
                if(!strcmp(kind,"info")||!strcmp(kind,"range")){
                    if(!query_param(req,"value",value,sizeof(value)))break;
                }else if(!strcmp(kind,"color")){
                    char r[8],g[8],b[8];
                    if(!query_param(req,"r",r,sizeof(r))||
                       !query_param(req,"g",g,sizeof(g))||
                       !query_param(req,"b",b,sizeof(b)))break;
                    unsigned ri=0,gi=0,bi=0;
                    char trailing=0;
                    if(sscanf(r,"%u %c",&ri,&trailing)!=1||
                       sscanf(g,"%u %c",&gi,&trailing)!=1||
                       sscanf(b,"%u %c",&bi,&trailing)!=1||
                       ri>255||gi>255||bi>255)break;
                    snprintf(value,sizeof(value),"#%02x%02x%02x",ri,gi,bi);
                }else if(!strcmp(kind,"button")){
                    // This legacy endpoint receives a button ID but cannot safely run an action from a state refresh.
                    snprintf(value,sizeof(value),"1");
                }
            }
            found=true;break;
        }
    }
    cJSON_Delete(index);
    if(!found||!*name){
        httpd_resp_send_err(req,HTTPD_404_NOT_FOUND,"Commande inconnue");return ESP_FAIL;
    }
    if(strcmp(selected,"button")!=0) {
        // The beta media JPEG URL may be 768 bytes; all other state values
        // stay capped at 159 bytes and use the existing small queue.
        const size_t namelen=strlen(name);
        const bool is_cover=namelen>=7&&!strcmp(name+namelen-7,"__cover");
        if(strlen(value)>(is_cover?768u:159u)){
            httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Valeur d'info trop longue");
            return ESP_FAIL;
        }
        if(!mira_full_enqueue_state(name,value)){
            httpd_resp_set_status(req,"503 Service Unavailable");
            return httpd_resp_sendstr(req,"File d'etats pleine");
        }
    }
    httpd_resp_set_type(req,"text/plain");
    return httpd_resp_sendstr(req,"OK");
}
// Provision the existing Jeedom screen password on the recovery AP.
// This separate NVS namespace never alters the old ESP firmware's NVS or SPIFFS files.
static esp_err_t provision_key(httpd_req_t *req) {
    if(!mira_setup_require_ap(req))return ESP_FAIL;
    if(req->content_len<9||req->content_len>256) {
        httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"JSON key attendu");
        return ESP_FAIL;
    }
    char body[257];
    if(!read_exact_request(req,body,req->content_len)){
        httpd_resp_send_err(req,HTTPD_408_REQ_TIMEOUT,"JSON incomplet");return ESP_FAIL;
    }
    cJSON *json=cJSON_Parse(body);
    const cJSON *key=json?cJSON_GetObjectItemCaseSensitive(json,"key"):NULL;
    if(!cJSON_IsString(key)||!key->valuestring||strlen(key->valuestring)<1||
       strlen(key->valuestring)>MIRA_KEY_MAX){
        if(json)cJSON_Delete(json);
        httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Cle de 1 a 96 caracteres requise");return ESP_FAIL;
    }
    nvs_handle_t nvs;
    esp_err_t err=nvs_open("mira_idf_auth",NVS_READWRITE,&nvs);
    if(err==ESP_OK){
        err=nvs_set_str(nvs,"jeedom_key",key->valuestring);
        if(err==ESP_OK)err=nvs_commit(nvs);
        nvs_close(nvs);
    }
    cJSON_Delete(json);
    if(err!=ESP_OK){httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"NVS indisponible");return ESP_FAIL;}
    ESP_LOGI(TAG,"Cle API Jeedom provisionnee sans journaliser sa valeur");
    return httpd_resp_sendstr(req,"OK");
}

// The plugin saves its Jeedom parameters through legacy GET endpoints.
// Keep those settings in a DIFFERENT namespace: never rewrite the old
// /params.json, so a rollback to the stable firmware remains possible.
// NVS storage here is not encrypted; this is experimental, not production-ready.
// Preserve the beta's GET/SET parameter names while storing typed display
// values in the dedicated IDF UI namespace. The screen's LVGL task reloads
// these values via a flag; this HTTP handler never touches LVGL directly.
typedef struct {const char *legacy,*key;uint32_t max;bool clock;} mira_display_field_t;
static const mira_display_field_t display_fields[]={
    {"DISPLAY_Jour","day_pct",100,false},
    {"DISPLAY_Nuit","night_pct",100,false},
    {"DISPLAY_Veille","sleep_light",100,false},
    {"DISPLAY_Delai","sleep_delay",86400,false},
    {"DISPLAY_NuitAuto","night_auto",1,false},
    {"DISPLAY_HorlogeVeille","sleep_clock",1,false},
    {"DISPLAY_DebutNuit","night_start",0,true},
    {"DISPLAY_FinNuit","night_end",0,true},
};
static const mira_display_field_t *display_field(const char *legacy){
    for(size_t i=0;i<sizeof(display_fields)/sizeof(display_fields[0]);i++)
        if(!strcmp(display_fields[i].legacy,legacy))return &display_fields[i];
    return NULL;
}
static bool display_field_write(const mira_display_field_t *field,const char *value){
    nvs_handle_t nv;
    if(nvs_open("mira_idf_ui",NVS_READWRITE,&nv)!=ESP_OK)return false;
    esp_err_t err=ESP_ERR_INVALID_ARG;
    if(field->clock){
        if(strlen(value)==5&&value[2]==':'&&
           isdigit((unsigned char)value[0])&&isdigit((unsigned char)value[1])&&
           isdigit((unsigned char)value[3])&&isdigit((unsigned char)value[4])){
            int hour=(value[0]-'0')*10+(value[1]-'0');
            int minute=(value[3]-'0')*10+(value[4]-'0');
            if(hour<24&&minute<60)err=nvs_set_str(nv,field->key,value);
        }
    }else{
        char *end=NULL;
        unsigned long number=strtoul(value,&end,10);
        if(value[0]&&end&&!*end&&number<=field->max&&
           ((strcmp(field->key,"day_pct")&&strcmp(field->key,"night_pct"))||number>=1)){
            if(field->max>255)err=nvs_set_u32(nv,field->key,(uint32_t)number);
            else err=nvs_set_u8(nv,field->key,(uint8_t)number);
        }
    }
    if(err==ESP_OK)err=nvs_commit(nv);
    nvs_close(nv);
    if(err==ESP_OK)mira_full_request_settings_reload();
    return err==ESP_OK;
}
static bool display_field_read(const mira_display_field_t *field,char *value,size_t cap){
    nvs_handle_t nv;
    if(nvs_open("mira_idf_ui",NVS_READONLY,&nv)!=ESP_OK)return false;
    esp_err_t err;
    if(field->clock){
        size_t size=cap;
        err=nvs_get_str(nv,field->key,value,&size);
    }else if(field->max>255){
        uint32_t v=0;err=nvs_get_u32(nv,field->key,&v);
        if(err!=ESP_OK){
            uint16_t old=0;
            if(nvs_get_u16(nv,field->key,&old)==ESP_OK){
                v=old;err=ESP_OK;
            }
        }
        if(err==ESP_OK)snprintf(value,cap,"%u",(unsigned)v);
    }else{
        uint8_t v=0;err=nvs_get_u8(nv,field->key,&v);
        if(err==ESP_OK)snprintf(value,cap,"%u",(unsigned)v);
    }
    nvs_close(nv);
    return err==ESP_OK;
}
static bool supported_jeedom_param(const char *name){
    if(display_field(name))return true;
    static const char *allowed[]={
        "Jeedom","Jeedom_IP","Jeedom_Key","Jeedom_Key_mira",
        "Jeedom_Refresh","Jeedom_Init_Cmd"
    };
    for(size_t i=0;i<sizeof(allowed)/sizeof(allowed[0]);i++)
        if(strcmp(name,allowed[i])==0)return true;
    return false;
}
static esp_err_t param_get(httpd_req_t *req){
    if(!require_key(req))return ESP_FAIL;
    char name[32];
    if(!query_param(req,"value",name,sizeof(name))||!supported_jeedom_param(name)){
        httpd_resp_set_type(req,"text/plain");
        return httpd_resp_sendstr(req,"Null");
    }
    const mira_display_field_t *display=display_field(name);
    if(display){
        char value[48]={0};
        bool ok=display_field_read(display,value,sizeof(value));
        httpd_resp_set_type(req,"text/plain");
        return httpd_resp_sendstr(req,ok?value:"Null");
    }
    nvs_handle_t nv;
    if(nvs_open("mira_idf_jeed",NVS_READONLY,&nv)!=ESP_OK)
        return httpd_resp_sendstr(req,!strcmp(name,"Jeedom")?"1":"Null");
    char value[256]={0};size_t len=sizeof(value);
    esp_err_t err=nvs_get_str(nv,name,value,&len);
    nvs_close(nv);
    httpd_resp_set_type(req,"text/plain");
    return httpd_resp_sendstr(req,err==ESP_OK?value:(!strcmp(name,"Jeedom")?"1":"Null"));
}
// The original beta implements /get?key=...&toggle|info|infofa|id|param=...
// Read-only: this endpoint MUST NOT issue an actuator or a Jeedom command.
static esp_err_t beta_get(httpd_req_t *req){
    if(!require_key(req))return ESP_FAIL;
    char kind[16]={0},argument[64]={0};
    static const char *const selectors[]={"toggle","toogle","info","infofa","id","param"};
    for(size_t i=0;i<sizeof(selectors)/sizeof(selectors[0]);i++){
        if(query_param(req,selectors[i],argument,sizeof(argument))){
            snprintf(kind,sizeof(kind),"%s",selectors[i]);
            break;
        }
    }
    httpd_resp_set_type(req,"text/plain; charset=utf-8");
    if(!*kind)return httpd_resp_sendstr(req,"Null");
    if(!strcmp(kind,"param")){
        if(!supported_jeedom_param(argument))return httpd_resp_sendstr(req,"Null");
        const mira_display_field_t *df=display_field(argument);
        if(df){
            char display_value[48]={0};
            if(display_field_read(df,display_value,sizeof(display_value)))
                return httpd_resp_sendstr(req,display_value);
            return httpd_resp_sendstr(req,"Null");
        }
        nvs_handle_t nv;
        if(nvs_open("mira_idf_jeed",NVS_READONLY,&nv)==ESP_OK){
            char result[256]={0};size_t z=sizeof(result);
            esp_err_t got=nvs_get_str(nv,argument,result,&z);
            nvs_close(nv);
            if(got==ESP_OK){
                esp_err_t sent=httpd_resp_sendstr(req,result);
                memset(result,0,sizeof(result));
                return sent;
            }
        }
        return httpd_resp_sendstr(req,"Null");
    }
    char *raw=read_config(NULL);
    cJSON *index=raw?index_from_config(raw):NULL;
    free(raw);
    if(!index)return httpd_resp_sendstr(req,"Null");
    cJSON *actions=cJSON_GetObjectItemCaseSensitive(index,"actions");
    const cJSON *entry=NULL;
    if(!strcmp(kind,"id")){
        esp_err_t out=httpd_resp_send_chunk(req,"Mira Panel: Identite des commandes.\n\n",HTTPD_RESP_USE_STRLEN);
        cJSON_ArrayForEach(entry,actions){
            const char *type=jstr((cJSON*)entry,"type","");
            const char *name=jstr((cJSON*)entry,"nom","");
            char line[340];
            if(!strcmp(type,"toggle")){
                snprintf(line,sizeof(line),"Nom: %s\nID %s: %d\nID %s: %d\n\n",name,
                    jstr((cJSON*)entry,"value0","Off"),jint((cJSON*)entry,"id0",0),
                    jstr((cJSON*)entry,"value1","On"),jint((cJSON*)entry,"id1",0));
            }else if(!strcmp(type,"button")||!strcmp(type,"info")){
                snprintf(line,sizeof(line),"Nom: %s\nID: %d\n\n",name,jint((cJSON*)entry,"id",0));
            }else continue;
            if(out==ESP_OK)out=httpd_resp_send_chunk(req,line,HTTPD_RESP_USE_STRLEN);
        }
        cJSON_Delete(index);
        if(out!=ESP_OK)return out;
        return httpd_resp_send_chunk(req,NULL,0);
    }
    char *end=NULL;
    long local=strtol(argument,&end,10);
    if(!argument[0]||!end||*end||local<1||local>4096){
        cJSON_Delete(index);return httpd_resp_sendstr(req,"Null");
    }
    char result[160]={0};
    bool found=false;
    const bool is_toggle=!strcmp(kind,"toggle")||!strcmp(kind,"toogle");
    cJSON_ArrayForEach(entry,actions){
        const char *type=jstr((cJSON*)entry,"type","");
        bool match=is_toggle?!strcmp(type,"toggle")&&
            (jint((cJSON*)entry,"id0",-1)==local||jint((cJSON*)entry,"id1",-1)==local):
            !strcmp(type,"info")&&jint((cJSON*)entry,"id",-1)==local;
        if(!match)continue;
        const char *name=jstr((cJSON*)entry,"nom","");
        found=mira_full_cached_state(name,result,sizeof(result));
        if(!found&&!is_toggle){
            const char *initial=jstr((cJSON*)entry,"value","");
            if(*initial){snprintf(result,sizeof(result),"%s",initial);found=true;}
        }
        break;
    }
    cJSON_Delete(index);
    return httpd_resp_sendstr(req,found?result:"Null");
}
// Mirror beta /getallparams: return original params.json untouched when it
// exists; on a clean IDF install synthesize the equivalent {params:[...]} from
// the native NVS values so the Jeedom configuration editor can read them.
static esp_err_t getallparams(httpd_req_t *req){
    if(!require_key(req))return ESP_FAIL;
    FILE *fp=fopen("/spiffs/params.json","rb");
    if(fp){
        long length=-1;
        if(fseek(fp,0,SEEK_END)==0)length=ftell(fp);
        if(length>1&&length<=32768&&fseek(fp,0,SEEK_SET)==0){
            char *buf=malloc((size_t)length+1);
            if(buf){
                size_t n=fread(buf,1,(size_t)length,fp);
                buf[n]=0;
                if(n==(size_t)length){
                    cJSON *root=cJSON_ParseWithLength(buf,n);
                    const cJSON *params=root?cJSON_GetObjectItemCaseSensitive(root,"params"):NULL;
                    bool valid=cJSON_IsArray(params);
                    if(root)cJSON_Delete(root);
                    if(valid){
                        // Keep the legacy JSON structure and encrypted secrets, but
                        // overlay native IDF settings written after migration.
                        // Do not expose plaintext Jeedom keys stored in NVS.
                        root=cJSON_ParseWithLength(buf,n);
                        cJSON *entries=root?cJSON_GetObjectItemCaseSensitive(root,"params"):NULL;
                        bool changed=false;
                        if(cJSON_IsArray(entries)){
                            cJSON *entry=NULL;
                            cJSON_ArrayForEach(entry,entries){
                                const char *name=jstr(entry,"param","");
                                if(!*name||strstr(name,"Key"))continue;
                                char value[256]={0};
                                bool have=false;
                                const mira_display_field_t *display=display_field(name);
                                if(display){
                                    have=display_field_read(display,value,sizeof(value));
                                }else if(supported_jeedom_param(name)){
                                    nvs_handle_t nv;
                                    if(nvs_open("mira_idf_jeed",NVS_READONLY,&nv)==ESP_OK){
                                        size_t cap=sizeof(value);
                                        have=nvs_get_str(nv,name,value,&cap)==ESP_OK;
                                        nvs_close(nv);
                                    }
                                }
                                if(have){
                                    cJSON *newvalue=cJSON_CreateString(value);
                                    if(newvalue){
                                        cJSON_ReplaceItemInObjectCaseSensitive(entry,"value",newvalue);
                                        changed=true;
                                    }
                                }
                                memset(value,0,sizeof(value));
                            }
                        }
                        if(changed){
                            char *updated=cJSON_PrintUnformatted(root);
                            if(updated){
                                fclose(fp);
                                httpd_resp_set_type(req,"application/json");
                                esp_err_t out=httpd_resp_sendstr(req,updated);
                                free(updated);cJSON_Delete(root);free(buf);
                                return out;
                            }
                        }
                        if(root)cJSON_Delete(root);
                        fclose(fp);
                        httpd_resp_set_type(req,"application/json");
                        esp_err_t out=httpd_resp_send(req,buf,n);
                        free(buf);
                        return out;
                    }
                }
                free(buf);
            }
        }
        fclose(fp);
    }
    cJSON *doc=cJSON_CreateObject();
    cJSON *params=doc?cJSON_AddArrayToObject(doc,"params"):NULL;
    if(!params){if(doc)cJSON_Delete(doc);return httpd_resp_send_err(
        req,HTTPD_500_INTERNAL_SERVER_ERROR,"Memoire indisponible"),ESP_FAIL;}
    nvs_handle_t nv;
    const bool have_nvs=nvs_open("mira_idf_jeed",NVS_READONLY,&nv)==ESP_OK;
    static const char *const names[]={
        "Jeedom","Jeedom_IP","Jeedom_Key","Jeedom_Key_mira",
        "Jeedom_Refresh","Jeedom_Init_Cmd"
    };
    for(size_t i=0;i<sizeof(names)/sizeof(names[0]);i++){
        // Beta params.json contains encrypted ciphertext for secret keys.
        // IDF stores those in NVS but has no compatible beta encryption
        // writer: never export raw secrets in synthesized JSON responses.
        if(strstr(names[i],"Key"))continue;
        char value[256]={0};size_t cap=sizeof(value);
        if(!have_nvs||nvs_get_str(nv,names[i],value,&cap)!=ESP_OK){
            if(strcmp(names[i],"Jeedom"))continue;
            strcpy(value,"1"); // Original Mira IDF default transport: HTTP
        }
        cJSON *entry=cJSON_CreateObject();
        if(!entry)continue;
        cJSON_AddStringToObject(entry,"param",names[i]);
        cJSON_AddStringToObject(entry,"value",value);
        cJSON_AddStringToObject(entry,"type",
            strstr(names[i],"Key")?"hidden-text":"text");
        cJSON_AddItemToArray(params,entry);
        memset(value,0,sizeof(value));
    }
    if(have_nvs)nvs_close(nv);
    for(size_t i=0;i<sizeof(display_fields)/sizeof(display_fields[0]);i++){
        char value[48]={0};
        if(!display_field_read(&display_fields[i],value,sizeof(value)))continue;
        cJSON *entry=cJSON_CreateObject();
        if(!entry)continue;
        cJSON_AddStringToObject(entry,"param",display_fields[i].legacy);
        cJSON_AddStringToObject(entry,"value",value);
        cJSON_AddStringToObject(entry,"type","text");
        cJSON_AddItemToArray(params,entry);
    }
    char *out=cJSON_PrintUnformatted(doc);
    cJSON_Delete(doc);
    if(!out){
        httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"JSON indisponible");
        return ESP_FAIL;
    }
    httpd_resp_set_type(req,"application/json");
    esp_err_t err=httpd_resp_sendstr(req,out);
    cJSON_free(out);
    return err;
}
static esp_err_t param_set(httpd_req_t *req){
    if(!require_key(req))return ESP_FAIL;
    char name[32],value[256];
    if(!query_param(req,"param",name,sizeof(name))||
       !query_param(req,"value",value,sizeof(value))||
       !supported_jeedom_param(name)||
       (!strcmp(name,"Jeedom")&&strcmp(value,"0")&&strcmp(value,"1"))){
        httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Parametre Jeedom invalide");
        return ESP_FAIL;
    }
    const mira_display_field_t *display=display_field(name);
    if(display){
        if(!display_field_write(display,value)){
            httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Reglage d'affichage invalide");
            return ESP_FAIL;
        }
        return httpd_resp_sendstr(req,"OK");
    }
    nvs_handle_t nv;
    esp_err_t err=nvs_open("mira_idf_jeed",NVS_READWRITE,&nv);
    if(err==ESP_OK) {
        err=nvs_set_str(nv,name,value);
        if(err==ESP_OK)err=nvs_commit(nv);
        nvs_close(nv);
    }
    if(err!=ESP_OK){
        httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Sauvegarde Jeedom impossible");
        return ESP_FAIL;
    }
    if(!strcmp(name,"Jeedom"))jeedom_mode_enabled=!strcmp(value,"1");
    ESP_LOGI(TAG,"Jeedom parametre %s synchronise",name); // NEVER log its value.
    request_binding_sync();
    return httpd_resp_sendstr(req,"OK");
}
static esp_err_t param_saved(httpd_req_t *req){
    if(!require_key(req))return ESP_FAIL;
    // nvs_commit() already called by param_set().
    return httpd_resp_sendstr(req,"OK");
}
// Jeedom plugin associates its command IDs with Mira widget IDs.
// Store mappings without executing them, especially during initialization.
static bool put_remote_binding(long id,const char *value){
    if(id<1||id>4096||!value||!*value||strlen(value)>30)return false;
    char *config=read_config(NULL);
    if(!config)return false;
    const uint32_t hash=ui_config_hash(config);
    free(config);
    char key[14],binding[48];
    snprintf(key,sizeof(key),"cmd%ld",id);
    int n=snprintf(binding,sizeof(binding),"%08" PRIx32 ":%s",hash,value);
    if(n<0||n>=(int)sizeof(binding))return false;
    nvs_handle_t nv;
    esp_err_t err=nvs_open("mira_idf_map",NVS_READWRITE,&nv);
    if(err==ESP_OK){
        err=nvs_set_str(nv,key,binding);
        if(err==ESP_OK)err=nvs_commit(nv);
        nvs_close(nv);
    }
    return err==ESP_OK;
}
static esp_err_t set_jeedom_id(httpd_req_t *req){
    if(!require_key(req))return ESP_FAIL;
    char id[16],value[32];
    if(!query_param(req,"id",id,sizeof(id))||
       !query_param(req,"value",value,sizeof(value))||
       !put_remote_binding(strtol(id,NULL,10),value)){
        httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Association Jeedom impossible");
        return ESP_FAIL;
    }
    return httpd_resp_sendstr(req,"OK");
}
static esp_err_t set_jeedom_toggle_id(httpd_req_t *req){
    if(!require_key(req))return ESP_FAIL;
    char id[16],off[32],on[32];
    if(!query_param(req,"id",id,sizeof(id))||
       !query_param(req,"value0",off,sizeof(off))||
       !query_param(req,"value1",on,sizeof(on))){
        httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"IDs de toggle manquants");
        return ESP_FAIL;
    }
    long local=strtol(id,NULL,10);
    if(local<1||local>=4096||
       !put_remote_binding(local,off)||
       !put_remote_binding(local+1,on)){
        httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Mapping toggle impossible");
        return ESP_FAIL;
    }
    return httpd_resp_sendstr(req,"OK");
}
static esp_err_t set_jeedom_init_id(httpd_req_t *req){
    if(!require_key(req))return ESP_FAIL;
    char id[32];
    if(!query_param(req,"value",id,sizeof(id))){
        httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Init ID absent");
        return ESP_FAIL;
    }
    nvs_handle_t nv;
    esp_err_t err=nvs_open("mira_idf_jeed",NVS_READWRITE,&nv);
    if(err==ESP_OK) {
        err=nvs_set_str(nv,"Jeedom_Init_Cmd",id);
        if(err==ESP_OK)err=nvs_commit(nv);
        nvs_close(nv);
    }
    if(err!=ESP_OK) {
        httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Init ID non enregistre");
        return ESP_FAIL;
    }
    request_binding_sync(); // now fetch authoritative full index from plugin
    return httpd_resp_sendstr(req,"OK");
}


// Outbound HTTP Jeedom commands are deliberately separate from passive States.
// The UI only enqueues a small fixed-size message; this worker resolves the
// historical local ID and its configured Jeedom ID in a different FreeRTOS task.
typedef struct {
    char name[96];
    char type[24];
    char value[160];
} mira_action_msg_t;
static QueueHandle_t action_queue;


static uint32_t ui_config_hash(const char *s) {
    uint32_t hash=2166136261u;
    for(const unsigned char *p=(const unsigned char*)s;*p;p++){
        hash^=*p;
        hash*=16777619u;
    }
    return hash;
}

static bool decimal_id(const char *str) {
    if(!str||!*str||strlen(str)>15)return false;
    for(const char *p=str;*p;p++)if(*p<'0'||*p>'9')return false;
    return true;
}
bool mira_jeedom_queue_local_action(const char *name,const char *type,const char *value) {
    if(!jeedom_mode_enabled||!action_queue||!name||!type||!value||!*name ||
       strlen(name)>=96||strlen(type)>=24||strlen(value)>=160)return false;
    if(strcmp(type,"toggle")&&strcmp(type,"button")&&strcmp(type,"multiaction")&&
       strcmp(type,"range")&&strcmp(type,"roundrange")&&strcmp(type,"thermostat")&&
       strcmp(type,"list")&&strcmp(type,"listbutton")&&strcmp(type,"color"))return false;
    mira_action_msg_t msg={0};
    snprintf(msg.name,sizeof(msg.name),"%s",name);
    snprintf(msg.type,sizeof(msg.type),"%s",type);
    snprintf(msg.value,sizeof(msg.value),"%s",value);
    return xQueueSend(action_queue,&msg,0)==pdTRUE;
}
/* MiraPanelServer::GenericCmd fallback when Jeedom is disabled and
 * MQTT did not accept a user action. Use the SAME background action worker
 * as Jeedom HTTP; do not execute network requests on the LVGL task.
 */
bool mira_generic_queue_local_action(const char *name,const char *type,
                                     const char *value){
    if(jeedom_mode_enabled||!action_queue||!name||!*name||!type||!value||
       strlen(name)>=96||strlen(type)>=24||strlen(value)>=160)return false;
    mira_action_msg_t msg={0};
    snprintf(msg.name,sizeof(msg.name),"%s",name);
    snprintf(msg.type,sizeof(msg.type),"__generic");
    snprintf(msg.value,sizeof(msg.value),"%s",value);
    return xQueueSend(action_queue,&msg,0)==pdTRUE;
}

static int resolve_local_action(cJSON *index,const mira_action_msg_t *msg,
                                char *value,size_t value_size) {
    cJSON *entries=cJSON_GetObjectItemCaseSensitive(index,"actions"),*item;
    cJSON_ArrayForEach(item,entries) {
        const char *kind=jstr(item,"type","");
        const char *name=jstr(item,"nom","");
        if(!strcmp(msg->type,"toggle")&&!strcmp(kind,"toggle")&&!strcmp(name,msg->name))
            return !strcmp(msg->value,"1")?jint(item,"id1",-1):jint(item,"id0",-1);
        if((!strcmp(msg->type,"list")||!strcmp(msg->type,"listbutton"))&&
           !strcmp(kind,"list") &&
           !strcmp(jstr(item,"nomliste",""),msg->name)&&
           !strcmp(name,msg->value)) {
            const int id=jint(item,"id",-1);
            snprintf(value,value_size,"%d",id); // Historical Jeedom list protocol.
            return id;
        }
        const bool range=!strcmp(msg->type,"range")||
                         !strcmp(msg->type,"roundrange")||
                         !strcmp(msg->type,"thermostat");
        char expected[128];
        if(!strcmp(msg->type,"thermostat"))
            snprintf(expected,sizeof(expected),"%.95s__consigne",msg->name);
        else snprintf(expected,sizeof(expected),"%s",msg->name);
        if(range&&!strcmp(kind,"range")&&!strcmp(name,expected)) {
            snprintf(value,value_size,"%s",msg->value);
            return jint(item,"id",-1);
        }
        if(!strcmp(msg->type,"color")&&!strcmp(kind,"color")&&!strcmp(name,msg->name)){
            unsigned r=0,g=0,b=0;
            char extra=0;
            if(sscanf(msg->value,"%u,%u,%u %c",&r,&g,&b,&extra)!=3 ||
               r>255u||g>255u||b>255u)return -1;
            snprintf(value,value_size,"%u,%u,%u",r,g,b);
            return jint(item,"id",-1);
        }
        if(!strcmp(msg->type,"button")&&!strcmp(kind,"button")&&!strcmp(name,msg->name))
            return jint(item,"id",-1);
        // Mira beta publishes four distinct Jeedom button IDs for a
        // multiaction widget. Resolve the gesture to its historical binding;
        // unknown gestures never fall back silently to a single click.
        if(!strcmp(msg->type,"multiaction")&&!strcmp(kind,"button")) {
            const char *prefix=NULL;
            if(!strcmp(msg->value,"single")||!strcmp(msg->value,"1"))
                prefix="Multi_Clic_";
            else if(!strcmp(msg->value,"double"))prefix="Multi_Double_";
            else if(!strcmp(msg->value,"triple"))prefix="Multi_Triple_";
            else if(!strcmp(msg->value,"long"))prefix="Multi_Long_";
            if(!prefix)return -1;
            snprintf(expected,sizeof(expected),"%s%.95s",prefix,msg->name);
            if(!strcmp(name,expected))return jint(item,"id",-1);
        }
    }
    return -1;  // Fail closed if legacy bindings cannot be reconstructed.
}
static bool urlencode_to(char *dst,size_t capacity,const char *src) {
    const char *hex="0123456789ABCDEF";
    size_t n=0;
    for(const unsigned char *p=(const unsigned char*)src;*p;p++) {
        unsigned char c=*p;
        bool safe=(c>='a'&&c<='z')||(c>='A'&&c<='Z')||
                  (c>='0'&&c<='9')||c=='-'||c=='_'||c=='.'||c=='~';
        size_t required=safe?1:3;
        if(n+required>=capacity)return false;
        if(safe)dst[n++]=(char)c;
        else{dst[n++]='%';dst[n++]=hex[c>>4];dst[n++]=hex[c&15];}
    }
    dst[n]=0;
    return true;
}
static bool nvs_read_string(nvs_handle_t nvs,const char *key,char *out,size_t size) {
    size_t len=size;
    return nvs_get_str(nvs,key,out,&len)==ESP_OK && out[0];
}
static void send_jeedom_action(const mira_action_msg_t *msg) {
    // No network action before the screen plugin provisions *all* required
    // IDs; a fresh /programui invalidates these mappings (see below).
    nvs_handle_t cfg;
    if(nvs_open("mira_idf_jeed",NVS_READONLY,&cfg)!=ESP_OK)return;
    char enabled[8]={0},base[192]={0},secret[256]={0};
    // Keep legacy HTTP as default; a missing mode flag must not silently
    // redirect actions to MQTT. Network requests still require a configured
    // Jeedom address and API key.
    const bool mode_saved=nvs_read_string(cfg,"Jeedom",enabled,sizeof(enabled));
    bool ready=(!mode_saved||!strcmp(enabled,"1"))&&
               nvs_read_string(cfg,"Jeedom_IP",base,sizeof(base))&&
               nvs_read_string(cfg,"Jeedom_Key_mira",secret,sizeof(secret));
    nvs_close(cfg);
    if(!ready)return;
    if(strncmp(base,"http://",7)!=0||strchr(base,'?')||strchr(base,'#')||
       strchr(base,'@')||strchr(base,'\r')||strchr(base,'\n'))return;
    size_t baselen=strlen(base);
    while(baselen>7&&base[baselen-1]=='/')base[--baselen]=0;
    if(baselen<=7)return;

    char *raw=read_config(NULL);
    if(!raw)return;
    const uint32_t current_hash=ui_config_hash(raw);
    cJSON *index=index_from_config(raw);
    free(raw);
    if(!index)return;
    char value[160]={0};
    int local_id=resolve_local_action(index,msg,value,sizeof(value));
    cJSON_Delete(index);
    if(local_id<1||local_id>4096)return;

    nvs_handle_t map;
    if(nvs_open("mira_idf_map",NVS_READONLY,&map)!=ESP_OK)return;
    char local_key[14],binding[48]={0};
    snprintf(local_key,sizeof(local_key),"cmd%d",local_id);
    bool mapped=nvs_read_string(map,local_key,binding,sizeof(binding));
    nvs_close(map);
    char expected_prefix[12];
    snprintf(expected_prefix,sizeof(expected_prefix),"%08" PRIx32 ":",current_hash);
    if(!mapped||strncmp(binding,expected_prefix,9)!=0)return;
    const char *remote_id=binding+9;
    if(!decimal_id(remote_id))return;

    char encoded_secret[768],encoded_value[480];
    if(!urlencode_to(encoded_secret,sizeof(encoded_secret),secret))return;
    if(value[0]&&!urlencode_to(encoded_value,sizeof(encoded_value),value))return;
    char uri[1600];
    int n=snprintf(uri,sizeof(uri),
                   "%s/core/api/jeeApi.php?plugin=mira&type=event&apikey=%s&id=%s",
                   base,encoded_secret,remote_id);
    if(n<1||n>=(int)sizeof(uri))return;
    if(value[0]) {
        size_t offset=(size_t)n;
        n=snprintf(uri+offset,sizeof(uri)-offset,"&value=%s",encoded_value);
        if(n<0||n>=(int)(sizeof(uri)-offset))return;
    }
    esp_http_client_config_t http={
        .url=uri,
        .timeout_ms=3000,
        .disable_auto_redirect=true,
    };
    esp_http_client_handle_t client=esp_http_client_init(&http);
    if(!client)return;
    esp_err_t result=esp_http_client_perform(client);
    int status=esp_http_client_get_status_code(client);
    if(result!=ESP_OK||status<200||status>=300)
        ESP_LOGW(TAG,"Jeedom action local=%d remote=%s failed, HTTP %d (%s)",
                 local_id,remote_id,status,esp_err_to_name(result));
    else ESP_LOGI(TAG,"Jeedom action local=%d delivered (HTTP %d)",local_id,status);
    esp_http_client_cleanup(client);
}

// The installed Jeedom plugin exposes its already-built ID association table
// through jeeApi.php?plugin=mira&type=event&id=<init_cmd>&value=json.
// Fetch ONLY this read-only JSON, verify every generated local action signature,
// then atomically replace the NVS map. Never guess a command ID.
typedef struct {
    char *buf;
    size_t used,cap;
    bool overflow;
} mira_http_capture_t;
static esp_err_t capture_http_json(esp_http_client_event_t *event) {
    mira_http_capture_t *c=(mira_http_capture_t*)event->user_data;
    if(event->event_id==HTTP_EVENT_ON_DATA && c && event->data_len>0){
        const size_t len=(size_t)event->data_len;
        if(len>=c->cap || c->used>=c->cap-len){
            c->overflow=true;
            return ESP_FAIL;
        }
        memcpy(c->buf+c->used,event->data,len);
        c->used+=len;
        c->buf[c->used]=0;
    }
    return ESP_OK;
}
static bool same_index_structure(const cJSON *local,const cJSON *remote){
    const cJSON *a=cJSON_GetObjectItemCaseSensitive(local,"actions");
    const cJSON *b=cJSON_GetObjectItemCaseSensitive(remote,"actions");
    if(!cJSON_IsArray(a)||!cJSON_IsArray(b))return false;
    const int count=cJSON_GetArraySize(a);
    if(count!=cJSON_GetArraySize(b)||count>4096)return false;
    for(int i=0;i<count;i++){
        cJSON *l=cJSON_GetArrayItem(a,i),*r=cJSON_GetArrayItem(b,i);
        if(strcmp(jstr(l,"type",""),jstr(r,"type",""))!=0 ||
           strcmp(jstr(l,"nom",""),jstr(r,"nom",""))!=0 ||
           strcmp(jstr(l,"nomliste",""),jstr(r,"nomliste",""))!=0)return false;
        if(!strcmp(jstr(l,"type",""),"toggle")){
            if(jint(l,"id0",-1)!=jint(r,"id0",-2)||
               jint(l,"id1",-1)!=jint(r,"id1",-2))return false;
        }else if(jint(l,"id",-1)!=jint(r,"id",-2))return false;
    }
    return true;
}
static esp_err_t save_remote_mapping(nvs_handle_t nvs,int local,int remote,uint32_t hash){
    if(local<1||local>4096||remote<1)return ESP_ERR_INVALID_ARG;
    char key[14],value[48];
    snprintf(key,sizeof(key),"cmd%d",local);
    snprintf(value,sizeof(value),"%08" PRIx32 ":%d",hash,remote);
    return nvs_set_str(nvs,key,value);
}
static void sync_jeedom_bindings(void){
    nvs_handle_t cfg;
    if(nvs_open("mira_idf_jeed",NVS_READONLY,&cfg)!=ESP_OK)return;
    char on[8]={0},base[192]={0},secret[256]={0},init[24]={0};
    const bool saved_mode=nvs_read_string(cfg,"Jeedom",on,sizeof(on));
    const bool ready=(!saved_mode||!strcmp(on,"1"))&&
                     nvs_read_string(cfg,"Jeedom_IP",base,sizeof(base))&&
                     nvs_read_string(cfg,"Jeedom_Key_mira",secret,sizeof(secret))&&
                     nvs_read_string(cfg,"Jeedom_Init_Cmd",init,sizeof(init));
    nvs_close(cfg);
    if(!ready||!decimal_id(init)||strncmp(base,"http://",7)||
       strchr(base,'?')||strchr(base,'#')||strchr(base,'@')||
       strchr(base,'\r')||strchr(base,'\n'))return;
    size_t len=strlen(base);
    while(len>7 && base[len-1]=='/')base[--len]=0;
    char token[768];
    if(len<=7||!urlencode_to(token,sizeof(token),secret))return;

    char uri[1100];
    int n=snprintf(uri,sizeof(uri),
        "%s/core/api/jeeApi.php?plugin=mira&type=event&apikey=%s&id=%s&value=json",
        base,token,init);
    if(n<0||n>=(int)sizeof(uri))return;

    mira_http_capture_t reply={.cap=65536};
    reply.buf=malloc(reply.cap);
    if(!reply.buf){ESP_LOGW(TAG,"No memory for Jeedom index fetch");return;}
    reply.buf[0]=0;
    esp_http_client_config_t http={
        .url=uri,.timeout_ms=5000,.disable_auto_redirect=true,
        .event_handler=capture_http_json,.user_data=&reply
    };
    esp_http_client_handle_t client=esp_http_client_init(&http);
    if(!client){free(reply.buf);return;}
    esp_err_t result=esp_http_client_perform(client);
    const int status=esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    if(result!=ESP_OK||status!=200||reply.overflow||reply.used<2){
        free(reply.buf);
        ESP_LOGW(TAG,"Jeedom mapping not ready (HTTP %d)",status);
        return;
    }
    cJSON *remote=cJSON_ParseWithLength(reply.buf,reply.used);
    free(reply.buf);
    if(!remote)return;
    char *config=read_config(NULL);
    if(!config){cJSON_Delete(remote);return;}
    const uint32_t hash=ui_config_hash(config);
    cJSON *local=index_from_config(config);
    free(config);
    if(!local||!same_index_structure(local,remote)){
        ESP_LOGW(TAG,"Jeedom index differs from current LVGL program; no action mapping activated");
        if(local)cJSON_Delete(local);
        cJSON_Delete(remote);
        return;
    }
    nvs_handle_t map;
    esp_err_t err=nvs_open("mira_idf_map",NVS_READWRITE,&map);
    if(err!=ESP_OK){cJSON_Delete(local);cJSON_Delete(remote);return;}
    err=nvs_erase_all(map);
    cJSON *arr=cJSON_GetObjectItemCaseSensitive(remote,"actions"),*entry;
    int bound=0;
    if(err==ESP_OK) {
        cJSON_ArrayForEach(entry,arr){
            if(!strcmp(jstr(entry,"type",""),"toggle")){
                err=save_remote_mapping(map,jint(entry,"id0",-1),
                                        jint(entry,"jeedom0",-1),hash);
                if(err==ESP_OK){
                    bound++;
                    err=save_remote_mapping(map,jint(entry,"id1",-1),
                                            jint(entry,"jeedom1",-1),hash);
                    if(err==ESP_OK)bound++;
                }
            }else {
                err=save_remote_mapping(map,jint(entry,"id",-1),
                                        jint(entry,"jeedom",-1),hash);
                if(err==ESP_OK)bound++;
            }
            if(err!=ESP_OK)break;
        }
    }
    if(err==ESP_OK)err=nvs_commit(map);
    nvs_close(map);
    cJSON_Delete(local);
    cJSON_Delete(remote);
    if(err==ESP_OK)ESP_LOGI(TAG,"Jeedom full index synchronized: %d bounded command IDs",bound);
    else ESP_LOGE(TAG,"Jeedom mapping validation/storage incomplete: %s",
                  esp_err_to_name(err));
}

static esp_err_t jeedom_status_get(httpd_req_t *req) {
    if(!require_key(req))return ESP_FAIL;
    char *raw=read_config(NULL);
    uint32_t hash=raw?ui_config_hash(raw):0;
    cJSON *index=raw?index_from_config(raw):NULL;
    free(raw);
    int total=0,valid=0;
    const cJSON *items=index?cJSON_GetObjectItemCaseSensitive(index,"actions"):NULL,*item;
    nvs_handle_t map;
    bool nvs_ok=nvs_open("mira_idf_map",NVS_READONLY,&map)==ESP_OK;
    char expected[12];
    snprintf(expected,sizeof(expected),"%08" PRIx32 ":",hash);
    cJSON_ArrayForEach(item,items) {
        int id[2],amount=1;
        if(!strcmp(jstr((cJSON*)item,"type",""),"toggle")) {
            id[0]=jint((cJSON*)item,"id0",-1);
            id[1]=jint((cJSON*)item,"id1",-1);
            amount=2;
        }else id[0]=jint((cJSON*)item,"id",-1);
        for(int i=0;i<amount;i++) {
            total++;
            if(!nvs_ok||id[i]<1||id[i]>4096)continue;
            char key[14],stored[48]={0};
            snprintf(key,sizeof(key),"cmd%d",id[i]);
            if(nvs_read_string(map,key,stored,sizeof(stored))&&
               !strncmp(stored,expected,9)&&decimal_id(stored+9))valid++;
        }
    }
    if(nvs_ok)nvs_close(map);
    if(index)cJSON_Delete(index);
    nvs_handle_t cfg;
    bool enabled=false; // Match beta GetParamBool("Jeedom") when absent.
    if(nvs_open("mira_idf_jeed",NVS_READONLY,&cfg)==ESP_OK){
        char v[8]={0};
        if(nvs_read_string(cfg,"Jeedom",v,sizeof(v)))enabled=strcmp(v,"0")!=0;
        nvs_close(cfg);
    }
    char body[160];
    snprintf(body,sizeof(body),
        "{\"jeedom_enabled\":%s,\"mapping_valid\":%s,\"ids_valid\":%d,\"ids_expected\":%d}",
        enabled?"true":"false",
        (enabled&&total>0&&valid==total)?"true":"false",valid,total);
    httpd_resp_set_type(req,"application/json");
    return httpd_resp_sendstr(req,body);
}

static void request_binding_sync(void){
    if(!action_queue)return;
    mira_action_msg_t msg={0};
    snprintf(msg.type,sizeof(msg.type),"__sync");
    (void)xQueueSend(action_queue,&msg,0);
}

/* The beta GenericCmd() looks up a widget's configured URL in the
 * programming JSON and replaces #value# with the effective action value.
 * For list selections that is the historical list-command ID; toggle
 * uses the registered On/Off label; color uses URL-encoded #RRGGBB.
 * Never interpret a passive Info as a user-triggered HTTP GET.
 */
static void send_generic_action(const mira_action_msg_t *msg){
    if(!msg||jeedom_mode_enabled)return;
    char *json=read_config(NULL);
    if(!json)return;
    cJSON *root=cJSON_Parse(json);
    if(!root){free(json);return;}
    const cJSON *widgets=cJSON_GetObjectItemCaseSensitive(root,"lvgl");
    const cJSON *item=NULL;
    const char *url=NULL,*type=NULL;
    cJSON_ArrayForEach(item,widgets){
        if(strcmp(jstr((cJSON*)item,"name",""),msg->name))continue;
        url=jstr((cJSON*)item,"url","");
        type=jstr((cJSON*)item,"type","");
        break;
    }
    if(!url||!*url||!type||strlen(url)>1023||
       (strncmp(url,"http://",7)&&strncmp(url,"https://",8))||
       strpbrk(url,"\r\n")){
        cJSON_Delete(root);free(json);return;
    }
    char command_value[160]={0};
    if(!strcmp(type,"toggle")){
        snprintf(command_value,sizeof(command_value),"%s",
                 !strcmp(msg->value,"1")?"On":"Off");
    }else if(!strcmp(type,"list")||!strcmp(type,"listbutton")){
        /* Match IndexAddList() + GenericCmd(): list sends the selected
         * option's numeric local ID rather than its displayed label.
         */
        cJSON *index=index_from_config(json);
        const cJSON *arr=index?cJSON_GetObjectItemCaseSensitive(index,"actions"):NULL,*row;
        cJSON_ArrayForEach(row,arr){
            if(strcmp(jstr((cJSON*)row,"type",""),"list")||
               strcmp(jstr((cJSON*)row,"nomliste",""),msg->name)||
               strcmp(jstr((cJSON*)row,"nom",""),msg->value))continue;
            snprintf(command_value,sizeof(command_value),"%d",
                     jint((cJSON*)row,"id",-1));
            break;
        }
        if(index)cJSON_Delete(index);
        if(!command_value[0]){cJSON_Delete(root);free(json);return;}
    }else if(!strcmp(type,"color")){
        unsigned r=0,g=0,b=0;
        bool valid=false;
        if(strlen(msg->value)==7 && msg->value[0]=='#'){
            valid=sscanf(msg->value+1,"%2x%2x%2x",&r,&g,&b)==3;
        }else{
            char extra=0;
            valid=sscanf(msg->value,"%u,%u,%u %c",&r,&g,&b,&extra)==3;
        }
        if(!valid||r>255u||g>255u||b>255u){
            cJSON_Delete(root);free(json);return;
        }
        snprintf(command_value,sizeof(command_value),"%%23%02X%02X%02X",r,g,b);
    }else if(!strcmp(type,"range")||!strcmp(type,"roundrange")){
        snprintf(command_value,sizeof(command_value),"%s",msg->value);
    }else if(strcmp(type,"button")){
        cJSON_Delete(root);free(json);return;
    }
    /* Escaped #value# appears literally in the beta user-defined URLs.
     * Preserve all occurrences, but reject expansions beyond a fixed bound.
     */
    char expanded[2048]={0};
    size_t used=0;
    for(const char *p=url;*p;){
        const char *chunk=p;
        size_t len=0;
        if(command_value[0]&&!strncmp(p,"#value#",7)){
            chunk=command_value;
            len=strlen(command_value);
            p+=7;
        }else{
            len=1;p++;
        }
        if(used+len>=sizeof(expanded)){cJSON_Delete(root);free(json);return;}
        memcpy(expanded+used,chunk,len);
        used+=len;
    }
    expanded[used]=0;
    cJSON_Delete(root);free(json);
    esp_http_client_config_t cfg={
        .url=expanded,.timeout_ms=5000,.disable_auto_redirect=true,
        .crt_bundle_attach=esp_crt_bundle_attach
    };
    esp_http_client_handle_t client=esp_http_client_init(&cfg);
    if(!client)return;
    const esp_err_t result=esp_http_client_perform(client);
    const int status=esp_http_client_get_status_code(client);
    if(result!=ESP_OK||status<200||status>=300)
        ESP_LOGW(TAG,"Generic Mira action failed (%s, HTTP %d)",
                 esp_err_to_name(result),status);
    esp_http_client_cleanup(client);
}

static void jeedom_action_worker(void *unused) {
    (void)unused;
    mira_action_msg_t action;
    for(;;) {
        if(xQueueReceive(action_queue,&action,portMAX_DELAY)==pdTRUE){
            if(!strcmp(action.type,"__sync"))sync_jeedom_bindings();
            else if(!strcmp(action.type,"__generic"))send_generic_action(&action);
            else send_jeedom_action(&action);
        }
    }
}
// The beta uses /reseau.json -> ap[0].passcrypt as the screen/plugin
// authentication key. Reuse it when decipherable, without ever replacing a
// key already provisioned on ESP-IDF or rewriting the original network file.
static void import_legacy_screen_key(void){
    FILE *fp=fopen("/spiffs/reseau.json","rb");
    if(!fp)return;
    if(fseek(fp,0,SEEK_END)!=0){fclose(fp);return;}
    long len=ftell(fp);
    if(len<2||len>4096){fclose(fp);return;}
    rewind(fp);
    char *buffer=malloc((size_t)len+1);
    if(!buffer){fclose(fp);return;}
    const size_t got=fread(buffer,1,(size_t)len,fp);
    fclose(fp);
    if(got!=(size_t)len){free(buffer);return;}
    buffer[got]=0;
    cJSON *root=cJSON_ParseWithLength(buffer,got);
    free(buffer);
    const cJSON *ap=root?cJSON_GetObjectItemCaseSensitive(root,"ap"):NULL;
    const cJSON *entry=cJSON_IsArray(ap)?cJSON_GetArrayItem(ap,0):NULL;
    const char *encrypted=jstr((cJSON*)entry,"passcrypt","");
    char plaintext[256]={0};
    bool valid=encrypted[0]&&mira_legacy_decrypt(encrypted,plaintext,sizeof(plaintext));
    if(valid&&strlen(plaintext)<=MIRA_KEY_MAX){
        nvs_handle_t nv;
        if(nvs_open("mira_idf_auth",NVS_READWRITE,&nv)==ESP_OK){
            size_t existing=0;
            if(nvs_get_str(nv,"jeedom_key",NULL,&existing)==ESP_ERR_NVS_NOT_FOUND &&
               nvs_set_str(nv,"jeedom_key",plaintext)==ESP_OK &&
               nvs_commit(nv)==ESP_OK)
                ESP_LOGI(TAG,"Legacy Mira screen authentication imported");
            nvs_close(nv);
        }
    }
    memset(plaintext,0,sizeof(plaintext));
    if(root)cJSON_Delete(root);
}

// Only migrate sleep settings with identical meaning and limits in IDF.
// Do not change calibrated 4.3C day/night brightness or panel timings here.
static void import_legacy_sleep_settings(const cJSON *params){
    nvs_handle_t nv;
    if(nvs_open("mira_idf_ui",NVS_READWRITE,&nv)!=ESP_OK)return;
    int changed=0;
    const cJSON *item;
    cJSON_ArrayForEach(item,params){
        const char *key=jstr((cJSON*)item,"param","");
        const char *value=jstr((cJSON*)item,"value","");
        if(!value[0]||strlen(value)>8)continue;
        char *end=NULL;
        long number=strtol(value,&end,10);
        if(!end||*end)continue;
        if(!strcmp(key,"DISPLAY_Delai")&&number>=0&&number<=86400){
            uint32_t old=0;
            if(nvs_get_u32(nv,"sleep_delay",&old)==ESP_ERR_NVS_NOT_FOUND &&
               nvs_set_u32(nv,"sleep_delay",(uint32_t)number)==ESP_OK)changed++;
        }else if(!strcmp(key,"DISPLAY_Veille")&&number>=0&&number<=100){
            uint8_t old=0;
            if(nvs_get_u8(nv,"sleep_light",&old)==ESP_ERR_NVS_NOT_FOUND &&
               nvs_set_u8(nv,"sleep_light",(uint8_t)number)==ESP_OK)changed++;
        }else if(!strcmp(key,"DISPLAY_HorlogeVeille")&&(number==0||number==1)){
            uint8_t old=0;
            if(nvs_get_u8(nv,"sleep_clock",&old)==ESP_ERR_NVS_NOT_FOUND &&
               nvs_set_u8(nv,"sleep_clock",(uint8_t)number)==ESP_OK)changed++;
        }else if(!strcmp(key,"DISPLAY_Jour")&&number>=1&&number<=100){
            uint8_t old=0;
            if(nvs_get_u8(nv,"day_pct",&old)==ESP_ERR_NVS_NOT_FOUND &&
               nvs_set_u8(nv,"day_pct",(uint8_t)number)==ESP_OK)changed++;
        }else if(!strcmp(key,"DISPLAY_Nuit")&&number>=1&&number<=100){
            uint8_t old=0;
            if(nvs_get_u8(nv,"night_pct",&old)==ESP_ERR_NVS_NOT_FOUND &&
               nvs_set_u8(nv,"night_pct",(uint8_t)number)==ESP_OK)changed++;
        }else if(!strcmp(key,"DISPLAY_NuitAuto")&&(number==0||number==1)){
            uint8_t old=0;
            if(nvs_get_u8(nv,"night_auto",&old)==ESP_ERR_NVS_NOT_FOUND &&
               nvs_set_u8(nv,"night_auto",(uint8_t)number)==ESP_OK)changed++;
        }
    }
    // Import time strings separately: they are HH:MM, not integers.
    cJSON_ArrayForEach(item,params){
        const char *key=jstr((cJSON*)item,"param","");
        const char *value=jstr((cJSON*)item,"value","");
        const char *target=!strcmp(key,"DISPLAY_DebutNuit")?"night_start":
                           !strcmp(key,"DISPLAY_FinNuit")?"night_end":NULL;
        if(!target||strlen(value)!=5||value[2]!=':')continue;
        int hours=-1,minutes=-1;
        if(sscanf(value,"%2d:%2d",&hours,&minutes)!=2||
           hours<0||hours>23||minutes<0||minutes>59)continue;
        char normalized[6];
        snprintf(normalized,sizeof(normalized),"%02d:%02d",hours,minutes);
        char current[8]={0};size_t size=sizeof(current);
        if(nvs_get_str(nv,target,current,&size)==ESP_ERR_NVS_NOT_FOUND &&
           nvs_set_str(nv,target,normalized)==ESP_OK)changed++;
    }
    if(changed&&nvs_commit(nv)==ESP_OK)
        ESP_LOGI(TAG,"Legacy Mira sleep settings imported: %d",changed);
    nvs_close(nv);
}

// Import the previous Arduino firmware's /params.json only when there is no
// newer ESP-IDF setting for a given key. Never modify the legacy SPIFFS file.
// Mira beta uses {"params":[{"param":"Jeedom_IP","value":"...","type":"text"}]}.
// "crypted-text" is AES data, NOT a usable plaintext Jeedom API key.
static void import_legacy_http_settings(void) {
    FILE *fp=fopen("/spiffs/params.json","rb");
    if(!fp)return;
    if(fseek(fp,0,SEEK_END)!=0){fclose(fp);return;}
    const long size=ftell(fp);
    if(size<2||size>32768){fclose(fp);return;}
    rewind(fp);
    char *contents=malloc((size_t)size+1);
    if(!contents){fclose(fp);return;}
    const size_t got=fread(contents,1,(size_t)size,fp);
    fclose(fp);
    if(got!=(size_t)size){free(contents);return;}
    contents[got]=0;
    cJSON *root=cJSON_ParseWithLength(contents,got);
    free(contents);
    const cJSON *params=root?cJSON_GetObjectItemCaseSensitive(root,"params"):NULL;
    if(!cJSON_IsArray(params)){
        if(root)cJSON_Delete(root);
        ESP_LOGW(TAG,"Legacy params.json is not a valid Mira params array");
        return;
    }
    nvs_handle_t nv;
    if(nvs_open("mira_idf_jeed",NVS_READWRITE,&nv)!=ESP_OK){
        cJSON_Delete(root);
        return;
    }
    int imported=0,encrypted=0,errors=0;
    const cJSON *entry;
    cJSON_ArrayForEach(entry,params){
        const char *key=jstr((cJSON*)entry,"param","");
        const char *value=jstr((cJSON*)entry,"value","");
        const char *type=jstr((cJSON*)entry,"type","");
        if(!supported_jeedom_param(key)||!value[0]||
           !strcmp(value,"null")||strlen(value)>=256)continue;
        // The old beta encrypts some fields; only validated plaintext
        // from the original AES decoder may be imported.
        char decrypted[256]={0};
        const bool legacy_secret=!strcmp(type,"crypted-text");
        if(legacy_secret){
            encrypted++;
            if(!mira_legacy_decrypt(value,decrypted,sizeof(decrypted)))continue;
            value=decrypted;
        }
        if(!strcmp(key,"Jeedom")&&strcmp(value,"0")&&strcmp(value,"1")){
            memset(decrypted,0,sizeof(decrypted));
            continue;
        }
        size_t existing=0;
        if(nvs_get_str(nv,key,NULL,&existing)==ESP_ERR_NVS_NOT_FOUND){
            if(nvs_set_str(nv,key,value)==ESP_OK)imported++;
            else errors++;
        }
        memset(decrypted,0,sizeof(decrypted));
    }
    if(imported&&nvs_commit(nv)!=ESP_OK)errors++;
    nvs_close(nv);
    import_legacy_sleep_settings(params);
    cJSON_Delete(root);
    ESP_LOGI(TAG,"Legacy HTTP parameters: imported=%d, encrypted examined=%d, errors=%d",
             imported,encrypted,errors);
}
esp_err_t mira_jeedom_action_init(void) {
    if(action_queue)return ESP_OK;
    import_legacy_screen_key();
    import_legacy_http_settings();
    nvs_handle_t settings;
    if(nvs_open("mira_idf_jeed",NVS_READONLY,&settings)==ESP_OK){
        char active[8]={0};size_t len=sizeof(active);
        if(nvs_get_str(settings,"Jeedom",active,&len)==ESP_OK)
            jeedom_mode_enabled=strcmp(active,"0")!=0;
        nvs_close(settings);
    }
    action_queue=xQueueCreate(12,sizeof(mira_action_msg_t));
    if(!action_queue)return ESP_ERR_NO_MEM;
    if(xTaskCreate(jeedom_action_worker,"mira_jeedom_http",7168,NULL,3,NULL)!=pdPASS) {
        vQueueDelete(action_queue);
        action_queue=NULL;
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG,"Jeedom HTTP event worker ready (no event during passive state updates)");
    request_binding_sync(); // survives reboot; no actions emitted during binding
    return ESP_OK;
}


// Legacy plugin POST /camera/config (form: key,name,url). The same namespace
// and FNV1a key are used by the older Arduino build, preserving stored URLs.
// Remote plugin must authenticate with the screen key; no URLs in logs.
static esp_err_t camera_config_post(httpd_req_t *req){
    if(req->content_len<10||req->content_len>3000){
        httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Camera form size invalid");
        return ESP_FAIL;
    }
    size_t bytes=req->content_len;
    char *body=malloc(bytes+1);
    if(!body){httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Memory unavailable");return ESP_FAIL;}
    if(!read_exact_request(req,body,bytes)){
        free(body);
        httpd_resp_send_err(req,HTTPD_408_REQ_TIMEOUT,"Incomplete camera form");
        return ESP_FAIL;
    }
    char *secret=NULL,*name=NULL,*url=NULL;
    for(char *segment=body;segment&&*segment;){
        char *next=strchr(segment,'&');
        if(next)*next++=0;
        char *sep=strchr(segment,'=');
        if(sep){
            *sep++=0;
            if(!strcmp(segment,"key"))secret=sep;
            else if(!strcmp(segment,"name"))name=sep;
            else if(!strcmp(segment,"url"))url=sep;
        }
        segment=next;
    }
    if(!secret||!name||!url||!decode_url(secret)||!correct_key(secret)){
        free(body);httpd_resp_send_err(req,HTTPD_403_FORBIDDEN,"Invalid Mira camera key");return ESP_FAIL;
    }
    if(!decode_url(name)||!decode_url(url)||strlen(name)<1||
       strlen(name)>100||!mira_camera_url_valid(url)){
        free(body);httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Invalid camera name/URL");return ESP_FAIL;
    }
    char *program=read_config(NULL);
    if(!program){
        free(body);httpd_resp_send_err(req,HTTPD_404_NOT_FOUND,"Mira programming missing");return ESP_FAIL;
    }
    cJSON *root=cJSON_Parse(program);
    free(program);
    bool found=false;
    cJSON *entry;
    const cJSON *arr=root?cJSON_GetObjectItemCaseSensitive(root,"lvgl"):NULL;
    cJSON_ArrayForEach(entry,arr){
        if(!strcmp(jstr(entry,"type",""),"camera")&&
           !strcmp(jstr(entry,"name",""),name)){
            found=true;break;
        }
    }
    if(root)cJSON_Delete(root);
    if(!found){
        free(body);httpd_resp_send_err(req,HTTPD_404_NOT_FOUND,"Unknown camera widget");return ESP_FAIL;
    }
    const bool saved=mira_camera_save_url(name,url);
    free(body);
    if(!saved){httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"NVS camera save failed");return ESP_FAIL;}
    httpd_resp_set_type(req,"text/plain");
    return httpd_resp_sendstr(req,"OK");
}

// Original beta navigation API: exact HTTP method, form field names,
// NVS namespace and payload shape. LVGL polls miraui on its own thread.
static bool nav_post_form(httpd_req_t *req,char *value,size_t capacity){
    if(req->content_len<2||req->content_len>900)return false;
    char *form=malloc((size_t)req->content_len+1);
    if(!form)return false;
    const size_t len=(size_t)req->content_len;
    bool ok=read_exact_request(req,form,len);
    char key[112]={0};
    if(ok){
        char raw[112]={0};
        ok=httpd_query_key_value(form,"key",raw,sizeof(raw))==ESP_OK &&
           decode_url(raw) && correct_key(raw);
        if(ok)memcpy(key,raw,strlen(raw)+1);
    }
    if(ok){
        // Favorites POST contains 'json'; page-mask POST contains 'mask'.
        const char *field=strstr(req->uri,"/navfavorites")?"json":"mask";
        char encoded[512]={0};
        ok=httpd_query_key_value(form,field,encoded,sizeof(encoded))==ESP_OK &&
           decode_url(encoded) && strlen(encoded)<capacity;
        if(ok)memcpy(value,encoded,strlen(encoded)+1);
    }
    memset(key,0,sizeof(key));
    memset(form,0,len);
    free(form);
    return ok;
}
static esp_err_t nav_pages_get(httpd_req_t *req){
    if(!require_key(req))return ESP_FAIL;
    uint8_t mask=0x3f;
    nvs_handle_t nv;
    if(nvs_open("miraui",NVS_READONLY,&nv)==ESP_OK){
        (void)nvs_get_u8(nv,"pageMask",&mask);
        nvs_close(nv);
    }
    char response[40];
    snprintf(response,sizeof(response),"{\"mask\":%u}",(unsigned)((mask&0x3f)|1));
    httpd_resp_set_type(req,"application/json");
    return httpd_resp_sendstr(req,response);
}
static esp_err_t nav_pages_post(httpd_req_t *req){
    char value[80]={0};
    if(!nav_post_form(req,value,sizeof(value))){
        httpd_resp_send_err(req,HTTPD_403_FORBIDDEN,"Cle ou formulaire invalide");
        return ESP_FAIL;
    }
    if(!value[0]){
        httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Masque absent");
        return ESP_FAIL;
    }
    for(const char *p=value;*p;p++)if(!isdigit((unsigned char)*p)){
        httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Masque invalide");
        return ESP_FAIL;
    }
    long mask=strtol(value,NULL,10);
    if(mask<1||mask>63||!(mask&1)){
        httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Accueil obligatoire");
        return ESP_FAIL;
    }
    nvs_handle_t nv;
    esp_err_t err=nvs_open("miraui",NVS_READWRITE,&nv);
    if(err==ESP_OK){
        err=nvs_set_u8(nv,"pageMask",(uint8_t)mask);
        if(err==ESP_OK)err=nvs_commit(nv);
        nvs_close(nv);
    }
    if(err!=ESP_OK){
        httpd_resp_set_status(req,"503 Service Unavailable");
        httpd_resp_sendstr(req,"Ecriture masque impossible");
        return ESP_FAIL;
    }
    return httpd_resp_sendstr(req,"OK");
}
static esp_err_t nav_favorites_get(httpd_req_t *req){
    if(!require_key(req))return ESP_FAIL;
    uint8_t fav[3]={0,1,2};
    nvs_handle_t nv;
    if(nvs_open("miraui",NVS_READONLY,&nv)==ESP_OK){
        for(uint8_t i=0;i<3;i++){
            char key[8];
            snprintf(key,sizeof(key),"fav%u",(unsigned)i);
            (void)nvs_get_u8(nv,key,&fav[i]);
        }
        nvs_close(nv);
    }
    char response[96];
    snprintf(response,sizeof(response),"{\"favorites\":[%u,%u,%u]}",
             (unsigned)fav[0],(unsigned)fav[1],(unsigned)fav[2]);
    httpd_resp_set_type(req,"application/json");
    return httpd_resp_sendstr(req,response);
}
static esp_err_t nav_favorites_post(httpd_req_t *req){
    char json[480]={0};
    if(!nav_post_form(req,json,sizeof(json))){
        httpd_resp_send_err(req,HTTPD_403_FORBIDDEN,"Cle ou formulaire invalide");
        return ESP_FAIL;
    }
    cJSON *doc=cJSON_Parse(json);
    const cJSON *items=doc?cJSON_GetObjectItemCaseSensitive(doc,"favorites"):NULL;
    bool valid=cJSON_IsArray(items)&&cJSON_GetArraySize(items)==3;
    uint8_t fav[3]={0};
    if(valid)for(int i=0;i<3;i++){
        const cJSON *v=cJSON_GetArrayItem(items,i);
        if(!cJSON_IsNumber(v)||v->valuedouble!=v->valueint||
           (v->valueint!=255&&(v->valueint<0||v->valueint>5))){
            valid=false;break;
        }
        fav[i]=(uint8_t)v->valueint;
        for(int j=0;j<i;j++)if(fav[i]!=255&&fav[i]==fav[j])valid=false;
    }
    if(doc)cJSON_Delete(doc);
    if(!valid){
        httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Favoris invalides ou dupliques");
        return ESP_FAIL;
    }
    nvs_handle_t nv;
    esp_err_t err=nvs_open("miraui",NVS_READWRITE,&nv);
    if(err==ESP_OK){
        for(uint8_t i=0;i<3&&err==ESP_OK;i++){
            char key[8];
            snprintf(key,sizeof(key),"fav%u",(unsigned)i);
            err=nvs_set_u8(nv,key,fav[i]);
        }
        if(err==ESP_OK)err=nvs_commit(nv);
        nvs_close(nv);
    }
    if(err!=ESP_OK){
        httpd_resp_set_status(req,"503 Service Unavailable");
        httpd_resp_sendstr(req,"Ecriture favoris impossible");
        return ESP_FAIL;
    }
    return httpd_resp_sendstr(req,"OK");
}
// Original beta 0.7.37 diagnostic contract, with real heap/nav counters.
static esp_err_t mira_health_get(httpd_req_t *req){
    wifi_ap_record_t ap={0};
    const bool connected=esp_wifi_sta_get_ap_info(&ap)==ESP_OK;
    uint32_t count=0,last_heap=0,lowest_heap=0;
    mira_full_navigation_stats(&count,&last_heap,&lowest_heap);
    mira_full_state_diagnostics_t diagnostic={0};
    mira_full_state_diagnostics(&diagnostic);
    char result[850];
    snprintf(result,sizeof(result),
        "{\"uptime_ms\":%llu,\"wifi\":%d,\"rssi_dbm\":%d,"
        "\"heap_free\":%u,\"heap_min\":%u,\"heap_largest_block\":%u,"
        "\"psram_free\":%u,\"nav_count\":%u,\"nav_last_heap\":%u,"
        "\"nav_lowest_heap\":%u,\"internal_heap_free\":%u,"
        "\"internal_largest_block\":%u,"
        "\"state_cache_used\":%u,\"state_cache_capacity\":%u,"
        "\"state_unchanged\":%u,\"state_cache_evictions\":%u,"
        "\"state_queue_dropped\":%u,\"state_queue_pending\":%u}",
        (unsigned long long)(esp_timer_get_time()/1000),
        connected?3:6,connected?(int)ap.rssi:0,
        (unsigned)esp_get_free_heap_size(),
        (unsigned)esp_get_minimum_free_heap_size(),
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
        (unsigned)count,(unsigned)last_heap,(unsigned)lowest_heap,
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),
        (unsigned)diagnostic.state_cache_used,
        (unsigned)diagnostic.state_cache_capacity,
        (unsigned)diagnostic.state_unchanged,
        (unsigned)diagnostic.state_cache_evictions,
        (unsigned)diagnostic.state_queue_dropped,
        (unsigned)diagnostic.state_queue_pending);
    httpd_resp_set_type(req,"application/json");
    httpd_resp_set_hdr(req,"Cache-Control","no-store");
    return httpd_resp_sendstr(req,result);
}
// Beta /sync/toggle: Jeedom synchronizes a switch passively by command ID.
// Never publish an actuator command in response to this HTTP request.
static esp_err_t sync_toggle_get(httpd_req_t *req){
    if(!require_key(req))return ESP_FAIL;
    char text_id[16]={0};
    if(!query_param(req,"id",text_id,sizeof(text_id))){
        httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"ID manquant");
        return ESP_FAIL;
    }
    size_t len=strlen(text_id);
    if(len==0||len>6){
        httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"ID invalide");
        return ESP_FAIL;
    }
    for(size_t i=0;i<len;i++){
        if(!isdigit((unsigned char)text_id[i])){
            httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"ID invalide");
            return ESP_FAIL;
        }
    }
    long id=strtol(text_id,NULL,10);
    if(id<1||id>4096){
        httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"ID invalide");
        return ESP_FAIL;
    }
    char *raw=read_config(NULL);
    cJSON *index=raw?index_from_config(raw):NULL;
    free(raw);
    if(!index){
        httpd_resp_send_err(req,HTTPD_404_NOT_FOUND,"Index absent");
        return ESP_FAIL;
    }
    const cJSON *actions=cJSON_GetObjectItemCaseSensitive(index,"actions");
    const cJSON *entry=NULL;
    char name[96]={0},value[2]={0};
    cJSON_ArrayForEach(entry,actions){
        if(strcmp(jstr((cJSON*)entry,"type",""),"toggle"))continue;
        int off=jint((cJSON*)entry,"id0",-1);
        int on=jint((cJSON*)entry,"id1",-1);
        if(id!=off&&id!=on)continue;
        snprintf(name,sizeof(name),"%s",jstr((cJSON*)entry,"nom",""));
        snprintf(value,sizeof(value),"%s",id==on?"1":"0");
        break;
    }
    cJSON_Delete(index);
    if(!*name){
        httpd_resp_send_err(req,HTTPD_404_NOT_FOUND,"Interrupteur inconnu");
        return ESP_FAIL;
    }
    if(!mira_full_enqueue_state(name,value)){
        httpd_resp_set_status(req,"503 Service Unavailable");
        return httpd_resp_sendstr(req,"File d'etats pleine");
    }
    httpd_resp_set_type(req,"text/plain");
    return httpd_resp_sendstr(req,"OK");
}
static esp_err_t legacy_reboot_get(httpd_req_t *req){
    if(!require_key(req))return ESP_FAIL;
    esp_err_t sent=httpd_resp_sendstr(req,"OK");
    if(sent!=ESP_OK)return sent;
    vTaskDelay(pdMS_TO_TICKS(350));
    esp_restart();
    return ESP_OK;
}
void mira_jeedom_register_http(httpd_handle_t server) {
    const httpd_uri_t routes[]={
        {.uri="/getlvgl",.method=HTTP_GET,.handler=getlvgl},
        {.uri="/getindex",.method=HTTP_GET,.handler=getindex},
        {.uri="/get",.method=HTTP_GET,.handler=beta_get},
        {.uri="/events",.method=HTTP_GET,.handler=beta_events_get},
        {.uri="/testmodule",.method=HTTP_GET,.handler=testmodule},
        {.uri="/getmodule",.method=HTTP_GET,.handler=getmodule},
        {.uri="/displayinfo",.method=HTTP_GET,.handler=displayinfo},
        {.uri="/programui",.method=HTTP_POST,.handler=programui},
        {.uri="/addlistlvgl",.method=HTTP_GET,.handler=beta_lvgl_append},
        {.uri="/screenadd",.method=HTTP_GET,.handler=beta_lvgl_append},
        {.uri="/removelistlvgl",.method=HTTP_GET,.handler=beta_lvgl_clear},
        {.uri="/screenformat",.method=HTTP_GET,.handler=beta_lvgl_clear},
        {.uri="/savelistlvgl",.method=HTTP_GET,.handler=beta_lvgl_save},
        {.uri="/screensave",.method=HTTP_GET,.handler=beta_lvgl_save},
        {.uri="/updatenr",.method=HTTP_GET,.handler=updatenr},
        {.uri="/mira/jeedom/key",.method=HTTP_POST,.handler=provision_key},
        {.uri="/getparam",.method=HTTP_GET,.handler=param_get},
        {.uri="/getallparams",.method=HTTP_GET,.handler=getallparams},
        {.uri="/updateparam",.method=HTTP_GET,.handler=param_set},
        {.uri="/addparam",.method=HTTP_GET,.handler=param_set},
        {.uri="/addlistparam",.method=HTTP_GET,.handler=param_set},
        {.uri="/updatelistparam",.method=HTTP_GET,.handler=param_set},
        {.uri="/savelistparam",.method=HTTP_GET,.handler=param_saved},
        {.uri="/createlistparam",.method=HTTP_GET,.handler=param_saved},
        {.uri="/setjeedomid",.method=HTTP_GET,.handler=set_jeedom_id},
        {.uri="/setjeedomtoggleid",.method=HTTP_GET,.handler=set_jeedom_toggle_id},
        {.uri="/sendjeedomorderid",.method=HTTP_GET,.handler=set_jeedom_init_id},
        {.uri="/mira/jeedom/status",.method=HTTP_GET,.handler=jeedom_status_get},
        {.uri="/camera/config",.method=HTTP_POST,.handler=camera_config_post},
        {.uri="/navpages",.method=HTTP_GET,.handler=nav_pages_get},
        {.uri="/navpages",.method=HTTP_POST,.handler=nav_pages_post},
        {.uri="/navfavorites",.method=HTTP_GET,.handler=nav_favorites_get},
        {.uri="/navfavorites",.method=HTTP_POST,.handler=nav_favorites_post},
        {.uri="/mira-health",.method=HTTP_GET,.handler=mira_health_get},
        {.uri="/reboot",.method=HTTP_GET,.handler=legacy_reboot_get},
        {.uri="/sync/toggle",.method=HTTP_GET,.handler=sync_toggle_get},
    };
    for(size_t i=0;i<sizeof(routes)/sizeof(routes[0]);i++) {
        esp_err_t err=httpd_register_uri_handler(server,&routes[i]);
        if(err!=ESP_OK)ESP_LOGE(TAG,"Route %s failed: %s",routes[i].uri,esp_err_to_name(err));
    }
}
