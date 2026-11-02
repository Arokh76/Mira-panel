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
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>

static const char *TAG="mira-mqtt-idf";
static esp_mqtt_client_handle_t client;
static bool connected;
static bool enabled;
static char host[128], username[64], secret[96], screen_name[48];
static uint16_t port=1883;
static char state_prefix[112], action_prefix[112],display_prefix[112];

static void refresh_prefixes(void) {
    snprintf(state_prefix,sizeof(state_prefix),"mira/%s/States/",screen_name);
    snprintf(action_prefix,sizeof(action_prefix),"mira/%s/Actions/",screen_name);
    snprintf(display_prefix,sizeof(display_prefix),"mira/%s/Display/",screen_name);
}
static void stop_client(void) {
    connected=false;
    if(!client)return;
    esp_mqtt_client_stop(client);
    esp_mqtt_client_destroy(client);
    client=NULL;
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
    if((size_t)plen>limit)return;
    char value[769]={0};
    memcpy(value,payload,(size_t)plen);
    if(memchr(value,'\\0',(size_t)plen))return;
    if(value[0]=='{'){
        cJSON *root=cJSON_Parse(value);
        const cJSON *v=root?cJSON_GetObjectItemCaseSensitive(root,"value"):NULL;
        if(cJSON_IsString(v)&&v->valuestring){
            if(strlen(v->valuestring)>limit){cJSON_Delete(root);return;}
            snprintf(value,sizeof(value),"%s",v->valuestring);
        }else if(cJSON_IsNumber(v))
            snprintf(value,sizeof(value),"%.9g",v->valuedouble);
        else if(cJSON_IsBool(v))
            snprintf(value,sizeof(value),"%s",cJSON_IsTrue(v)?"1":"0");
        if(root)cJSON_Delete(root);
    }
    (void)mira_full_enqueue_state(name,value);
}
// Mira beta: mira/<screen>/Display/<key>/set
// Queue only; no LVGL calls or state-to-action MQTT feedback here.
static void incoming_display(const char *topic,int length,const char *payload,int plen){
    const size_t prefix_len=strlen(display_prefix);
    if(length<=(int)prefix_len+4||plen<1||plen>=48||
       strncmp(topic,display_prefix,prefix_len)!=0)return;
    const size_t remaining=(size_t)length-prefix_len;
    if(remaining<=4||memcmp(topic+length-4,"/set",4)!=0)return;
    const size_t key_len=remaining-4;
    if(key_len==0||key_len>=32)return;
    char key[32]={0},value[48]={0};
    memcpy(key,topic+prefix_len,key_len);
    for(size_t i=0;i<key_len;i++)
        if(key[i]=='/'||key[i]=='+'||key[i]=='#')return;
    memcpy(value,payload,(size_t)plen);
    if(memchr(value,'\0',(size_t)plen)!=NULL)return;
    (void)mira_full_enqueue_display_command(key,value);
}
static void mqtt_event(void *arg, esp_event_base_t base,int32_t event_id,void *event_data) {
    (void)arg;(void)base;
    esp_mqtt_event_handle_t e=(esp_mqtt_event_handle_t)event_data;
    if(event_id==MQTT_EVENT_CONNECTED) {
        connected=true;
        char subscription[120];
        snprintf(subscription,sizeof(subscription),"%s#",state_prefix);
        esp_mqtt_client_subscribe(client,subscription,0);
        snprintf(subscription,sizeof(subscription),"%s+/set",display_prefix);
        esp_mqtt_client_subscribe(client,subscription,0);
        ESP_LOGI(TAG,"Connected. Subscribed state and display controls for %s",screen_name);
    }else if(event_id==MQTT_EVENT_DISCONNECTED){
        connected=false;
    }else if(event_id==MQTT_EVENT_DATA) {
        // MQTT fragmented payloads must not be mistaken for full values.
        if(e->current_data_offset==0 && e->data_len==e->total_data_len){
            incoming_state(e->topic,e->topic_len,e->data,e->data_len);
            incoming_display(e->topic,e->topic_len,e->data,e->data_len);
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
        .session.keepalive=30,
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
        char plaintext[769]={0};
        const char *value=stored;
        if(cJSON_IsString(t)&&t->valuestring&&!strcmp(t->valuestring,"crypted-text")){
            if(!mira_legacy_decrypt(stored,plaintext,sizeof(plaintext)))continue;
            value=plaintext;
        }
        if(key){
            size_t used=strlen(value),found=0;
            if(used>=size_limit)goto clear_value;
            if(!strcmp(key,"screen")){
                if(!used)goto clear_value;
                for(const char *q=value;*q;q++)
                    if(*q=='/'||*q=='#'||*q=='+')goto clear_value;
            }
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
    refresh_prefixes();
}
esp_err_t mira_mqtt_init(void) {
    import_beta_mqtt_config();
    read_config();
    start_client();
    ESP_LOGI(TAG,"Native MQTT %s (disabled until provisioned)",enabled?"enabled":"disabled");
    return ESP_OK;
}
bool mira_mqtt_publish_action(const char *name,const char *value) {
    if(!client||!connected||!enabled||!name||!value)return false;
    if(strlen(name)>95||strlen(value)>159)return false;
    char topic[240];snprintf(topic,sizeof(topic),"%s%s",action_prefix,name);
    // Only actual user LVGL event callbacks call this; MQTT State callbacks never do.
    return esp_mqtt_client_enqueue(client,topic,value,0,0,0,true)>=0;
}
static esp_err_t mqtt_get(httpd_req_t *r) {
    char out[128];
    snprintf(out,sizeof(out),"{\"enabled\":%s,\"connected\":%s,\"screen\":\"%s\"}",
             enabled?"true":"false",connected?"true":"false",screen_name);
    httpd_resp_set_type(r,"application/json");
    return httpd_resp_sendstr(r,out);
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
       !cJSON_IsString(n)||!n->valuestring||!n->valuestring[0]||
       strlen(n->valuestring)>=sizeof(screen_name)||
       !cJSON_IsNumber(p)||p->valueint<1||p->valueint>65535||
       (u&&(!cJSON_IsString(u)||strlen(u->valuestring)>=sizeof(username)))||
       (s&&(!cJSON_IsString(s)||strlen(s->valuestring)>=sizeof(secret)))||
       (!cJSON_IsBool(en)&&!cJSON_IsNumber(en))) {
        if(root)cJSON_Delete(root);
        httpd_resp_send_err(r,HTTPD_400_BAD_REQUEST,"Expected host, port, screen, enabled and optional username/password");
        return ESP_FAIL;
    }
    // Disable unsafe topic wildcards and path separators in the screen key.
    for(const char *q=n->valuestring;*q;q++){
        if(*q=='/'||*q=='+'||*q=='#'){
            cJSON_Delete(root);httpd_resp_send_err(r,HTTPD_400_BAD_REQUEST,"Invalid screen name");return ESP_FAIL;
        }
    }
    nvs_handle_t nv;
    esp_err_t err=nvs_open("mira_idf_mqtt",NVS_READWRITE,&nv);
    if(err!=ESP_OK){cJSON_Delete(root);httpd_resp_send_err(r,HTTPD_500_INTERNAL_SERVER_ERROR,"NVS unavailable");return ESP_FAIL;}
    err=nvs_set_str(nv,"host",h->valuestring);
    if(err==ESP_OK)err=nvs_set_str(nv,"screen",n->valuestring);
    if(err==ESP_OK)err=nvs_set_u16(nv,"port",(uint16_t)p->valueint);
    if(err==ESP_OK)err=nvs_set_u8(nv,"enabled",cJSON_IsTrue(en)|| (cJSON_IsNumber(en)&&en->valueint!=0));
    if(err==ESP_OK&&u)err=nvs_set_str(nv,"username",u->valuestring);
    if(err==ESP_OK&&s)err=nvs_set_str(nv,"secret",s->valuestring);
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
