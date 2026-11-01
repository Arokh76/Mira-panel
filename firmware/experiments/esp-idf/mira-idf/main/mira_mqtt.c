// Native ESP-IDF MQTT bridge for Mira 4.3C.
// States update only the LVGL task via its bounded queue; never emit actions.
#include "mira_mqtt.h"
#include "mira_full.h"
#include "mira_setup_access.h"
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
static char state_prefix[112], action_prefix[112];

static void refresh_prefixes(void) {
    snprintf(state_prefix,sizeof(state_prefix),"mira/%s/States/",screen_name);
    snprintf(action_prefix,sizeof(action_prefix),"mira/%s/Actions/",screen_name);
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
    if(tlen <= (int)prefix_len || tlen > (int)prefix_len + 95 || plen<0 || plen>159)return;
    if(strncmp(topic,state_prefix,prefix_len)!=0)return;
    char name[96]={0},value[160]={0};
    memcpy(name,topic+prefix_len,(size_t)tlen-prefix_len);
    memcpy(value,payload,(size_t)plen);
    if(value[0]=='{'){
        cJSON *root=cJSON_Parse(value);
        const cJSON *v=root?cJSON_GetObjectItemCaseSensitive(root,"value"):NULL;
        if(cJSON_IsString(v)&&v->valuestring)
            snprintf(value,sizeof(value),"%s",v->valuestring);
        else if(cJSON_IsNumber(v))
            snprintf(value,sizeof(value),"%.9g",v->valuedouble);
        else if(cJSON_IsBool(v))
            snprintf(value,sizeof(value),"%s",cJSON_IsTrue(v)?"1":"0");
        if(root)cJSON_Delete(root);
    }
    (void)mira_full_enqueue_state(name,value);
}
static void mqtt_event(void *arg, esp_event_base_t base,int32_t event_id,void *event_data) {
    (void)arg;(void)base;
    esp_mqtt_event_handle_t e=(esp_mqtt_event_handle_t)event_data;
    if(event_id==MQTT_EVENT_CONNECTED) {
        connected=true;
        char subscription[120];
        snprintf(subscription,sizeof(subscription),"%s#",state_prefix);
        esp_mqtt_client_subscribe(client,subscription,0);
        ESP_LOGI(TAG,"Connected. Subscribed states %s",subscription);
    }else if(event_id==MQTT_EVENT_DISCONNECTED){
        connected=false;
    }else if(event_id==MQTT_EVENT_DATA) {
        // MQTT fragmented payloads must not be mistaken for full values.
        if(e->current_data_offset==0 && e->data_len==e->total_data_len)
            incoming_state(e->topic,e->topic_len,e->data,e->data_len);
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
