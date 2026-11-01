// Mira Panel native ESP-IDF Jeedom HTTP compatibility (Waveshare 4.3C).
// Preserve the installed Jeedom plugin contract; no Arduino dependency.
// Reads/writes the existing LVGL JSON without erasing or formatting SPIFFS.
// Incoming state updates are PASSIVE, never forwarded to MQTT or an actuator.
#include "mira_jeedom.h"
#include "mira_full.h"
#include "mira_setup_access.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <inttypes.h>
#include "cJSON.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_http_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#define MIRA_JSON_PATH "/spiffs/miralvgl.json"
#define MIRA_JSON_TMP  "/spiffs/miralvgl.new"
#define MIRA_JSON_MAX 49152
#define MIRA_FORM_MAX 110000
#define MIRA_KEY_MAX 96

static const char *TAG="mira-idf-jeedom";
static volatile bool jeedom_mode_enabled;
bool mira_jeedom_actions_enabled(void){return jeedom_mode_enabled;}
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
    char query[600];
    if(httpd_req_get_url_query_str(req,query,sizeof(query))!=ESP_OK)return false;
    char encoded[512];
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
    char *save=NULL;
    for(char *option=strtok_r(choices,"|",&save);option;option=strtok_r(NULL,"|",&save)){
        trim(option);
        if(!*option)continue;
        cJSON *entry=append_action(arr,id,"list",option);
        if(!entry)return;
        cJSON_AddStringToObject(entry,"nomliste",name);
        cJSON_AddStringToObject(entry,"option",option);
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
        if(!*name||!*type)continue;
        if(!strcmp(type,"info")||!strcmp(type,"button")){
            cJSON *a=append_action(arr,&id,type,name);
            if(a && !strcmp(type,"info")){
                cJSON_AddStringToObject(a,"unit",jstr((cJSON*)w,"unit",""));
                cJSON_AddStringToObject(a,"value",jstr((cJSON*)w,"value",""));
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
            cJSON *a=append_action(arr,&id,"range",name);
            if(a){
                cJSON_AddNumberToObject(a,"min",jint((cJSON*)w,"min",0));
                cJSON_AddNumberToObject(a,"max",jint((cJSON*)w,"max",100));
                cJSON_AddNumberToObject(a,"value",jint((cJSON*)w,"value",0));
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
            if(a)cJSON_AddStringToObject(a,"unit","°C");
            snprintf(compound,sizeof(compound),"%s__consigne",name);
            a=append_action(arr,&id,"range",compound);
            if(a){
                cJSON_AddNumberToObject(a,"min",jint((cJSON*)w,"min",5));
                cJSON_AddNumberToObject(a,"max",jint((cJSON*)w,"max",30));
                cJSON_AddNumberToObject(a,"value",jint((cJSON*)w,"value",20));
            }
            snprintf(compound,sizeof(compound),"%s__mode",name);
            add_choices(arr,&id,compound,jstr((cJSON*)w,"modes","Confort|Éco|Hors gel|Arrêt"));
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
                append_action(arr,&id,"info",compound);
            }
            for(size_t n=0;n<3;n++){
                char compound[192];snprintf(compound,sizeof(compound),"%.95s__%s",name,media_actions[n]);
                append_action(arr,&id,"button",compound);
            }
            char compound[192];
            snprintf(compound,sizeof(compound),"%.95s__pause",name);
            append_action(arr,&id,"button",compound);
            snprintf(compound,sizeof(compound),"%.95s__volume_cmd",name);
            append_action(arr,&id,"range",compound);
            snprintf(compound,sizeof(compound),"%.95s__device_cmd",name);
            append_action(arr,&id,"range",compound);
        }else if(!strcmp(type,"datetime")&&!weather_registered){
            weather_registered=true;
            append_action(arr,&id,"info","__mira_weather_temperature");
            append_action(arr,&id,"info","__mira_weather_condition");
            append_action(arr,&id,"info","__mira_weather_code");
        }
    }
    cJSON_Delete(root);
    return out;
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
    return httpd_resp_sendstr(req,
      "{\"profile\":\"waveshare43c\",\"width\":800,\"height\":480,"
      "\"contentWidth\":800,\"contentHeight\":428,\"navHeight\":52,"
      "\"rotation\":0,\"logicalWidth\":800,\"logicalHeight\":480,"
      "\"httpPrimary\":true,\"mqttOptional\":true,\"freeLayout\":false}");
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
    bool valid=root&&cJSON_IsArray(cJSON_GetObjectItemCaseSensitive(root,"lvgl"));
    if(root)cJSON_Delete(root);
    if(!valid){free(body);httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Tableau lvgl absent");return ESP_FAIL;}
    FILE *out=fopen(MIRA_JSON_TMP,"wb");
    if(!out){free(body);httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"SPIFFS non accessible");return ESP_FAIL;}
    size_t written=fwrite(encoded_json,1,len,out);
    int close_error=fclose(out);
    free(body);
    if(written!=len||close_error!=0||rename(MIRA_JSON_TMP,MIRA_JSON_PATH)!=0){
        remove(MIRA_JSON_TMP);
        httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Enregistrement Mira echoue");return ESP_FAIL;
    }
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
    char selected[24]={0},action_id[16]={0},value[160]={0};
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
                    int ri=atoi(r),gi=atoi(g),bi=atoi(b);
                    if(ri<0||ri>255||gi<0||gi>255||bi<0||bi>255)break;
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
static bool supported_jeedom_param(const char *name){
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
    nvs_handle_t nv;
    if(nvs_open("mira_idf_jeed",NVS_READONLY,&nv)!=ESP_OK)
        return httpd_resp_sendstr(req,"Null");
    char value[256]={0};size_t len=sizeof(value);
    esp_err_t err=nvs_get_str(nv,name,value,&len);
    nvs_close(nv);
    httpd_resp_set_type(req,"text/plain");
    return httpd_resp_sendstr(req,err==ESP_OK?value:"Null");
}
static esp_err_t param_set(httpd_req_t *req){
    if(!require_key(req))return ESP_FAIL;
    char name[32],value[256];
    if(!query_param(req,"param",name,sizeof(name))||
       !query_param(req,"value",value,sizeof(value))||
       !supported_jeedom_param(name)){
        httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Parametre Jeedom invalide");
        return ESP_FAIL;
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
       strcmp(type,"list")&&strcmp(type,"listbutton"))return false;
    mira_action_msg_t msg={0};
    snprintf(msg.name,sizeof(msg.name),"%s",name);
    snprintf(msg.type,sizeof(msg.type),"%s",type);
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
        if(!strcmp(msg->type,"button")&&!strcmp(kind,"button")&&!strcmp(name,msg->name))
            return jint(item,"id",-1);
        // Multiaction's ordinary click is the same as the first physical trigger.
        if(!strcmp(msg->type,"multiaction")&&!strcmp(kind,"button")) {
            snprintf(expected,sizeof(expected),"Multi_Clic_%.95s",msg->name);
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
    bool ready=nvs_read_string(cfg,"Jeedom",enabled,sizeof(enabled))&&
               !strcmp(enabled,"1")&&
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
    const bool ready=nvs_read_string(cfg,"Jeedom",on,sizeof(on))&&
                     !strcmp(on,"1")&&
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
    bool enabled=false;
    if(nvs_open("mira_idf_jeed",NVS_READONLY,&cfg)==ESP_OK){
        char v[8]={0};
        enabled=nvs_read_string(cfg,"Jeedom",v,sizeof(v))&&!strcmp(v,"1");
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

static void jeedom_action_worker(void *unused) {
    (void)unused;
    mira_action_msg_t action;
    for(;;) {
        if(xQueueReceive(action_queue,&action,portMAX_DELAY)==pdTRUE){
            if(!strcmp(action.type,"__sync"))sync_jeedom_bindings();
            else send_jeedom_action(&action);
        }
    }
}
esp_err_t mira_jeedom_action_init(void) {
    if(action_queue)return ESP_OK;
    nvs_handle_t settings;
    if(nvs_open("mira_idf_jeed",NVS_READONLY,&settings)==ESP_OK){
        char active[8]={0};size_t len=sizeof(active);
        jeedom_mode_enabled=nvs_get_str(settings,"Jeedom",active,&len)==ESP_OK&&!strcmp(active,"1");
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

void mira_jeedom_register_http(httpd_handle_t server) {
    const httpd_uri_t routes[]={
        {.uri="/getlvgl",.method=HTTP_GET,.handler=getlvgl},
        {.uri="/getindex",.method=HTTP_GET,.handler=getindex},
        {.uri="/testmodule",.method=HTTP_GET,.handler=testmodule},
        {.uri="/getmodule",.method=HTTP_GET,.handler=getmodule},
        {.uri="/displayinfo",.method=HTTP_GET,.handler=displayinfo},
        {.uri="/programui",.method=HTTP_POST,.handler=programui},
        {.uri="/updatenr",.method=HTTP_GET,.handler=updatenr},
        {.uri="/mira/jeedom/key",.method=HTTP_POST,.handler=provision_key},
        {.uri="/getparam",.method=HTTP_GET,.handler=param_get},
        {.uri="/addlistparam",.method=HTTP_GET,.handler=param_set},
        {.uri="/updatelistparam",.method=HTTP_GET,.handler=param_set},
        {.uri="/savelistparam",.method=HTTP_GET,.handler=param_saved},
        {.uri="/createlistparam",.method=HTTP_GET,.handler=param_saved},
        {.uri="/setjeedomid",.method=HTTP_GET,.handler=set_jeedom_id},
        {.uri="/setjeedomtoggleid",.method=HTTP_GET,.handler=set_jeedom_toggle_id},
        {.uri="/sendjeedomorderid",.method=HTTP_GET,.handler=set_jeedom_init_id},
        {.uri="/mira/jeedom/status",.method=HTTP_GET,.handler=jeedom_status_get},
    };
    for(size_t i=0;i<sizeof(routes)/sizeof(routes[0]);i++) {
        esp_err_t err=httpd_register_uri_handler(server,&routes[i]);
        if(err!=ESP_OK)ESP_LOGE(TAG,"Route %s failed: %s",routes[i].uri,esp_err_to_name(err));
    }
}
