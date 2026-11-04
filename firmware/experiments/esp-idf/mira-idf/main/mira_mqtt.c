// Native ESP-IDF MQTT bridge for Mira 4.3C.
// States update only the LVGL task via its bounded queue; never emit actions.
#include "mira_mqtt.h"
#include "mira_full.h"
#include "mira_setup_access.h"
#include "mira_legacy_cipher.h"
#include "mqtt_client.h"
#include "nvs.h"
#include "cJSON.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include <ctype.h>

static const char *TAG="mira-mqtt-idf";
static esp_mqtt_client_handle_t client;
static bool connected;
static bool enabled;
static char host[128], username[64], secret[96], screen_name[48];
static uint16_t port=1883;
static char state_prefix[112], action_prefix[112],display_prefix[112];
static char last_display_signature[160];
static int64_t last_display_check_us;
/* ESP-MQTT may split a single PUBLISH into several MQTT_EVENT_DATA
 * callbacks, with the topic only on the first fragment. Buffer a bounded
 * 1024-byte envelope and dispatch once at total_data_len, never partially. */
static struct {
    char topic[240];
    char payload[1025];
    int expected;
    int received;
    int topic_len;
    bool active;
} incoming_fragment;
static void clear_fragment(void){
    incoming_fragment.active=false;
    incoming_fragment.expected=0;
    incoming_fragment.received=0;
    incoming_fragment.topic_len=0;
}


/* Exact MiraMqtt::cleanScreenName() behavior for MQTT prefixes.
 * Stored settings are left intact; only the outbound topic name is
 * normalized. Slash hierarchy inside the name is legal in the beta,
 * whereas '+' and '#' must never become topic wildcards.
 */
static void clean_screen_name(char *name,size_t cap){
    if(!name||cap<6)return;
    size_t n=strlen(name),begin=0;
    while(begin<n&&isspace((unsigned char)name[begin]))begin++;
    while(n>begin&&isspace((unsigned char)name[n-1]))n--;
    if(begin)memmove(name,name+begin,n-begin);
    name[n-begin]=0;
    while(name[0]=='/')memmove(name,name+1,strlen(name));
    n=strlen(name);
    while(n&&name[n-1]=='/')name[--n]=0;
    for(char *p=name;*p;p++)if(*p=='#'||*p=='+')*p='_';
    if(!name[0]||!strcmp(name,"null"))snprintf(name,cap,"panel");
}

static void refresh_prefixes(void) {
    snprintf(state_prefix,sizeof(state_prefix),"mira/%s/States/",screen_name);
    snprintf(action_prefix,sizeof(action_prefix),"mira/%s/Actions/",screen_name);
    snprintf(display_prefix,sizeof(display_prefix),"mira/%s/Display/",screen_name);
}
static void stop_client(void) {
    connected=false;
    clear_fragment();
    if(!client)return;
    esp_mqtt_client_stop(client);
    esp_mqtt_client_destroy(client);
    client=NULL;
}
/* MiraMqtt::mqttPayloadValue in firmware-beta trims the MQTT envelope
 * BEFORE parsing a small {"value":...} JSON object. The first IDF port
 * rejected JSON envelopes larger than the final 159-byte info value and
 * failed to recognize envelopes beginning with whitespace.
 *
 * Accept a bounded envelope (not arbitrary broker data), decode, then
 * apply the existing 159/768-byte *decoded* value limit. Preserve any
 * interior whitespace of JSON string properties, as Arduino does.
 */
static bool mqtt_decode_payload(const char *payload,int length,
                                char *out,size_t capacity,size_t max_value){
    if(!payload||!out||!capacity||length<0||length>1024)return false;
    if(memchr(payload,0,(size_t)length)!=NULL)return false;
    char envelope[1025]={0};
    memcpy(envelope,payload,(size_t)length);
    size_t used=(size_t)length,leading=0;
    while(leading<used&&isspace((unsigned char)envelope[leading]))leading++;
    while(used>leading&&isspace((unsigned char)envelope[used-1]))used--;
    if(used==leading)return false;
    if(leading)memmove(envelope,envelope+leading,used-leading);
    envelope[used-leading]=0;
    const char *decoded=envelope;
    char *owned=NULL;
    cJSON *json=NULL;
    if(envelope[0]=='{'){
        json=cJSON_Parse(envelope);
        const cJSON *item=json?cJSON_GetObjectItemCaseSensitive(json,"value"):NULL;
        if(cJSON_IsString(item)&&item->valuestring)decoded=item->valuestring;
        else if(item){
            owned=cJSON_PrintUnformatted(item);
            if(owned)decoded=owned;
        }
    }
    size_t final_len=strlen(decoded);
    const bool valid=final_len<=max_value && final_len<capacity;
    if(valid)memcpy(out,decoded,final_len+1);
    if(owned)cJSON_free(owned);
    if(json)cJSON_Delete(json);
    return valid;
}
static void incoming_state(const char *topic, int tlen, const char *payload, int plen) {
    const size_t prefix_len=strlen(state_prefix);
    if(tlen <= (int)prefix_len || tlen > (int)prefix_len + 95 || plen<0)return;
    if(strncmp(topic,state_prefix,prefix_len)!=0)return;
    char name[96]={0};
    memcpy(name,topic+prefix_len,(size_t)tlen-prefix_len);
    const size_t n=strlen(name);
    const bool cover=n>=7&&!strcmp(name+n-7,"__cover");
    const size_t limit=cover?768u:159u;
    char value[769]={0};
    if(!mqtt_decode_payload(payload,plen,value,sizeof(value),limit))return;
    (void)mira_full_enqueue_state(name,value);
}
// Mira beta: mira/<screen>/Display/<key>/set
// Queue only; no LVGL calls or state-to-action MQTT feedback here.
static void incoming_display(const char *topic,int length,const char *payload,int plen){
    const size_t prefix_len=strlen(display_prefix);
    if(length<=(int)prefix_len+4||plen<1||
       strncmp(topic,display_prefix,prefix_len)!=0)return;
    const size_t remaining=(size_t)length-prefix_len;
    if(remaining<=4||memcmp(topic+length-4,"/set",4)!=0)return;
    const size_t key_len=remaining-4;
    if(key_len==0||key_len>=32)return;
    char key[32]={0},value[48]={0};
    memcpy(key,topic+prefix_len,key_len);
    for(size_t i=0;i<key_len;i++)
        if(key[i]=='/'||key[i]=='+'||key[i]=='#')return;
    if(!mqtt_decode_payload(payload,plen,value,sizeof(value),47u))return;
    if(mira_full_enqueue_display_command(key,value)){
        /* Arduino publishDisplayStates(true) sends the new retained values
         * after every Display/<key>/set, without waiting for the normal
         * 15-second change scan. The LVGL task will apply this command
         * before sending the next snapshot; no cross-thread LVGL access.
         */
        last_display_signature[0]=0;
    }
}
static void mqtt_event(void *arg, esp_event_base_t base,int32_t event_id,void *event_data) {
    (void)arg;(void)base;
    esp_mqtt_event_handle_t e=(esp_mqtt_event_handle_t)event_data;
    if(event_id==MQTT_EVENT_CONNECTED) {
        connected=true;
        // Request full retained-state publication by the next LVGL UI tick.
        // MQTT callbacks never read LVGL-owned display fields.
        last_display_signature[0]=0;
        last_display_check_us=0;
        char subscription[120];
        snprintf(subscription,sizeof(subscription),"%s#",state_prefix);
        esp_mqtt_client_subscribe(client,subscription,0);
        snprintf(subscription,sizeof(subscription),"%s+/set",display_prefix);
        esp_mqtt_client_subscribe(client,subscription,0);
        ESP_LOGI(TAG,"Connected. Subscribed state and display controls for %s",screen_name);
    }else if(event_id==MQTT_EVENT_DISCONNECTED){
        connected=false;
        clear_fragment();
    }else if(event_id==MQTT_EVENT_DATA) {
        if(e->total_data_len<1||e->total_data_len>1024||
           e->data_len<0||e->current_data_offset<0||
           e->current_data_offset>e->total_data_len||
           e->data_len>e->total_data_len-e->current_data_offset){
            clear_fragment();
            return;
        }
        if(e->current_data_offset==0){
            clear_fragment();
            if(!e->topic||e->topic_len<1||
               e->topic_len>=(int)sizeof(incoming_fragment.topic))return;
            memcpy(incoming_fragment.topic,e->topic,(size_t)e->topic_len);
            incoming_fragment.topic[e->topic_len]=0;
            incoming_fragment.topic_len=e->topic_len;
            incoming_fragment.expected=e->total_data_len;
            incoming_fragment.active=true;
        }
        if(!incoming_fragment.active||
           incoming_fragment.expected!=e->total_data_len||
           incoming_fragment.received!=e->current_data_offset||
           (!e->data&&e->data_len>0)){
            clear_fragment();
            return;
        }
        if(e->data_len>0)
            memcpy(incoming_fragment.payload+incoming_fragment.received,
                   e->data,(size_t)e->data_len);
        incoming_fragment.received+=e->data_len;
        if(incoming_fragment.received==incoming_fragment.expected){
            incoming_state(incoming_fragment.topic,incoming_fragment.topic_len,
                           incoming_fragment.payload,incoming_fragment.received);
            incoming_display(incoming_fragment.topic,incoming_fragment.topic_len,
                             incoming_fragment.payload,incoming_fragment.received);
            clear_fragment();
        }
    }
}
static void start_client(void) {
    stop_client();
    if(!enabled||!host[0])return;
    char uri[160];
    snprintf(uri,sizeof(uri),"mqtt://%s:%u",host,(unsigned)port);
    esp_mqtt_client_config_t config={
        .broker.address.uri=uri,
        .credentials.username=username[0]?username:NULL,
        .credentials.authentication.password=secret[0]?secret:NULL,
        /* firmware-beta MiraMqtt.h KEEPALIVE_S=20 */
        .session.keepalive=20,
    };
    client=esp_mqtt_client_init(&config);
    if(!client){ESP_LOGE(TAG,"MQTT init failed");return;}
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_mqtt_client_register_event(client,ESP_EVENT_ANY_ID,mqtt_event,NULL));
    if(esp_mqtt_client_start(client)!=ESP_OK)stop_client();
}
// Migrate the ORIGINAL beta MQTT parameters once, without rewriting params.json.
// Native NVS settings always have precedence over old SPIFFS data.
static void import_beta_mqtt_config(void){
    FILE *fp=fopen("/spiffs/params.json","rb");
    if(!fp)return;
    if(fseek(fp,0,SEEK_END)!=0){fclose(fp);return;}
    long size=ftell(fp);
    if(size<2||size>32768||fseek(fp,0,SEEK_SET)!=0){fclose(fp);return;}
    char *buf=malloc((size_t)size+1);
    if(!buf){fclose(fp);return;}
    size_t n=fread(buf,1,(size_t)size,fp);
    fclose(fp);
    if(n!=(size_t)size){free(buf);return;}
    buf[n]=0;
    cJSON *root=cJSON_ParseWithLength(buf,n);
    free(buf);
    const cJSON *params=root?cJSON_GetObjectItemCaseSensitive(root,"params"):NULL;
    if(!cJSON_IsArray(params)){if(root)cJSON_Delete(root);return;}
    nvs_handle_t nv;
    if(nvs_open("mira_idf_mqtt",NVS_READWRITE,&nv)!=ESP_OK){
        cJSON_Delete(root);return;
    }
    unsigned imported=0;
    const cJSON *item=NULL;
    cJSON_ArrayForEach(item,params){
        const cJSON *p=cJSON_GetObjectItemCaseSensitive(item,"param");
        const cJSON *v=cJSON_GetObjectItemCaseSensitive(item,"value");
        const cJSON *t=cJSON_GetObjectItemCaseSensitive(item,"type");
        if(!cJSON_IsString(p)||!cJSON_IsString(v)||!p->valuestring||!v->valuestring)continue;
        const char *legacy=p->valuestring;
        const char *stored=v->valuestring;
        if(!*stored||!strcmp(stored,"null")||strlen(stored)>768)continue;
        const char *key=NULL;
        unsigned size_limit=0;
        if(!strcmp(legacy,"MQTT_Nom")){key="screen";size_limit=48;}
        else if(!strcmp(legacy,"MQTT_Serveur")){key="host";size_limit=128;}
        else if(!strcmp(legacy,"MQTT_Utilisateur")){key="username";size_limit=64;}
        else if(!strcmp(legacy,"MQTT_Mot_de_passe")){key="secret";size_limit=96;}
        else if(strcmp(legacy,"MQTT_Port")&&strcmp(legacy,"MQTT_Actif"))continue;
        char plaintext[256]={0};
        const char *value=stored;
        if(cJSON_IsString(t)&&t->valuestring&&!strcmp(t->valuestring,"crypted-text")){
            if(!mira_legacy_decrypt(stored,plaintext,sizeof(plaintext)))continue;
            value=plaintext;
        }
        if(key){
            size_t used=strlen(value),found=0;
            if(used>=size_limit)goto clear_value;
            if(!strcmp(key,"screen")&&!used)goto clear_value;
            /* Arduino stores the label and normalizes it only when
             * constructing MQTT topics; don't drop compatible beta names.
             */
            if(nvs_get_str(nv,key,NULL,&found)==ESP_ERR_NVS_NOT_FOUND &&
               nvs_set_str(nv,key,value)==ESP_OK)imported++;
        }else if(!strcmp(legacy,"MQTT_Port")){
            unsigned long port=0;
            char *end=NULL;
            port=strtoul(value,&end,10);
            uint16_t old=0;
            if(end!=value&&!*end&&port>=1&&port<=65535 &&
               nvs_get_u16(nv,"port",&old)==ESP_ERR_NVS_NOT_FOUND &&
               nvs_set_u16(nv,"port",(uint16_t)port)==ESP_OK)imported++;
        }else if(!strcmp(legacy,"MQTT_Actif")){
            uint8_t old=0;
            if((!strcmp(value,"1")||!strcmp(value,"0")||
                !strcasecmp(value,"true")||!strcasecmp(value,"false")) &&
               nvs_get_u8(nv,"enabled",&old)==ESP_ERR_NVS_NOT_FOUND &&
               nvs_set_u8(nv,"enabled",
                   (uint8_t)(!strcmp(value,"1")||!strcasecmp(value,"true")))==ESP_OK)imported++;
        }
clear_value:
        memset(plaintext,0,sizeof(plaintext));
    }
    if(imported&&nvs_commit(nv)!=ESP_OK)
        ESP_LOGW(TAG,"Legacy MQTT import could not be committed");
    else if(imported)
        ESP_LOGI(TAG,"Migrated %u beta MQTT settings into native NVS",imported);
    nvs_close(nv);
    cJSON_Delete(root);
}

static void read_config(void) {
    /* A disabled/reprovisioned broker must never inherit old credentials
     * or a stale enabled flag from the previous live configuration. */
    host[0]=0;username[0]=0;secret[0]=0;
    enabled=false;port=1883;
    snprintf(screen_name,sizeof(screen_name),"panel");
    nvs_handle_t nvs;
    if(nvs_open("mira_idf_mqtt",NVS_READONLY,&nvs)!=ESP_OK){refresh_prefixes();return;}
    size_t z;
    z=sizeof(host);nvs_get_str(nvs,"host",host,&z);
    z=sizeof(username);nvs_get_str(nvs,"username",username,&z);
    z=sizeof(secret);nvs_get_str(nvs,"secret",secret,&z);
    z=sizeof(screen_name);nvs_get_str(nvs,"screen",screen_name,&z);
    uint8_t on=0; nvs_get_u8(nvs,"enabled",&on);enabled=on!=0;
    nvs_get_u16(nvs,"port",&port);
    if(!port)port=1883;
    nvs_close(nvs);
    /* Beta refreshConfig() treats literal "null" as an absent MQTT
     * credential or host, never a broker name or username/password.
     */
    if(!strcmp(host,"null"))host[0]=0;
    if(!strcmp(username,"null"))username[0]=0;
    if(!strcmp(secret,"null"))secret[0]=0;
    clean_screen_name(screen_name,sizeof(screen_name));
    refresh_prefixes();
}
esp_err_t mira_mqtt_init(void) {
    import_beta_mqtt_config();
    read_config();
    start_client();
    ESP_LOGI(TAG,"Native MQTT %s (disabled until provisioned)",enabled?"enabled":"disabled");
    return ESP_OK;
}
/* Exact firmware-beta topics: mira/<screen>/Display/<key>/state.
 * Retained, read-only snapshots. MQTT callbacks do not enter LVGL.
 * Publish on connect and on actual changes (checked at most once / 15 s),
 * without forwarding a passive state as a command.
 */
void mira_mqtt_publish_display_states(const mira_mqtt_display_state_t *state){
    if(!state||!enabled||!connected||!client)return;
    const int64_t now=esp_timer_get_time();
    const bool first=last_display_signature[0]==0;
    if(!first && now-last_display_check_us<15000000LL)return;
    last_display_check_us=now;
    char signature[160];
    const int n=snprintf(signature,sizeof(signature),
        "%u|%u|%u|%lu|%u|%s|%s|%u|%u|%s|%s",
        (unsigned)state->day,(unsigned)state->night,
        (unsigned)state->sleep_brightness,
        (unsigned long)state->sleep_timeout,
        (unsigned)state->night_auto,state->night_start,state->night_end,
        (unsigned)state->clock_sleep,(unsigned)state->brightness,
        state->sleeping?"sleep":"awake",state->night_active?"night":"day");
    if(n<0||n>=(int)sizeof(signature))return;
    if(!first&&!strcmp(signature,last_display_signature))return;
    static const char *const names[]={
        "day","night","sleep_brightness","sleep_timeout","night_auto",
        "night_start","night_end","clock_sleep","brightness","mode","state"
    };
    char values[11][24]={0};
    snprintf(values[0],24,"%u",(unsigned)state->day);
    snprintf(values[1],24,"%u",(unsigned)state->night);
    snprintf(values[2],24,"%u",(unsigned)state->sleep_brightness);
    snprintf(values[3],24,"%lu",(unsigned long)state->sleep_timeout);
    snprintf(values[4],24,"%u",(unsigned)state->night_auto);
    snprintf(values[5],24,"%s",state->night_start);
    snprintf(values[6],24,"%s",state->night_end);
    snprintf(values[7],24,"%u",(unsigned)state->clock_sleep);
    snprintf(values[8],24,"%u",(unsigned)state->brightness);
    snprintf(values[9],24,"%s",state->night_active?"night":"day");
    snprintf(values[10],24,"%s",state->sleeping?"sleep":"awake");
    bool sent=true;
    for(size_t i=0;i<sizeof(names)/sizeof(names[0]);i++){
        char topic[160];
        const int t=snprintf(topic,sizeof(topic),"%s%s/state",display_prefix,names[i]);
        if(t<0||t>=(int)sizeof(topic)||
           esp_mqtt_client_enqueue(client,topic,values[i],0,0,1,true)<0){
            sent=false;
            break;
        }
    }
    // A partial failure will trigger retry rather than permanently storing
    // an incomplete display state snapshot.
    if(sent)snprintf(last_display_signature,sizeof(last_display_signature),
                      "%s",signature);
}

bool mira_mqtt_publish_action(const char *name,const char *value) {
    if(!client||!connected||!enabled||!name||!value)return false;
    if(strlen(name)>95||strlen(value)>159)return false;
    char topic[240];snprintf(topic,sizeof(topic),"%s%s",action_prefix,name);
    // Only actual user LVGL event callbacks call this; MQTT State callbacks never do.
    return esp_mqtt_client_enqueue(client,topic,value,0,0,0,true)>=0;
}
static esp_err_t mqtt_get(httpd_req_t *r) {
    if(!mira_setup_require_ap(r))return ESP_FAIL;
    // Names can legally include quotes and backslashes: escape JSON using
    // cJSON instead of interpolating an unchecked NVS string into quotes.
    cJSON *result=cJSON_CreateObject();
    if(!result)return httpd_resp_send_err(r,HTTPD_500_INTERNAL_SERVER_ERROR,
                                           "MQTT status unavailable");
    cJSON_AddBoolToObject(result,"enabled",enabled);
    cJSON_AddBoolToObject(result,"connected",connected);
    cJSON_AddStringToObject(result,"screen",screen_name);
    char *payload=cJSON_PrintUnformatted(result);
    cJSON_Delete(result);
    if(!payload)return httpd_resp_send_err(r,HTTPD_500_INTERNAL_SERVER_ERROR,
                                            "MQTT status unavailable");
    httpd_resp_set_type(r,"application/json");
    esp_err_t sent=httpd_resp_sendstr(r,payload);
    cJSON_free(payload);
    return sent;
}
static esp_err_t mqtt_post(httpd_req_t *r) {
    if(!mira_setup_require_ap(r))return ESP_FAIL;
    if(r->content_len<2||r->content_len>768){
        httpd_resp_send_err(r,HTTPD_400_BAD_REQUEST,"Invalid MQTT configuration size");
        return ESP_FAIL;
    }
    char body[769];size_t got=0,len=r->content_len;
    while(got<len) {
        int n=httpd_req_recv(r,body+got,len-got);
        if(n<=0){httpd_resp_send_err(r,HTTPD_408_REQ_TIMEOUT,"Incomplete MQTT JSON");return ESP_FAIL;}
        got+=(size_t)n;
    }
    body[len]=0;
    cJSON *root=cJSON_ParseWithLength(body,len);
    const cJSON *h=root?cJSON_GetObjectItemCaseSensitive(root,"host"):NULL;
    const cJSON *p=root?cJSON_GetObjectItemCaseSensitive(root,"port"):NULL;
    const cJSON *n=root?cJSON_GetObjectItemCaseSensitive(root,"screen"):NULL;
    const cJSON *u=root?cJSON_GetObjectItemCaseSensitive(root,"username"):NULL;
    const cJSON *s=root?cJSON_GetObjectItemCaseSensitive(root,"password"):NULL;
    const cJSON *en=root?cJSON_GetObjectItemCaseSensitive(root,"enabled"):NULL;
    if(!cJSON_IsString(h)||!h->valuestring||strlen(h->valuestring)>=sizeof(host)||
       !cJSON_IsString(n)||!n->valuestring||
       strlen(n->valuestring)>=sizeof(screen_name)||
       !cJSON_IsNumber(p)||p->valueint<1||p->valueint>65535||
       (u&&(!cJSON_IsString(u)||strlen(u->valuestring)>=sizeof(username)))||
       (s&&(!cJSON_IsString(s)||strlen(s->valuestring)>=sizeof(secret)))||
       (!cJSON_IsBool(en)&&!cJSON_IsNumber(en))) {
        if(root)cJSON_Delete(root);
        httpd_resp_send_err(r,HTTPD_400_BAD_REQUEST,"Expected host, port, screen, enabled and optional username/password");
        return ESP_FAIL;
    }
    /* Preserve beta's MQTT screen names, including subtopics. The
     * clean_screen_name() pass strips leading/trailing '/' and replaces
     * wildcard characters before constructing any subscription topic.
     */
    nvs_handle_t nv;
    esp_err_t err=nvs_open("mira_idf_mqtt",NVS_READWRITE,&nv);
    if(err!=ESP_OK){cJSON_Delete(root);httpd_resp_send_err(r,HTTPD_500_INTERNAL_SERVER_ERROR,"NVS unavailable");return ESP_FAIL;}
    err=nvs_set_str(nv,"host",h->valuestring);
    if(err==ESP_OK)err=nvs_set_str(nv,"screen",n->valuestring);
    if(err==ESP_OK)err=nvs_set_u16(nv,"port",(uint16_t)p->valueint);
    if(err==ESP_OK)err=nvs_set_u8(nv,"enabled",cJSON_IsTrue(en)|| (cJSON_IsNumber(en)&&en->valueint!=0));
    if(err==ESP_OK&&u)err=nvs_set_str(nv,"username",u->valuestring);
    /* Match beta HTML MQTT settings: a blank password means keep
     * the existing broker secret (do not silently disable authentication). */
    if(err==ESP_OK&&s&&s->valuestring[0])
        err=nvs_set_str(nv,"secret",s->valuestring);
    if(err==ESP_OK)err=nvs_commit(nv);
    nvs_close(nv);cJSON_Delete(root);
    if(err!=ESP_OK){httpd_resp_send_err(r,HTTPD_500_INTERNAL_SERVER_ERROR,"NVS write failed");return ESP_FAIL;}
    read_config();
    start_client();
    httpd_resp_set_type(r,"application/json");
    return httpd_resp_sendstr(r,"{\"saved\":true}");
}
void mira_mqtt_register_http(httpd_handle_t server) {
    const httpd_uri_t get={.uri="/mira/mqtt",.method=HTTP_GET,.handler=mqtt_get};
    const httpd_uri_t post={.uri="/mira/mqtt",.method=HTTP_POST,.handler=mqtt_post};
    ESP_ERROR_CHECK_WITHOUT_ABORT(httpd_register_uri_handler(server,&get));
    ESP_ERROR_CHECK_WITHOUT_ABORT(httpd_register_uri_handler(server,&post));
}
