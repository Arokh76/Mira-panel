// Mira Panel native ESP-IDF Jeedom HTTP compatibility (Waveshare 4.3C).
// Preserve the installed Jeedom plugin contract; no Arduino dependency.
// Reads/writes the existing LVGL JSON without erasing or formatting SPIFFS.
// Incoming state updates are PASSIVE, never forwarded to MQTT or an actuator.
#include "mira_jeedom.h"
#include "freertos/idf_additions.h" // PSRAM task/queue memory-capability APIs
#include "mira_full.h"
#include "mira_camera.h"
#include "mira_setup_access.h"
#include "mira_legacy_cipher.h"
#include "mira_mqtt.h"
#include "mira_beta_web.h"
#include <stdio.h>
#include <errno.h>
#include <sys/stat.h>
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
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"

#define MIRA_JSON_PATH "/spiffs/miralvgl.json"
#define MIRA_JSON_TMP  "/spiffs/miralvgl.new"
#define MIRA_JSON_MAX 49152
#define MIRA_FORM_MAX 110000
#define MIRA_KEY_MAX 96

static const char *TAG="mira-idf-jeedom";
// SPIFFS rename does not replace an existing destination. Stage the previous
// configuration under a backup name and restore it if promotion fails.
// Recover a backup left behind by a reset between the two renames.
static bool replace_lvgl_file_safely(void){
    const char *backup="/spiffs/miralvgl.bak";
    struct stat st;
    const bool has_target=(stat(MIRA_JSON_PATH,&st)==0);
    if(stat(backup,&st)==0){
        if(!has_target){
            if(rename(backup,MIRA_JSON_PATH)!=0){
                ESP_LOGE(TAG,"Cannot recover previous widget JSON: errno=%d",errno);
                return false;
            }
        }else if(remove(backup)!=0){
            ESP_LOGE(TAG,"Cannot clear previous widget backup: errno=%d",errno);
            return false;
        }
    }
    const bool old_exists=(stat(MIRA_JSON_PATH,&st)==0);
    if(old_exists&&rename(MIRA_JSON_PATH,backup)!=0){
        ESP_LOGE(TAG,"Cannot back up widget JSON: errno=%d",errno);
        return false;
    }
    if(rename(MIRA_JSON_TMP,MIRA_JSON_PATH)!=0){
        const int saved_errno=errno;
        if(old_exists&&rename(backup,MIRA_JSON_PATH)!=0)
            ESP_LOGE(TAG,"CRITICAL: widget JSON restore failed; backup remains: errno=%d",errno);
        ESP_LOGE(TAG,"Cannot promote widget JSON: errno=%d",saved_errno);
        return false;
    }
    if(old_exists&&remove(backup)!=0)
        ESP_LOGW(TAG,"Widget JSON installed; old backup cleanup failed: errno=%d",errno);
    return true;
}

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
static void mira_http_param_written(const char *name,const char *value);
static void mira_http_load_stored(void);
static bool nvs_read_string(nvs_handle_t nvs,const char *key,char *out,size_t size);
// As on WT32, the init command only requests passive state updates; it is
// NOT an actuator command. Revalidate states on reconnect and periodically.
static volatile bool jeedom_state_replay_pending=true;
static volatile uint32_t jeedom_replay_attempts,jeedom_replay_acks;
static volatile uint32_t jeedom_last_replay_ms,jeedom_last_replay_ok_ms;
static volatile int jeedom_last_replay_http;
static volatile uint32_t mira_sse_worker_failures;
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
    // WT32 authenticates against its already loaded RAM key. A transient NVS
    // read must never make /getindex fail while the web interface still works.
    return mira_beta_web_auth_key_copy(out,cap);
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
        /* Beta IndexAddList(): Listes_Value is an integer. Every
         * option in the same group carries that initial numeric ID,
         * including the second and later choices. Never turn those
         * values into JSON strings: Jeedom reads the actual JSON type.
         */
        cJSON_AddNumberToObject(entry,"value",first_id);
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

// WT32 parity: its SPIFFS_Config_Actions table persists between requests.
// In ESP-IDF keep its serialized equivalent and mutable response workspace in
// PSRAM. /getindex never reparses SPIFFS or allocates a cJSON action table.
// Only a programming change or the initial boot reconstructs the index.
// The response overlays current states (Info/Range/Toggle/Color) directly from
// the existing passive PSRAM cache. This also preserves WT32-style live values
// without mutating the immutable command ID mapping.
enum { MIRA_INDEX_INFO=1, MIRA_INDEX_RANGE, MIRA_INDEX_TOGGLE,
       MIRA_INDEX_RED, MIRA_INDEX_GREEN, MIRA_INDEX_BLUE };
typedef struct {
    uint32_t start,end;
    uint8_t kind;
    char name[96];
} mira_index_field_t;
static SemaphoreHandle_t mira_index_mutex;
static char *mira_index_template,*mira_index_response;
static size_t mira_index_template_len,mira_index_response_cap;
static mira_index_field_t *mira_index_fields;
static size_t mira_index_fields_count;
typedef struct {
    char name[192];
    char type[24];
    char list_name[96];
    int id,id0,id1;
} mira_action_ref_t;
static mira_action_ref_t *mira_action_refs;
static size_t mira_action_ref_count;
static uint32_t mira_index_hash;
typedef struct {
    uint32_t config_hash;
    uint32_t remote[4097];
    uint32_t valid_count;
} mira_binding_cache_t;
static mira_binding_cache_t *mira_binding_cache;
static uint32_t mira_mapping_last_attempt_ms,mira_mapping_last_ok_ms;
static volatile uint32_t mira_http_actions_queued,mira_http_actions_attempted;
static volatile uint32_t mira_http_actions_acked,mira_http_actions_failed;
static volatile uint32_t mira_http_actions_no_config,mira_http_actions_no_mapping;
static volatile uint32_t mira_http_actions_no_index;
static volatile int mira_http_action_last_status;
static uint32_t mira_index_builds,mira_index_failures,mira_index_responses;
static const char *index_find_field(const char *start,const char *end,
                                    const char *key,const char **token_end){
    char search[24];
    snprintf(search,sizeof(search),"\"%s\":",key);
    const char *p=strstr(start,search);
    if(!p||p>=end)return NULL;
    p+=strlen(search);
    const char *begin=p;
    if(*p=='"'){
        ++p;
        bool escaped=false;
        while(p<end){
            if(escaped)escaped=false;
            else if(*p=='\\')escaped=true;
            else if(*p=='"'){++p;break;}
            ++p;
        }
    }else{
        while(p<end&&*p!=','&&*p!='}'&&*p!=']')p++;
    }
    if(p>end||p<=begin)return NULL;
    *token_end=p;
    return begin;
}
static bool mira_index_rebuild(void){
    if(!mira_index_mutex){
        mira_index_mutex=xSemaphoreCreateMutex();
        if(!mira_index_mutex)return false;
    }
    char *raw=read_config(NULL);
    if(!raw){__atomic_fetch_add(&mira_index_failures,1u,__ATOMIC_RELAXED);return false;}
    const uint32_t index_hash=ui_config_hash(raw);
    cJSON *tree=index_from_config(raw);
    free(raw);
    if(!tree){__atomic_fetch_add(&mira_index_failures,1u,__ATOMIC_RELAXED);return false;}
    char *printed=cJSON_PrintUnformatted(tree);
    const cJSON *actions=cJSON_GetObjectItemCaseSensitive(tree,"actions");
    int count=cJSON_IsArray(actions)?cJSON_GetArraySize(actions):0;
    if(!printed||count<0||count>640){
        cJSON_free(printed);cJSON_Delete(tree);
        __atomic_fetch_add(&mira_index_failures,1u,__ATOMIC_RELAXED);
        return false;
    }
    const size_t length=strlen(printed);
    char *snapshot=heap_caps_malloc(length+1,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    const size_t capacity=(size_t)count*3u+1u;
    mira_index_field_t *fields=heap_caps_calloc(capacity,sizeof(*fields),
                                                 MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    mira_action_ref_t *refs=heap_caps_calloc(count?count:1,sizeof(*refs),
                                             MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!snapshot||!fields||!refs){
        heap_caps_free(snapshot);heap_caps_free(fields);heap_caps_free(refs);
        cJSON_free(printed);cJSON_Delete(tree);
        __atomic_fetch_add(&mira_index_failures,1u,__ATOMIC_RELAXED);
        return false;
    }
    memcpy(snapshot,printed,length+1);
    const char *cursor=strstr(snapshot,"\"actions\":[");
    if(cursor)cursor+=strlen("\"actions\":[");
    size_t field_count=0,ref_count=0;
    const cJSON *action=NULL;
    cJSON_ArrayForEach(action,actions){
        // Stable action-ID table mirrors WT32's in-memory SPIFFS_Config_Actions.
        const char *ref_name=jstr((cJSON*)action,"nom","");
        const char *ref_type=jstr((cJSON*)action,"type","");
        if(*ref_name&&strlen(ref_name)<sizeof(refs[0].name)&&
           strlen(ref_type)<sizeof(refs[0].type)&&ref_count<(size_t)count){
            mira_action_ref_t *ref=&refs[ref_count++];
            snprintf(ref->name,sizeof(ref->name),"%s",ref_name);
            snprintf(ref->type,sizeof(ref->type),"%s",ref_type);
            snprintf(ref->list_name,sizeof(ref->list_name),"%s",
                     jstr((cJSON*)action,"nomliste",""));
            ref->id=jint((cJSON*)action,"id",-1);
            ref->id0=jint((cJSON*)action,"id0",-1);
            ref->id1=jint((cJSON*)action,"id1",-1);
        }
        if(!cursor)continue;
        while(*cursor&&*cursor!='{')cursor++;
        if(!*cursor)break;
        const char *begin=cursor,*end=NULL;
        int nesting=0;
        bool quoted=false,escaped=false;
        for(const char *p=cursor;*p;p++){
            if(quoted){
                if(escaped)escaped=false;
                else if(*p=='\\')escaped=true;
                else if(*p=='"')quoted=false;
            }else if(*p=='"')quoted=true;
            else if(*p=='{')nesting++;
            else if(*p=='}'&&--nesting==0){end=p+1;break;}
        }
        if(!end)break;
        cursor=end;
        const char *type=jstr((cJSON*)action,"type","");
        const char *name=jstr((cJSON*)action,"nom","");
        if(!*name||strlen(name)>=96)continue;
        const char *keys[3]={NULL,NULL,NULL};
        uint8_t kinds[3]={0};
        unsigned wanted=0;
        if(!strcmp(type,"info")){keys[0]="value";kinds[0]=MIRA_INDEX_INFO;wanted=1;}
        else if(!strcmp(type,"range")){keys[0]="value";kinds[0]=MIRA_INDEX_RANGE;wanted=1;}
        else if(!strcmp(type,"toggle")){keys[0]="state";kinds[0]=MIRA_INDEX_TOGGLE;wanted=1;}
        else if(!strcmp(type,"color")){
            keys[0]="r";keys[1]="g";keys[2]="b";
            kinds[0]=MIRA_INDEX_RED;kinds[1]=MIRA_INDEX_GREEN;kinds[2]=MIRA_INDEX_BLUE;
            wanted=3;
        }
        for(unsigned j=0;j<wanted&&field_count<capacity;j++){
            const char *token_end=NULL;
            const char *token=index_find_field(begin,end,keys[j],&token_end);
            if(!token)continue;
            mira_index_field_t *field=&fields[field_count++];
            field->start=(uint32_t)(token-snapshot);
            field->end=(uint32_t)(token_end-snapshot);
            field->kind=kinds[j];
            snprintf(field->name,sizeof(field->name),"%s",name);
        }
    }
    cJSON_free(printed);
    cJSON_Delete(tree);
    // Control characters may become \\u00XX (six bytes); allocate enough
    // for the worst case, but bound the PSRAM workspace to avoid starvation.
    size_t render_cap=length+field_count*(160u*6u+4u)+1u;
    char *render=NULL;
    if(render_cap<=1000000u)
        render=heap_caps_malloc(render_cap,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!render){
        // The index is still usable with static values if memory is scarce.
        render_cap=0;
        ESP_LOGW(TAG,"MIRA_INDEX_DYNAMIC_BUFFER_UNAVAILABLE: using static PSRAM snapshot");
    }
    if(xSemaphoreTake(mira_index_mutex,pdMS_TO_TICKS(250))!=pdTRUE){
        heap_caps_free(snapshot);heap_caps_free(fields);heap_caps_free(refs);heap_caps_free(render);
        __atomic_fetch_add(&mira_index_failures,1u,__ATOMIC_RELAXED);
        return false;
    }
    char *previous_template=mira_index_template;
    char *previous_response=mira_index_response;
    mira_index_field_t *previous_fields=mira_index_fields;
    mira_action_ref_t *previous_refs=mira_action_refs;
    mira_index_template=snapshot;
    mira_index_template_len=length;
    mira_index_fields=fields;
    mira_index_fields_count=field_count;
    mira_action_refs=refs;
    mira_action_ref_count=ref_count;
    mira_index_hash=index_hash;
    mira_index_response=render;
    mira_index_response_cap=render_cap;
    __atomic_fetch_add(&mira_index_builds,1u,__ATOMIC_RELAXED);
    xSemaphoreGive(mira_index_mutex);
    heap_caps_free(previous_template);
    heap_caps_free(previous_response);
    heap_caps_free(previous_fields);
    heap_caps_free(previous_refs);
    ESP_LOGI(TAG,"MIRA_INDEX_PSRAM_READY: bytes=%u actions=%d live_fields=%u",
             (unsigned)length,count,(unsigned)field_count);
    return true;
}
static bool mira_index_quote(const char *source,char *dst,size_t cap){
    static const char hex[]="0123456789abcdef";
    size_t n=0;
    if(cap<3)return false;
    dst[n++]='"';
    for(const unsigned char *p=(const unsigned char *)source;*p;p++){
        const unsigned char c=*p;
        if(c=='"'||c=='\\'){
            if(n+2>=cap)return false;
            dst[n++]='\\';dst[n++]=(char)c;
        }else if(c<32){
            if(n+6>=cap)return false;
            dst[n++]='\\';dst[n++]='u';dst[n++]='0';dst[n++]='0';
            dst[n++]=hex[c>>4];dst[n++]=hex[c&15];
        }else{
            if(n+1>=cap)return false;
            dst[n++]=(char)c;
        }
    }
    if(n+2>cap)return false;
    dst[n++]='"';dst[n]=0;
    return true;
}
static bool mira_index_live_token(const mira_index_field_t *field,
                                  char *dst,size_t cap){
    char raw[160]={0};
    if(!mira_full_cached_state(field->name,raw,sizeof(raw)))return false;
    if(field->kind==MIRA_INDEX_INFO)return mira_index_quote(raw,dst,cap);
    if(field->kind==MIRA_INDEX_TOGGLE){
        const bool on=!strcasecmp(raw,"on")||!strcasecmp(raw,"true")||
                      !strcasecmp(raw,"oui")||!strcasecmp(raw,"actif")||
                      !strcmp(raw,"1");
        return mira_index_quote(on?"On":"Off",dst,cap);
    }
    if(field->kind==MIRA_INDEX_RANGE){
        char *end=NULL;
        errno=0;
        const double numeric=strtod(raw,&end);
        if(!*raw||!end||*end||errno==ERANGE||!isfinite(numeric))return false;
        const int n=snprintf(dst,cap,"%.10g",numeric);
        return n>0&&(size_t)n<cap;
    }
    unsigned r=0,g=0,b=0;
    if(raw[0]=='#'&&strlen(raw)==7){
        for(int i=1;i<7;i++)if(!isxdigit((unsigned char)raw[i]))return false;
        unsigned rgb=0;
        if(sscanf(raw+1,"%6x",&rgb)!=1)return false;
        r=(rgb>>16)&255u;g=(rgb>>8)&255u;b=rgb&255u;
    }else{
        char extra=0;
        if(sscanf(raw,"%u,%u,%u %c",&r,&g,&b,&extra)!=3||
           r>255||g>255||b>255)return false;
    }
    unsigned channel=field->kind==MIRA_INDEX_RED?r:
                     field->kind==MIRA_INDEX_GREEN?g:b;
    char value[4];
    snprintf(value,sizeof(value),"%u",channel);
    return mira_index_quote(value,dst,cap);
}
static esp_err_t mira_index_send_snapshot(httpd_req_t *req){
    if(!mira_index_template && !mira_index_rebuild())return ESP_ERR_NOT_FOUND;
    if(!mira_index_mutex||
       xSemaphoreTake(mira_index_mutex,pdMS_TO_TICKS(250))!=pdTRUE)return ESP_ERR_TIMEOUT;
    const char *payload=mira_index_template;
    size_t length=mira_index_template_len;
    if(payload&&mira_index_response&&mira_index_response_cap){
        char *output=mira_index_response;
        size_t n=0,cursor=0;
        bool complete=true;
        for(size_t i=0;i<mira_index_fields_count;i++){
            const mira_index_field_t *field=&mira_index_fields[i];
            if(field->start<cursor||field->end>length){complete=false;break;}
            const size_t literal=(size_t)field->start-cursor;
            char token[160*6+4];
            bool live=mira_index_live_token(field,token,sizeof(token));
            const char *part=live?token:payload+field->start;
            const size_t part_len=live?strlen(token):(size_t)field->end-field->start;
            if(n+literal+part_len>=mira_index_response_cap){complete=false;break;}
            memcpy(output+n,payload+cursor,literal);n+=literal;
            memcpy(output+n,part,part_len);n+=part_len;
            cursor=field->end;
        }
        if(complete&&n+(length-cursor)<mira_index_response_cap){
            memcpy(output+n,payload+cursor,length-cursor);
            n+=length-cursor;
            output[n]=0;
            payload=output;
            length=n;
        }else ESP_LOGW(TAG,"MIRA_INDEX_DYNAMIC_OVERLAY_SKIPPED: original snapshot retained");
    }
    esp_err_t result=payload?httpd_resp_send(req,payload,(ssize_t)length):ESP_ERR_NOT_FOUND;
    if(result==ESP_OK)__atomic_fetch_add(&mira_index_responses,1u,__ATOMIC_RELAXED);
    xSemaphoreGive(mira_index_mutex);
    return result;
}

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
/* ESP-IDF HTTPD asynchronous request: keep /events open like Arduino
 * AsyncEventSource. One worker per browser (maximum two), no socket writes
 * from LVGL, the MQTT callback, or the HTTP server's main task. A client
 * sees changed states in under 250 ms and gets periodic SSE keepalives.
 * Failed writes release the async request and its resources. */
typedef struct {
    httpd_req_t *req;
    cJSON *index;
    char (*last)[160];
    unsigned count;
    uint32_t last_direct_seq;
} mira_sse_client_t;
static unsigned mira_sse_clients;
static EventGroupHandle_t mira_sse_signal;
#define MIRA_SSE_CHANGED BIT0
void mira_jeedom_sse_notify(void){
    // All listening clients wake together; no arbitrary polling period.
    EventGroupHandle_t signal=mira_sse_signal;
    if(signal)xEventGroupSetBits(signal,MIRA_SSE_CHANGED);
}
/* Direct HTTP form actions used to call Arduino events.send() immediately.
 * A fixed 16-event history lets both browsers receive that event without
 * holding an HTTP socket or allocating from the LVGL task. */
typedef struct {uint32_t seq;int id;char value[160];} mira_sse_direct_t;
static mira_sse_direct_t mira_sse_direct[16];
static uint32_t mira_sse_direct_seq;
static portMUX_TYPE mira_sse_lock=portMUX_INITIALIZER_UNLOCKED;
static void mira_sse_record_direct(int id,const char *value){
    if(id<1||id>4096||!value)return;
    portENTER_CRITICAL(&mira_sse_lock);
    uint32_t next=++mira_sse_direct_seq;
    mira_sse_direct_t *slot=&mira_sse_direct[next%16u];
    slot->seq=next;slot->id=id;
    size_t n=0;
    while(value[n]&&n<sizeof(slot->value)-1){
        char c=value[n];
        slot->value[n]=(c=='\r'||c=='\n')?' ':c;
        n++;
    }
    slot->value[n]=0;
    portEXIT_CRITICAL(&mira_sse_lock);
    mira_jeedom_sse_notify();
}

static esp_err_t mira_sse_changed(mira_sse_client_t *client){
    const cJSON *actions=cJSON_GetObjectItemCaseSensitive(client->index,"actions");
    const cJSON *item=NULL;
    unsigned ordinal=0;
    cJSON_ArrayForEach(item,actions){
        if(ordinal>=client->count)break;
        char *previous=client->last[ordinal++];
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
                 strcmp(type,"inputbox")&&strcmp(type,"infofa"))continue;
        if(id<1||id>4096)continue;
        // A passive state cannot inject an extra SSE field or event.
        for(char *p=value;*p;p++)if(*p=='\r'||*p=='\n')*p=' ';
        if(!strcmp(previous,value))continue;
        char event[230];
        int written=snprintf(event,sizeof(event),"event: %d\ndata: %s\n\n",id,value);
        if(written<1||(size_t)written>=sizeof(event))continue;
        esp_err_t err=httpd_resp_send_chunk(client->req,event,(size_t)written);
        if(err!=ESP_OK)return err;
        snprintf(previous,160,"%s",value);
    }
    /* Send the direct events from the beta browser's action route,
     * without waiting for a separate Jeedom/MQTT state reflection. */
    for(unsigned attempt=0;attempt<16u;attempt++){
        mira_sse_direct_t copy={0};
        bool pending=false;
        portENTER_CRITICAL(&mira_sse_lock);
        uint32_t latest=mira_sse_direct_seq;
        if(latest-client->last_direct_seq>16u)
            client->last_direct_seq=latest-16u;
        if(client->last_direct_seq!=latest){
            const uint32_t wanted=client->last_direct_seq+1u;
            copy=mira_sse_direct[wanted%16u];
            client->last_direct_seq=wanted;
            pending=copy.seq==wanted;
        }
        portEXIT_CRITICAL(&mira_sse_lock);
        if(!pending)break;
        char event[230];
        int size=snprintf(event,sizeof(event),"event: %d\ndata: %s\n\n",copy.id,copy.value);
        if(size>0&&(size_t)size<sizeof(event)){
            esp_err_t err=httpd_resp_send_chunk(client->req,event,(size_t)size);
            if(err!=ESP_OK)return err;
        }
    }
    return ESP_OK;
}

static void mira_sse_worker(void *argument){
    mira_sse_client_t *client=(mira_sse_client_t *)argument;
    httpd_req_t *req=client->req;
    esp_err_t err=httpd_resp_set_type(req,"text/event-stream; charset=utf-8");
    if(err==ESP_OK)err=httpd_resp_set_hdr(req,"Cache-Control","no-cache");
    if(err==ESP_OK)err=httpd_resp_send_chunk(req,"retry: 3000\n\n",HTTPD_RESP_USE_STRLEN);
    TickType_t last_heartbeat=xTaskGetTickCount();
    while(err==ESP_OK){
        err=mira_sse_changed(client);
        if(err!=ESP_OK)break;
        // EventSource is notified directly by the MQTT/Jeedom state cache
        // or by the browser action route, just as Arduino events.send().
        xEventGroupWaitBits(mira_sse_signal,MIRA_SSE_CHANGED,pdTRUE,pdFALSE,
                            pdMS_TO_TICKS(10000));
        TickType_t now=xTaskGetTickCount();
        if(now-last_heartbeat>=pdMS_TO_TICKS(10000)){
            err=httpd_resp_send_chunk(req,": Mira keepalive\n\n",HTTPD_RESP_USE_STRLEN);
            last_heartbeat=now;
        }
    }
    // ESP-IDF closes/releases the duplicated HTTP request in this call.
    httpd_req_async_handler_complete(req);
    if(client->index)cJSON_Delete(client->index);
    free(client->last);
    free(client);
    __atomic_sub_fetch(&mira_sse_clients,1u,__ATOMIC_RELAXED);
    // The PSRAM-backed stack must be deleted with the matching ESP-IDF API.
    vTaskDeleteWithCaps(NULL);
}

static esp_err_t beta_events_get(httpd_req_t *req){
    if(!mira_beta_web_authorized(req))return ESP_FAIL;
    // Reserve at most two clients to protect the HTTP socket pool and RAM.
    unsigned active=__atomic_load_n(&mira_sse_clients,__ATOMIC_RELAXED);
    do {
        if(active>=2u){
            httpd_resp_set_status(req,"503 Service Unavailable");
            return httpd_resp_sendstr(req,"SSE client limit");
        }
    }while(!__atomic_compare_exchange_n(&mira_sse_clients,&active,active+1u,
                                        false,__ATOMIC_RELAXED,__ATOMIC_RELAXED));
    char *raw=read_config(NULL);
    cJSON *index=raw?index_from_config(raw):NULL;
    free(raw);
    const cJSON *actions=index?cJSON_GetObjectItemCaseSensitive(index,"actions"):NULL;
    const int count=cJSON_IsArray(actions)?cJSON_GetArraySize(actions):0;
    // MAX_BINDINGS=40; a media composite registers up to 15 Jeedom
    // actions per widget. Include the three internal weather Infos.
    if(count<0||count>640){
        if(index)cJSON_Delete(index);
        __atomic_sub_fetch(&mira_sse_clients,1u,__ATOMIC_RELAXED);
        httpd_resp_set_status(req,"503 Service Unavailable");
        httpd_resp_sendstr(req,"SSE index too large");
        return ESP_FAIL;
    }
    // Created once on startup, never concurrently in two HTTP requests.
    if(!mira_sse_signal){
        if(index)cJSON_Delete(index);
        __atomic_sub_fetch(&mira_sse_clients,1u,__ATOMIC_RELAXED);
        httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"SSE signal unavailable");
        return ESP_FAIL;
    }
    mira_sse_client_t *client=calloc(1,sizeof(*client));
    if(client){
        client->count=(unsigned)count;
        client->index=index;
        // Large composite indexes belong in PSRAM, never in internal
        // display/GT911 RAM. The screen requires PSRAM in any case.
        client->last=heap_caps_calloc(count?((size_t)count):1u,
                                     sizeof(*client->last),
                                     MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    }
    if(!client||!client->last){
        if(client)free(client);
        if(index)cJSON_Delete(index);
        __atomic_sub_fetch(&mira_sse_clients,1u,__ATOMIC_RELAXED);
        httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"SSE allocation failed");
        return ESP_FAIL;
    }
    portENTER_CRITICAL(&mira_sse_lock);
    client->last_direct_seq=mira_sse_direct_seq;
    portEXIT_CRITICAL(&mira_sse_lock);
    // The worker owns the duplicated request. The original request handler
    // returns immediately, leaving the browser's EventSource connected.
    esp_err_t err=httpd_req_async_handler_begin(req,&client->req);
    if(err!=ESP_OK){
        cJSON_Delete(index);free(client->last);free(client);
        __atomic_sub_fetch(&mira_sse_clients,1u,__ATOMIC_RELAXED);
        httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"SSE async unavailable");
        return ESP_FAIL;
    }
    // Unlike WT32's AsyncEventSource, an ESP-IDF SSE client needs a task.
    // The former 6144-byte INTERNAL stack exceeded the ~4.6KB largest block
    // on Mira 7. Allocate its 8KB stack in PSRAM; TCB stays internal.
    if(xTaskCreateWithCaps(mira_sse_worker,"mira_sse",8192,client,4,NULL,
                           MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)!=pdPASS){
        __atomic_fetch_add(&mira_sse_worker_failures,1u,__ATOMIC_RELAXED);
        ESP_LOGW(TAG,"MIRA_SSE_PSRAM_TASK_FAILED: free=%u largest_internal=%u",
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
        httpd_req_async_handler_complete(client->req);
        cJSON_Delete(index);free(client->last);free(client);
        __atomic_sub_fetch(&mira_sse_clients,1u,__ATOMIC_RELAXED);
        return ESP_FAIL;
    }
    return ESP_OK;
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
    /* Match Arduino events.send(data,event): the Web forms use
     * different SSE DATA contracts, not the raw action queue payload.
     * send: no SSE; range: value; color: "#rrggbb";
     * list: list group name; textbox: submitted text. */
    char sse_data[160]={0};
    const char *sse_payload=NULL;
    if(!strcmp(kind,"send_range"))sse_payload=payload;
    else if(!strcmp(kind,"send_list"))sse_payload=action_name;
    else if(!strcmp(kind,"send_textbox"))sse_payload=payload;else if(!strcmp(kind,"send_color")){
        unsigned red=0,green=0,blue=0;
        if(sscanf(payload,"%u,%u,%u",&red,&green,&blue)==3&&
           red<=255u&&green<=255u&&blue<=255u){
            snprintf(sse_data,sizeof(sse_data),"#%02x%02x%02x",
                     red,green,blue);
            sse_payload=sse_data;
        }
    }
    if(sse_payload)mira_sse_record_direct((int)id,sse_payload);
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
                }else if(!strcmp(kind,"httpaction"))value[0]=0;
                else snprintf(value,sizeof(value),"1");
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
    httpd_resp_set_type(req,"application/json");
    httpd_resp_set_hdr(req,"Cache-Control","no-store");
    const esp_err_t status=mira_index_send_snapshot(req);
    if(status==ESP_ERR_NOT_FOUND){
        httpd_resp_send_err(req,HTTPD_404_NOT_FOUND,"Index Mira absent");
        return ESP_FAIL;
    }
    if(status==ESP_ERR_TIMEOUT){
        httpd_resp_set_status(req,"503 Service Unavailable");
        httpd_resp_set_type(req,"text/plain");
        httpd_resp_sendstr(req,"Index Mira occupe");
        return ESP_FAIL;
    }
    return status;
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
    // Arduino MiraPanelUI::MAX_BINDINGS=40. Do not accept 41-64 widgets
    // which the screen would silently ignore after a successful Web save.
    if(cJSON_GetArraySize(array)>=40){
        httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Maximum 40 widgets LVGL");
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
    const cJSON *pending=cJSON_GetObjectItemCaseSensitive(beta_lvgl_stage,"lvgl");
    if(!cJSON_IsArray(pending)||cJSON_GetArraySize(pending)>40){
        httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,
                            "Programmation LVGL depasse 40 widgets");
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
    if(bytes!=len||closed!=0||!replace_lvgl_file_safely()){
        remove(MIRA_JSON_TMP);
        httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Sauvegarde LVGL impossible");
        return ESP_FAIL;
    }
    cJSON_Delete(beta_lvgl_stage);beta_lvgl_stage=NULL;
    beta_lvgl_dirty=false;
    if(!mira_index_rebuild())
        ESP_LOGW(TAG,"Jeedom index cache rebuild failed after screen save");
    mira_full_request_reload(); // always reload within the LVGL task
    jeedom_state_replay_pending=true;
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
    if(count>40)valid=false;  // Exact firmware-beta MiraPanelUI::MAX_BINDINGS.
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
    if(written!=len||close_error!=0||!replace_lvgl_file_safely()){
        remove(MIRA_JSON_TMP);
        httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Enregistrement Mira echoue");return ESP_FAIL;
    }
    cJSON_Delete(beta_lvgl_stage);beta_lvgl_stage=NULL;beta_lvgl_dirty=false;
    if(!mira_index_rebuild())
        ESP_LOGW(TAG,"Jeedom index cache rebuild failed after programming");
    mira_full_request_reload();
    jeedom_state_replay_pending=true;
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
    if(err==ESP_OK)mira_beta_web_auth_key_set(key->valuestring);
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
            // Beta refreshDisplayConfig(): DISPLAY_Delai controls the
            // sleep-enable switch after a new Jeedom configuration arrives.
            if(err==ESP_OK&&!strcmp(field->key,"sleep_delay"))
                err=nvs_set_u8(nv,"sleep_enabled",number>0?1:0);
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
    /* Arduino GetParamText() reports a missing parameter as null.
     * Do not forge "Jeedom=1" when NVS is absent: the default Jeedom
     * protocol on firmware-beta is disabled.
     */
    if(nvs_open("mira_idf_jeed",NVS_READONLY,&nv)!=ESP_OK)
        return httpd_resp_sendstr(req,"Null");
    char value[256]={0};size_t len=sizeof(value);
    esp_err_t err=nvs_get_str(nv,name,value,&len);
    nvs_close(nv);
    httpd_resp_set_type(req,"text/plain");
    return httpd_resp_sendstr(req,err==ESP_OK?value:"Null");
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
        // Never return an ordinary Info value for an InfoFA lookup:
        // the legacy /get?infofa=ID selector is a distinct action type.
        bool match=is_toggle?!strcmp(type,"toggle")&&
            (jint((cJSON*)entry,"id0",-1)==local||jint((cJSON*)entry,"id1",-1)==local):
            ((!strcmp(kind,"info")&&!strcmp(type,"info"))||
             (!strcmp(kind,"infofa")&&!strcmp(type,"infofa")))&&
             jint((cJSON*)entry,"id",-1)==local;
        if(!match)continue;
        const char *name=jstr((cJSON*)entry,"nom","");
        found=mira_full_cached_state(name,result,sizeof(result));
        if(is_toggle){
            /* Arduino IndexSetToggle() stores "On" or "Off" and /get
             * returns that canonical label, not the raw incoming "1"/"0"
             * which MQTT and Jeedom can send to the widget cache.
             * Unobserved toggles retain the default state "Off".
             */
            if(!found){
                snprintf(result,sizeof(result),"%s",
                         jstr((cJSON*)entry,"state","Off"));
                found=true;
            }else{
                const bool on=!strcasecmp(result,"On")||
                              !strcasecmp(result,"true")||
                              !strcasecmp(result,"oui")||
                              !strcasecmp(result,"actif")||
                              !strcmp(result,"1");
                snprintf(result,sizeof(result),"%s",on?"On":"Off");
            }
        }else if(!found){
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
            // Arduino beta does not invent Jeedom=1 for an unset option.
            // The synthesized /getallparams must agree with /getparam and
            // the live action transport (both default to disabled).
            continue;
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
    mira_http_param_written(name,value);
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
    if(id<1||id>4096||!value||!decimal_id(value)||
       strlen(value)>30)return false;
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
       !decimal_id(id)||!decimal_id(value)||
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
    if(!decimal_id(id)||!decimal_id(off)||!decimal_id(on)||
       local<1||local>=4096||
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
    if(!query_param(req,"value",id,sizeof(id))||!decimal_id(id)){
        httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Init ID absent ou invalide");
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
/* Latest-wins transport: a 40-widget panel has at most 80 independent
 * media-range actions. Reserve 96 keyed slots in PSRAM so a saturated
 * button FIFO cannot discard the final released slider/thermostat value.
 * Never execute HTTP while holding the short critical section. */
#define MIRA_LAST_VALUE_SLOTS 96u
static mira_action_msg_t *last_value_actions;
static TaskHandle_t action_worker_handle;
static portMUX_TYPE last_value_lock=portMUX_INITIALIZER_UNLOCKED;
static unsigned next_latest_slot;

// The WT32 holds JEEDOM_IP and JEEDOM_Key_mira in memory during normal
// operation. Do the same in PSRAM: an intermittent NVS miss during a click
// must not silently discard a physical light command.
typedef struct {
    char base[192];
    char secret[256];
} mira_http_config_t;
static SemaphoreHandle_t mira_http_config_mutex;
static mira_http_config_t *mira_http_config;
static void mira_http_param_written(const char *name,const char *value){
    if(!name||!value)return;
    if(strcmp(name,"Jeedom_IP")&&strcmp(name,"Jeedom_Key_mira")&&
       strcmp(name,"Jeedom"))return;
    if(!mira_http_config_mutex)
        mira_http_config_mutex=xSemaphoreCreateMutex();
    if(!mira_http_config_mutex)return;
    if(!mira_http_config){
        mira_http_config=heap_caps_calloc(1,sizeof(*mira_http_config),
                                         MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        if(!mira_http_config)return;
    }
    if(xSemaphoreTake(mira_http_config_mutex,pdMS_TO_TICKS(250))!=pdTRUE)return;
    if(!strcmp(name,"Jeedom_IP"))
        snprintf(mira_http_config->base,sizeof(mira_http_config->base),"%s",value);
    else if(!strcmp(name,"Jeedom_Key_mira"))
        snprintf(mira_http_config->secret,sizeof(mira_http_config->secret),"%s",value);
    else if(!strcmp(name,"Jeedom")&&!strcmp(value,"0")){
        // Security: turning Jeedom off also invalidates stored runtime credentials.
        memset(mira_http_config,0,sizeof(*mira_http_config));
    }
    xSemaphoreGive(mira_http_config_mutex);
}
static void mira_http_load_stored(void){
    if(!jeedom_mode_enabled)return;
    nvs_handle_t cfg;
    if(nvs_open("mira_idf_jeed",NVS_READONLY,&cfg)!=ESP_OK)return;
    char base[192]={0},secret[256]={0};
    const bool okay=nvs_read_string(cfg,"Jeedom_IP",base,sizeof(base))&&
                    nvs_read_string(cfg,"Jeedom_Key_mira",secret,sizeof(secret));
    nvs_close(cfg);
    if(okay){
        mira_http_param_written("Jeedom_IP",base);
        mira_http_param_written("Jeedom_Key_mira",secret);
    }
    memset(secret,0,sizeof(secret));
}
static bool mira_http_copy_config(char *base,size_t base_cap,
                                  char *secret,size_t secret_cap){
    if(!jeedom_mode_enabled||!base||!secret)return false;
    if(mira_http_config&&mira_http_config_mutex&&
       xSemaphoreTake(mira_http_config_mutex,pdMS_TO_TICKS(250))==pdTRUE){
        snprintf(base,base_cap,"%s",mira_http_config->base);
        snprintf(secret,secret_cap,"%s",mira_http_config->secret);
        xSemaphoreGive(mira_http_config_mutex);
        if(base[0]&&secret[0])return true;
    }
    // One controlled recovery attempt when parameters were missing at boot.
    // After success, subsequent actions use the PSRAM copy, not repeated NVS.
    mira_http_load_stored();
    if(mira_http_config&&mira_http_config_mutex&&
       xSemaphoreTake(mira_http_config_mutex,pdMS_TO_TICKS(250))==pdTRUE){
        snprintf(base,base_cap,"%s",mira_http_config->base);
        snprintf(secret,secret_cap,"%s",mira_http_config->secret);
        xSemaphoreGive(mira_http_config_mutex);
    }
    return base[0]&&secret[0];
}

static bool action_is_continuous(const char *type){
    return !strcmp(type,"range")||!strcmp(type,"roundrange")||
           !strcmp(type,"thermostat")||!strcmp(type,"color");
}
static bool enqueue_latest(const mira_action_msg_t *msg){
    if(!last_value_actions||!msg||!msg->name[0])return false;
    unsigned candidate=MIRA_LAST_VALUE_SLOTS;
    portENTER_CRITICAL(&last_value_lock);
    for(unsigned i=0;i<MIRA_LAST_VALUE_SLOTS;i++){
        if(last_value_actions[i].name[0]&&
           !strcmp(last_value_actions[i].name,msg->name)&&
           !strcmp(last_value_actions[i].type,msg->type)){
            candidate=i;break;
        }
        if(!last_value_actions[i].name[0]&&candidate==MIRA_LAST_VALUE_SLOTS)
            candidate=i;
    }
    if(candidate<MIRA_LAST_VALUE_SLOTS)last_value_actions[candidate]=*msg;
    portEXIT_CRITICAL(&last_value_lock);
    if(candidate==MIRA_LAST_VALUE_SLOTS){
        ESP_LOGE(TAG,"Jeedom last-value storage full; action refused");
        return false;
    }
    if(action_worker_handle)xTaskNotifyGive(action_worker_handle);
    return true;
}
static bool dequeue_latest(mira_action_msg_t *result){
    if(!last_value_actions||!result)return false;
    bool found=false;
    portENTER_CRITICAL(&last_value_lock);
    for(unsigned offset=0;offset<MIRA_LAST_VALUE_SLOTS;offset++){
        const unsigned i=(next_latest_slot+offset)%MIRA_LAST_VALUE_SLOTS;
        if(!last_value_actions[i].name[0])continue;
        *result=last_value_actions[i];
        last_value_actions[i].name[0]=0;
        next_latest_slot=(i+1u)%MIRA_LAST_VALUE_SLOTS;
        found=true;break;
    }
    portEXIT_CRITICAL(&last_value_lock);
    return found;
}


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
       strcmp(type,"list")&&strcmp(type,"listbutton")&&strcmp(type,"color")&&
       strcmp(type,"inputbox")&&strcmp(type,"httpaction"))return false;
    mira_action_msg_t msg={0};
    snprintf(msg.name,sizeof(msg.name),"%s",name);
    snprintf(msg.type,sizeof(msg.type),"%s",type);
    snprintf(msg.value,sizeof(msg.value),"%s",value);
    bool accepted=action_is_continuous(type)?
        enqueue_latest(&msg):xQueueSend(action_queue,&msg,0)==pdTRUE;
    if(!accepted)return false;
    __atomic_fetch_add(&mira_http_actions_queued,1u,__ATOMIC_RELAXED);
    if(action_worker_handle)xTaskNotifyGive(action_worker_handle);
    return true;
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
    if(action_is_continuous(type))return enqueue_latest(&msg);
    if(xQueueSend(action_queue,&msg,0)!=pdTRUE)return false;
    if(action_worker_handle)xTaskNotifyGive(action_worker_handle);
    return true;
}

static int resolve_local_action(const mira_action_msg_t *msg,
                                char *value,size_t value_size,
                                uint32_t *config_hash,uint32_t *remote_id){
    if(!mira_index_mutex||
       xSemaphoreTake(mira_index_mutex,pdMS_TO_TICKS(600))!=pdTRUE)return -1;
    int local_id=-1;
    char expected[192];
    for(size_t i=0;i<mira_action_ref_count;i++){
        const mira_action_ref_t *item=&mira_action_refs[i];
        const char *kind=item->type,*name=item->name;
        if(!strcmp(msg->type,"toggle")&&!strcmp(kind,"toggle")&&!strcmp(name,msg->name)){
            local_id=!strcmp(msg->value,"1")?item->id1:item->id0;
            break;
        }
        if((!strcmp(msg->type,"list")||!strcmp(msg->type,"listbutton"))&&
           !strcmp(kind,"list")&&!strcmp(item->list_name,msg->name)&&
           !strcmp(name,msg->value)){
            local_id=item->id;
            snprintf(value,value_size,"%d",local_id);
            break;
        }
        const bool range=!strcmp(msg->type,"range")||
                         !strcmp(msg->type,"roundrange")||
                         !strcmp(msg->type,"thermostat");
        if(!strcmp(msg->type,"thermostat"))
            snprintf(expected,sizeof(expected),"%.95s__consigne",msg->name);
        else snprintf(expected,sizeof(expected),"%s",msg->name);
        if(range&&!strcmp(kind,"range")&&!strcmp(name,expected)){
            local_id=item->id;
            snprintf(value,value_size,"%s",msg->value);
            break;
        }
        if(!strcmp(msg->type,"color")&&!strcmp(kind,"color")&&!strcmp(name,msg->name)){
            unsigned r=0,g=0,b=0;
            bool valid=false;
            if(msg->value[0]=='#'&&strlen(msg->value)==7){
                bool hex=true;
                for(size_t j=1;j<7;j++)
                    if(!isxdigit((unsigned char)msg->value[j])){hex=false;break;}
                if(hex){
                    unsigned color=0;
                    if(sscanf(msg->value+1,"%6x",&color)==1){
                        r=(color>>16)&255u;g=(color>>8)&255u;b=color&255u;
                        valid=true;
                    }
                }
            }else{
                char extra=0;
                valid=sscanf(msg->value,"%u,%u,%u %c",&r,&g,&b,&extra)==3;
            }
            if(!valid||r>255u||g>255u||b>255u)break;
            snprintf(value,value_size,"#%02X%02X%02X",r,g,b);
            local_id=item->id;
            break;
        }
        if((!strcmp(msg->type,"button")&&!strcmp(kind,"button"))||
           (!strcmp(msg->type,"httpaction")&&!strcmp(kind,"httpaction"))){
            if(!strcmp(name,msg->name)){local_id=item->id;break;}
        }
        if(!strcmp(msg->type,"inputbox")&&!strcmp(kind,"inputbox")&&
           !strcmp(name,msg->name)){
            local_id=item->id;
            snprintf(value,value_size,"%s",msg->value);
            break;
        }
        if(!strcmp(msg->type,"multiaction")&&!strcmp(kind,"button")){
            const char *prefix=NULL;
            if(!strcmp(msg->value,"single")||!strcmp(msg->value,"1"))
                prefix="Multi_Clic_";
            else if(!strcmp(msg->value,"double"))prefix="Multi_Double_";
            else if(!strcmp(msg->value,"triple"))prefix="Multi_Triple_";
            else if(!strcmp(msg->value,"long"))prefix="Multi_Long_";
            if(!prefix)break;
            snprintf(expected,sizeof(expected),"%s%.95s",prefix,msg->name);
            if(!strcmp(name,expected)){local_id=item->id;break;}
        }
    }
    if(config_hash)*config_hash=mira_index_hash;
    if(remote_id)*remote_id=
        local_id>0&&local_id<=4096&&mira_binding_cache&&
        mira_binding_cache->config_hash==mira_index_hash?
        mira_binding_cache->remote[local_id]:0;
    xSemaphoreGive(mira_index_mutex);
    return local_id;
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
/* Bounded response capture. Jeedom plugin event() replies with the ID of
 * the executed command; it may arrive across multiple HTTP data events.
 * No LVGL call, no secrets or response bodies written to logs.
 */
typedef struct {
    char buf[48];
    size_t len;
    bool overflow;
} mira_jeedom_ack_t;
static esp_err_t capture_jeedom_ack(esp_http_client_event_t *event){
    if(event->event_id!=HTTP_EVENT_ON_DATA||!event->user_data||
       event->data_len<=0)return ESP_OK;
    mira_jeedom_ack_t *ack=(mira_jeedom_ack_t*)event->user_data;
    const size_t n=(size_t)event->data_len;
    if(n>=sizeof(ack->buf)-ack->len){
        ack->overflow=true;
        return ESP_FAIL;
    }
    memcpy(ack->buf+ack->len,event->data,n);
    ack->len+=n;
    ack->buf[ack->len]=0;
    return ESP_OK;
}
static void send_jeedom_action(const mira_action_msg_t *msg) {
    // Never do flash I/O on every touch. WT32 keeps its action IDs and
    // outbound Jeedom URL in memory, with retries and explicit ID ACK.
    char base[192]={0},secret[256]={0};
    if(!mira_http_copy_config(base,sizeof(base),secret,sizeof(secret))){
        __atomic_fetch_add(&mira_http_actions_no_config,1u,__ATOMIC_RELAXED);
        ESP_LOGW(TAG,"JEEDOM_HTTP_ACTION_CONFIG_MISSING: no request sent");
        return;
    }
    if(strncmp(base,"http://",7)!=0||strchr(base,'?')||strchr(base,'#')||
       strchr(base,'@')||strchr(base,'\r')||strchr(base,'\n')){
        __atomic_fetch_add(&mira_http_actions_no_config,1u,__ATOMIC_RELAXED);
        return;
    }
    size_t baselen=strlen(base);
    while(baselen>7&&base[baselen-1]=='/')base[--baselen]=0;
    if(baselen<=7){
        __atomic_fetch_add(&mira_http_actions_no_config,1u,__ATOMIC_RELAXED);
        return;
    }

    char value[160]={0};
    uint32_t current_hash=0,remote_number=0;
    int local_id=resolve_local_action(msg,value,sizeof(value),
                                      &current_hash,&remote_number);
    if(local_id<1||local_id>4096){
        __atomic_fetch_add(&mira_http_actions_no_index,1u,__ATOMIC_RELAXED);
        ESP_LOGW(TAG,"JEEDOM_HTTP_ACTION_LOCAL_ID_MISSING: %s",msg->type);
        return;
    }
    char remote_id[24]={0};
    if(remote_number>0){
        snprintf(remote_id,sizeof(remote_id),"%" PRIu32,remote_number);
    }else{
        // Recovery only: if the in-RAM mapping has not yet been populated,
        // try the previously committed NVS ID once. Never trust stale hashes.
        nvs_handle_t map;
        if(nvs_open("mira_idf_map",NVS_READONLY,&map)==ESP_OK){
            char local_key[14],binding[48]={0};
            snprintf(local_key,sizeof(local_key),"cmd%d",local_id);
            bool mapped=nvs_read_string(map,local_key,binding,sizeof(binding));
            nvs_close(map);
            char expected_prefix[12];
            snprintf(expected_prefix,sizeof(expected_prefix),
                     "%08" PRIx32 ":",current_hash);
            if(mapped&&!strncmp(binding,expected_prefix,9)&&
               decimal_id(binding+9)){
                snprintf(remote_id,sizeof(remote_id),"%s",binding+9);
            }
        }
    }
    if(!remote_id[0]){
        __atomic_fetch_add(&mira_http_actions_no_mapping,1u,__ATOMIC_RELAXED);
        ESP_LOGW(TAG,"JEEDOM_HTTP_ACTION_MAPPING_MISSING: local=%d",local_id);
        return;
    }

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
    /* Firmware-beta SendQueud(): at most three sends, spaced by 3 seconds,
     * and removal only after ReponseAsync matches the requested Jeedom ID.
     * The jeedom-beta plugin's event() echoes init('id'), including for infos.
     * 2xx alone is not an acknowledgement; for example an HTML login page
     * can otherwise be misread as a successfully executed action.
     */
    for(unsigned attempt=1;attempt<=3;attempt++){
        mira_jeedom_ack_t acknowledgement={0};
        esp_http_client_config_t http={
            .url=uri,
            .timeout_ms=3000,
            .disable_auto_redirect=true,
            .event_handler=capture_jeedom_ack,
            .user_data=&acknowledgement
        };
        esp_http_client_handle_t client=esp_http_client_init(&http);
        if(!client){
            __atomic_fetch_add(&mira_http_actions_failed,1u,__ATOMIC_RELAXED);
            ESP_LOGW(TAG,"Jeedom HTTP client unavailable");return;
        }
        __atomic_fetch_add(&mira_http_actions_attempted,1u,__ATOMIC_RELAXED);
        esp_err_t result=esp_http_client_perform(client);
        const int status=esp_http_client_get_status_code(client);
        __atomic_store_n(&mira_http_action_last_status,status,__ATOMIC_RELAXED);
        esp_http_client_cleanup(client);
        /* Strip HTTP whitespace only; the plugin response itself must
         * contain exactly one numeric remote ID and no unexpected content.
         */
        while(acknowledgement.len &&
              isspace((unsigned char)acknowledgement.buf[acknowledgement.len-1]))
            acknowledgement.buf[--acknowledgement.len]=0;
        const char *reply=acknowledgement.buf;
        while(*reply && isspace((unsigned char)*reply))reply++;
        const bool confirmed=(result==ESP_OK&&status>=200&&status<300&&
                              !acknowledgement.overflow&&!strcmp(reply,remote_id));
        if(confirmed){
            __atomic_fetch_add(&mira_http_actions_acked,1u,__ATOMIC_RELAXED);
            ESP_LOGI(TAG,"Jeedom action local=%d acknowledged (attempt %u)",
                     local_id,attempt);
            return;
        }
        ESP_LOGW(TAG,"Jeedom action local=%d not acknowledged (try %u/3, HTTP %d, transport=%s)",
                 local_id,attempt,status,esp_err_to_name(result));
        if(attempt<3)vTaskDelay(pdMS_TO_TICKS(3000));
    }
    __atomic_fetch_add(&mira_http_actions_failed,1u,__ATOMIC_RELAXED);
    ESP_LOGE(TAG,"Jeedom action local=%d dropped after three unacknowledged sends",local_id);
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
/* Expose a sanitized mapping diagnostic for the Jeedom "unknown Mira ID"
 * error. Do not guess a replacement ID or disable the plugin validation. */
static volatile int last_binding_sync_http;
// The HTTP init action must not run while STA is still associating or
// before DHCP/static-IP provisioning has completed. AP IP is not sufficient.
static bool jeedom_sta_has_ip(void){
    esp_netif_t *sta=esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t ip={0};
    wifi_ap_record_t connected_ap={0};
    return sta&&esp_netif_get_ip_info(sta,&ip)==ESP_OK&&ip.ip.addr!=0&&
           esp_wifi_sta_get_ap_info(&connected_ap)==ESP_OK;
}
// WT32 sends its Init Jeedom request directly. Never require a successful
// Jeedom ID-map download before asking the plugin for its passive state data.
// The HTTP acknowledgement confirms dispatch, NOT that values were received.
// Recovery logic later repeats this read-only init when cache stays empty.
static bool jeedom_request_passive_states(void){
    if(!jeedom_sta_has_ip())return false;
    nvs_handle_t cfg;
    if(nvs_open("mira_idf_jeed",NVS_READONLY,&cfg)!=ESP_OK)return false;
    char on[8]={0},base[192]={0},secret[256]={0},init[24]={0};
    const bool ready=nvs_read_string(cfg,"Jeedom",on,sizeof(on))&&
                     !strcmp(on,"1")&&
                     nvs_read_string(cfg,"Jeedom_IP",base,sizeof(base))&&
                     nvs_read_string(cfg,"Jeedom_Key_mira",secret,sizeof(secret))&&
                     nvs_read_string(cfg,"Jeedom_Init_Cmd",init,sizeof(init));
    nvs_close(cfg);
    if(!ready||!decimal_id(init)||strncmp(base,"http://",7)||
       strchr(base,'?')||strchr(base,'#')||strchr(base,'@')||
       strchr(base,'\r')||strchr(base,'\n'))return false;
    size_t len=strlen(base);
    while(len>7&&base[len-1]=='/')base[--len]=0;
    char token[768];
    if(len<=7||!urlencode_to(token,sizeof(token),secret))return false;
    char url[1100];
    int n=snprintf(url,sizeof(url),
        "%s/core/api/jeeApi.php?plugin=mira&type=event&apikey=%s&id=%s",
        base,token,init);
    if(n<=0||n>=(int)sizeof(url))return false;
    const uint32_t now=(uint32_t)(esp_timer_get_time()/1000);
    jeedom_last_replay_ms=now;
    __atomic_fetch_add(&jeedom_replay_attempts,1u,__ATOMIC_RELAXED);
    mira_jeedom_ack_t ack={0};
    esp_http_client_config_t http={
        .url=url,.timeout_ms=18000,.disable_auto_redirect=true,
        .event_handler=capture_jeedom_ack,.user_data=&ack
    };
    esp_http_client_handle_t client=esp_http_client_init(&http);
    if(!client){jeedom_last_replay_http=0;return false;}
    esp_err_t result=esp_http_client_perform(client);
    int status=esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    jeedom_last_replay_http=status;
    while(ack.len&&isspace((unsigned char)ack.buf[ack.len-1]))
        ack.buf[--ack.len]=0;
    char *reply=ack.buf;
    while(*reply&&isspace((unsigned char)*reply))reply++;
    const bool acknowledged=result==ESP_OK&&status==200&&!ack.overflow&&
                             !strcmp(reply,init);
    if(acknowledged){
        jeedom_last_replay_ok_ms=(uint32_t)(esp_timer_get_time()/1000);
        __atomic_fetch_add(&jeedom_replay_acks,1u,__ATOMIC_RELAXED);
        jeedom_state_replay_pending=false;
        ESP_LOGI(TAG,"JEEDOM_STATE_REPLAY_ACK: read-only WT32 init accepted");
    }else{
        ESP_LOGW(TAG,"JEEDOM_STATE_REPLAY_FAILED: HTTP %d, transport=%s",
                 status,esp_err_to_name(result));
    }
    return acknowledged;
}
static void sync_jeedom_bindings(void){
    mira_mapping_last_attempt_ms=(uint32_t)(esp_timer_get_time()/1000);
    last_binding_sync_http=0;
    nvs_handle_t cfg;
    if(nvs_open("mira_idf_jeed",NVS_READONLY,&cfg)!=ESP_OK)return;
    char on[8]={0},base[192]={0},secret[256]={0},init[24]={0};
    const bool saved_mode=nvs_read_string(cfg,"Jeedom",on,sizeof(on));
    /* Beta GetParamBool("Jeedom") is false when the parameter is absent.
     * A missing NVS flag must never start background Jeedom sync.
     */
    const bool ready=saved_mode&&!strcmp(on,"1")&&
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
    last_binding_sync_http=status;
    if(result!=ESP_OK||status!=200||reply.overflow||reply.used<2){
        free(reply.buf);
        ESP_LOGW(TAG,"Jeedom Init Cmd ID %s not accepted: HTTP %d, transport %s",
                 init,status,esp_err_to_name(result));
        return;
    }
    cJSON *remote=cJSON_ParseWithLength(reply.buf,reply.used);
    free(reply.buf);
    if(!remote){
        ESP_LOGW(TAG,"Jeedom Init Cmd ID %s returned no valid command mapping",init);
        return;
    }
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
    // Stage the complete mapping in PSRAM, then publish it only when NVS
    // has committed the exact same validated ID set. Every outgoing action
    // will reuse this snapshot rather than reopening NVS on each touch.
    mira_binding_cache_t *fresh=heap_caps_calloc(1,sizeof(*fresh),
                                                 MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(fresh)fresh->config_hash=hash;
    nvs_handle_t map;
    esp_err_t err=nvs_open("mira_idf_map",NVS_READWRITE,&map);
    if(err!=ESP_OK){
        heap_caps_free(fresh);cJSON_Delete(local);cJSON_Delete(remote);
        return;
    }
    err=nvs_erase_all(map);
    cJSON *arr=cJSON_GetObjectItemCaseSensitive(remote,"actions"),*entry;
    int bound=0;
    if(err==ESP_OK) {
        cJSON_ArrayForEach(entry,arr){
            const bool toggle=!strcmp(jstr(entry,"type",""),"toggle");
            const int local0=toggle?jint(entry,"id0",-1):jint(entry,"id",-1);
            const int remote0=toggle?jint(entry,"jeedom0",-1):jint(entry,"jeedom",-1);
            err=save_remote_mapping(map,local0,remote0,hash);
            if(err!=ESP_OK)break;
            bound++;
            if(fresh&&local0>0&&local0<=4096&&remote0>0){
                fresh->remote[local0]=(uint32_t)remote0;
                fresh->valid_count++;
            }
            if(toggle){
                const int local1=jint(entry,"id1",-1),remote1=jint(entry,"jeedom1",-1);
                err=save_remote_mapping(map,local1,remote1,hash);
                if(err!=ESP_OK)break;
                bound++;
                if(fresh&&local1>0&&local1<=4096&&remote1>0){
                    fresh->remote[local1]=(uint32_t)remote1;
                    fresh->valid_count++;
                }
            }
        }
    }
    if(err==ESP_OK)err=nvs_commit(map);
    nvs_close(map);
    cJSON_Delete(local);
    cJSON_Delete(remote);
    if(err==ESP_OK){
        mira_mapping_last_ok_ms=(uint32_t)(esp_timer_get_time()/1000);
        if(fresh&&mira_index_mutex&&
           xSemaphoreTake(mira_index_mutex,pdMS_TO_TICKS(1000))==pdTRUE){
            if(mira_index_hash==hash){
                mira_binding_cache_t *old=mira_binding_cache;
                mira_binding_cache=fresh;
                fresh=NULL;
                xSemaphoreGive(mira_index_mutex);
                heap_caps_free(old);
            }else xSemaphoreGive(mira_index_mutex);
        }
        ESP_LOGI(TAG,"Jeedom full index synchronized: %d bounded command IDs",bound);
    }else ESP_LOGE(TAG,"Jeedom mapping validation/storage incomplete: %s",
                  esp_err_to_name(err));
    heap_caps_free(fresh);
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
    char init_id[24]={0};
    if(nvs_open("mira_idf_jeed",NVS_READONLY,&cfg)==ESP_OK){
        char v[8]={0};
        if(nvs_read_string(cfg,"Jeedom",v,sizeof(v)))enabled=strcmp(v,"0")!=0;
        (void)nvs_read_string(cfg,"Jeedom_Init_Cmd",init_id,sizeof(init_id));
        nvs_close(cfg);
    }
    if(!decimal_id(init_id))init_id[0]=0;
    mira_full_state_diagnostics_t health={0};
    mira_full_state_diagnostics(&health);
    char body[768];
    snprintf(body,sizeof(body),
        "{\"jeedom_enabled\":%s,\"mapping_valid\":%s,"
        "\"ids_valid\":%d,\"ids_expected\":%d,"
        "\"init_cmd_id\":\"%s\",\"last_init_http\":%d,"
        "\"state_cache_used\":%u,\"replay_pending\":%s,"
        "\"replay_attempts\":%u,\"replay_acks\":%u,"
        "\"replay_last_http\":%d,\"sse_clients\":%u,"
        "\"sse_task_failures\":%u,\"index_builds\":%u,"
        "\"index_failures\":%u,\"index_live_fields\":%u,"
        "\"index_responses\":%u}",
        enabled?"true":"false",
        (enabled&&total>0&&valid==total)?"true":"false",valid,total,
        init_id,last_binding_sync_http,(unsigned)health.state_cache_used,
        jeedom_state_replay_pending?"true":"false",
        (unsigned)__atomic_load_n(&jeedom_replay_attempts,__ATOMIC_RELAXED),
        (unsigned)__atomic_load_n(&jeedom_replay_acks,__ATOMIC_RELAXED),
        jeedom_last_replay_http,
        (unsigned)__atomic_load_n(&mira_sse_clients,__ATOMIC_RELAXED),
        (unsigned)__atomic_load_n(&mira_sse_worker_failures,__ATOMIC_RELAXED),
        (unsigned)__atomic_load_n(&mira_index_builds,__ATOMIC_RELAXED),
        (unsigned)__atomic_load_n(&mira_index_failures,__ATOMIC_RELAXED),
        (unsigned)mira_index_fields_count,
        (unsigned)__atomic_load_n(&mira_index_responses,__ATOMIC_RELAXED));
    httpd_resp_set_type(req,"application/json");
    return httpd_resp_sendstr(req,body);
}

static void request_binding_sync(void){
    if(!action_queue)return;
    mira_action_msg_t msg={0};
    snprintf(msg.type,sizeof(msg.type),"__sync");
    if(xQueueSend(action_queue,&msg,0)!=pdTRUE)
        ESP_LOGW(TAG,"Jeedom sync FIFO busy; periodic state recovery remains active");
    if(action_worker_handle)xTaskNotifyGive(action_worker_handle);
}
void mira_jeedom_network_reconnected(void){
    // Wi-Fi callbacks must never start HTTP work or touch LVGL. Queue the
    // same read-only re-initialization the WT32 performs after reconnect.
    jeedom_state_replay_pending=true;
    jeedom_last_replay_ms=0;
    request_binding_sync();
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

static void jeedom_replay_maintenance(void){
    if(!jeedom_mode_enabled||!jeedom_sta_has_ip())return;
    const uint32_t now=(uint32_t)(esp_timer_get_time()/1000);
    const uint32_t since_attempt=(uint32_t)(now-jeedom_last_replay_ms);
    // Retry sparingly if init has failed or no passive values have arrived.
    // Refresh at most every 30 minutes even with a nonempty cache, so a
    // long-running screen recovers after an interrupted Jeedom update stream.
    // No user actuator command is ever synthesized here.
    if(jeedom_last_replay_ms && since_attempt<300000u)return;
    mira_full_state_diagnostics_t health={0};
    mira_full_state_diagnostics(&health);
    const bool no_values=health.state_cache_used==0;
    const bool refresh_due=jeedom_last_replay_ok_ms==0||
        (uint32_t)(now-jeedom_last_replay_ok_ms)>=1800000u;
    if(jeedom_state_replay_pending||no_values||refresh_due){
        ESP_LOGI(TAG,"JEEDOM_REPLAY_MAINT: pending=%d cache=%u",
                 jeedom_state_replay_pending?1:0,
                 (unsigned)health.state_cache_used);
        (void)jeedom_request_passive_states();
    }
}
static void jeedom_action_worker(void *unused) {
    (void)unused;
    mira_action_msg_t action;
    bool prefer_latest=true;
    for(;;) {
        bool found=prefer_latest&&dequeue_latest(&action);
        if(!found)found=xQueueReceive(action_queue,&action,0)==pdTRUE;
        if(!found)found=dequeue_latest(&action);
        if(!found){
            // Periodic wakeup permits state recovery without an extra task.
            // The WT32 already treats Jeedom init as a read-only replay.
            (void)ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(30000));
            jeedom_replay_maintenance();
            continue;
        }
        prefer_latest=!prefer_latest;
        if(!strcmp(action.type,"__sync")){
            // Mapping and state replay are independent. A temporary failure
            // fetching ID associations must not suppress incoming Info values.
            const unsigned max_attempts=4;
            bool mapping_checked=false;
            for(unsigned attempt=1;attempt<=max_attempts;attempt++){
                if(!jeedom_mode_enabled)break;
                if(jeedom_sta_has_ip()){
                    if(!mapping_checked){
                        sync_jeedom_bindings();
                        mapping_checked=true;
                    }
                    if(!jeedom_state_replay_pending||
                       jeedom_request_passive_states())break;
                }else{
                    ESP_LOGI(TAG,"JEEDOM_BOOT_REPLAY_WAIT_IP=%u/%u",
                             attempt,max_attempts);
                }
                if(attempt<max_attempts)vTaskDelay(pdMS_TO_TICKS(3000));
            }
            if(jeedom_state_replay_pending)
                ESP_LOGW(TAG,"JEEDOM_REPLAY_PENDING: automatic maintenance will retry");
            ESP_LOGI(TAG,"JEEDOM_HTTP_STACK_MIN_FREE=%u bytes",
                     (unsigned)uxTaskGetStackHighWaterMark(NULL));
        }else if(!strcmp(action.type,"__generic"))send_generic_action(&action);
        else send_jeedom_action(&action);
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
               nvs_commit(nv)==ESP_OK){
                mira_beta_web_auth_key_set(plaintext);
                ESP_LOGI(TAG,"Legacy Mira screen authentication imported");
            }
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
// Receive-only beta HTTP/SSE: no FreeRTOS HTTP action worker, no outbound
// Jeedom commands. The event-group is needed by /events clients.
esp_err_t mira_jeedom_passive_init(void){
    if(!mira_sse_signal)mira_sse_signal=xEventGroupCreate();
    if(!mira_sse_signal)return ESP_ERR_NO_MEM;
    // Startup allocates and serializes the index once, as on WT32.
    // Failure does not disable incoming passive states or browser recovery.
    if(!mira_index_rebuild())
        ESP_LOGW(TAG,"Jeedom PSRAM index cache unavailable; /getindex will retry");
    ESP_LOGI(TAG,"JEEDOM_PASSIVE_HTTP_READY: /updatenr, /get, /events; outbound actions deferred");
    return ESP_OK;
}
bool mira_jeedom_passive_ready(void){return mira_sse_signal!=NULL;}

esp_err_t mira_jeedom_action_init(void) {
    if(action_queue)return ESP_OK;
    // Keep the SSE event group immutable after the HTTP server starts:
    // two simultaneous EventSource clients must never race to create it.
    if(!mira_sse_signal)mira_sse_signal=xEventGroupCreate();
    if(!mira_sse_signal)return ESP_ERR_NO_MEM;
    import_legacy_screen_key();
    import_legacy_http_settings();
    nvs_handle_t settings;
    if(nvs_open("mira_idf_jeed",NVS_READONLY,&settings)==ESP_OK){
        char active[8]={0};size_t len=sizeof(active);
        if(nvs_get_str(settings,"Jeedom",active,&len)==ESP_OK)
            jeedom_mode_enabled=strcmp(active,"0")!=0;
        nvs_close(settings);
    }
    // External RAM for queue messages: RGB+Wi-Fi leaves a 6.9KB largest
    // internal block and the old ~3.4KB queue plus 7168B worker stack
    // exhausted it. Keep all action data in PSRAM.
    action_queue=xQueueCreateWithCaps(12,sizeof(mira_action_msg_t),
                                      MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!action_queue){
        ESP_LOGE(TAG,"JEEDOM_ACTION_QUEUE_PSRAM_NO_MEM");
        return ESP_ERR_NO_MEM;
    }
    last_value_actions=heap_caps_calloc(MIRA_LAST_VALUE_SLOTS,
                      sizeof(*last_value_actions),
                      MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!last_value_actions){
        vQueueDeleteWithCaps(action_queue);
        action_queue=NULL;
        return ESP_ERR_NO_MEM;
    }
    // Jeedom index validation + HTTP + cJSON + NVS can nest deeply on
    // initial synchronization. 7168 bytes overflowed reliably on boot
    // (2026-11-10 serial logs); give the worker 24 KiB of octal PSRAM.
    // WithCaps keeps the TCB internal. No blocking HTTP on LVGL.
    if(xTaskCreateWithCaps(jeedom_action_worker,"mira_jeedom_http",24576,
                           NULL,3,&action_worker_handle,
                           MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)!=pdPASS) {
        heap_caps_free(last_value_actions);
        last_value_actions=NULL;
        vQueueDeleteWithCaps(action_queue);
        action_queue=NULL;
        ESP_LOGE(TAG,"JEEDOM_ACTION_STACK_PSRAM_NO_MEM");
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG,"JEEDOM_ACTIONS_PSRAM_READY: mode=%s dma_free=%u dma_largest=%u psram=%u",
        jeedom_mode_enabled?"JEEDOM":"GENERIC",
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_DMA),
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_DMA),
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    mira_http_load_stored(); // WT32-style outbound HTTP config in PSRAM
    request_binding_sync(); // read-only ID map fetch; never an actuator command
    return ESP_OK;
}
bool mira_jeedom_action_ready(void){
    return action_queue!=NULL&&last_value_actions!=NULL&&action_worker_handle!=NULL;
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
