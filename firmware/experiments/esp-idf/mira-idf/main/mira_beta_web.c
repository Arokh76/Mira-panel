// Native handlers for the actual firmware-beta web templates, NOT a new UI.
// The Arduino server is replaced with esp_http_server. HTML is copied verbatim
// from firmware-beta/HTML/HTML_Parametres.h and HTML_MQTT.h.
#include "mira_beta_web.h"
#include "mira_beta_web_pages.h"
#include "mira_beta_data.h"
#include "mira_full.h"
#include "mira_jeedom.h"
#include "mira_mqtt.h"
#include "mira_recovery.h"
#include "mira_display.h"
#include "mira_gpio43.h"
#include "mira_beta_networks.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "cJSON.h"
#include "nvs.h"
#include "mbedtls/base64.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <limits.h>

#define MIRA_WEB_FORM_LIMIT 16384
#define MIRA_WEB_JSON_LIMIT 49152
#define MIRA_WEB_PATH "/spiffs/miralvgl.json"
#define MIRA_WEB_TEMP "/spiffs/miralvgl.webnew"
static const char *TAG="mira-beta-web";
static uint32_t network_action_nonce;
static int64_t network_nonce_time;

static esp_err_t html_err(httpd_req_t *req,httpd_err_code_t code,const char *msg){
    return httpd_resp_send_err(req,code,msg);
}
bool mira_beta_web_authorized(httpd_req_t *req){
    nvs_handle_t nv;
    char key[97]={0};
    size_t key_len=sizeof(key);
    bool have=false;
    if(nvs_open("mira_idf_auth",NVS_READONLY,&nv)==ESP_OK){
        have=nvs_get_str(nv,"jeedom_key",key,&key_len)==ESP_OK&&key[0]!=0;
        nvs_close(nv);
    }
    size_t len=httpd_req_get_hdr_value_len(req,"Authorization");
    char header[280]={0};
    bool match=false;
    if(have&&len>6&&len<sizeof(header) &&
       httpd_req_get_hdr_value_str(req,"Authorization",header,sizeof(header))==ESP_OK &&
       !strncmp(header,"Basic ",6)){
        unsigned char raw[200]={0};
        size_t bytes=0;
        if(mbedtls_base64_decode(raw,sizeof(raw)-1,&bytes,
                (const unsigned char *)header+6,strlen(header+6))==0){
            char expected[105];
            int n=snprintf(expected,sizeof(expected),"admin:%s",key);
            if(n>0&&(size_t)n==bytes){
                unsigned char different=0;
                for(size_t i=0;i<bytes;i++)different|=(unsigned char)(raw[i]^(unsigned char)expected[i]);
                match=different==0;
            }
            memset(expected,0,sizeof(expected));
        }
        memset(raw,0,sizeof(raw));
    }
    memset(key,0,sizeof(key));
    if(match)return true;
    httpd_resp_set_status(req,"401 Unauthorized");
    httpd_resp_set_hdr(req,"WWW-Authenticate","Basic realm=\"Mira Panel\", charset=\"UTF-8\"");
    httpd_resp_set_type(req,"text/plain; charset=utf-8");
    httpd_resp_sendstr(req,"Authentification Mira Panel requise");
    return false;
}
static int hexval(unsigned char c){
    if(c>='0'&&c<='9')return c-'0';
    if(c>='a'&&c<='f')return c-'a'+10;
    if(c>='A'&&c<='F')return c-'A'+10;
    return -1;
}
static bool decode_form(char *s){
    char *src=s,*dst=s;
    while(*src){
        unsigned char c=(unsigned char)*src++;
        if(c=='%'){
            if(!src[0]||!src[1])return false;
            int hi=hexval((unsigned char)src[0]),lo=hexval((unsigned char)src[1]);
            if(hi<0||lo<0)return false;
            c=(unsigned char)(hi*16+lo);
            src+=2;
        }else if(c=='+')c=' ';
        if(c==0||c=='\r'||c=='\n'||c==0x7f)return false;
        *dst++=(char)c;
    }
    *dst=0;
    return true;
}
static cJSON *read_form(httpd_req_t *req){
    size_t len=req->content_len;
    if(!len||len>MIRA_WEB_FORM_LIMIT)return NULL;
    char *buf=malloc(len+1);
    if(!buf)return NULL;
    size_t got=0;
    while(got<len){
        int n=httpd_req_recv(req,buf+got,len-got);
        if(n<=0){free(buf);return NULL;}
        got+=(size_t)n;
    }
    buf[len]=0;
    cJSON *form=cJSON_CreateObject();
    if(!form){free(buf);return NULL;}
    char *next=NULL;
    for(char *pair=strtok_r(buf,"&",&next);pair;pair=strtok_r(NULL,"&",&next)){
        char *sep=strchr(pair,'=');
        if(!sep){cJSON_Delete(form);free(buf);return NULL;}
        *sep++=0;
        if(!decode_form(pair)||!decode_form(sep)||!pair[0]||strlen(pair)>96||strlen(sep)>768){
            cJSON_Delete(form);free(buf);return NULL;
        }
        cJSON *value=cJSON_CreateString(sep);
        if(!value){cJSON_Delete(form);free(buf);return NULL;}
        if(cJSON_HasObjectItem(form,pair))cJSON_ReplaceItemInObjectCaseSensitive(form,pair,value);
        else cJSON_AddItemToObject(form,pair,value);
    }
    free(buf);
    return form;
}
static const char *field(cJSON *form,const char *key){
    const cJSON *v=cJSON_GetObjectItemCaseSensitive(form,key);
    return cJSON_IsString(v)?v->valuestring:NULL;
}
static bool decimal(const char *s,unsigned max,unsigned *out){
    if(!s||!*s)return false;
    unsigned val=0;
    for(const unsigned char *p=(const unsigned char *)s;*p;p++){
        if(*p<'0'||*p>'9')return false;
        unsigned digit=(unsigned)(*p-'0');
        if(digit>max||val>(max-digit)/10)return false;
        val=val*10+digit;
    }
    if(val>max)return false;
    *out=val;
    return true;
}
static esp_err_t chunk(httpd_req_t *r,const char *s){
    // HTTP chunked transfer: a zero-length chunk TERMINATES the response.
    // Empty beta placeholders (e.g. unchecked MQTTCHECKED, blank credentials)
    // must emit nothing, not the final chunk of the HTML page.
    if(!s||!*s)return ESP_OK;
    return httpd_resp_send_chunk(r,s,HTTPD_RESP_USE_STRLEN);
}
static esp_err_t escaped(httpd_req_t *r,const char *s){
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
            if(p>start && httpd_resp_send_chunk(r,start,p-start)!=ESP_OK)return ESP_FAIL;
            if(!*p)break;
            if(chunk(r,entity)!=ESP_OK)return ESP_FAIL;
            start=p+1;
        }
    }
    return ESP_OK;
}
static cJSON *read_widgets(void){
    FILE *f=fopen(MIRA_WEB_PATH,"rb");
    if(!f)return NULL;
    if(fseek(f,0,SEEK_END)){fclose(f);return NULL;}
    long size=ftell(f);
    if(size<2||size>MIRA_WEB_JSON_LIMIT){fclose(f);return NULL;}
    rewind(f);
    char *buf=malloc((size_t)size+1);
    if(!buf){fclose(f);return NULL;}
    size_t got=fread(buf,1,(size_t)size,f);
    fclose(f);
    if(got!=(size_t)size){free(buf);return NULL;}
    buf[size]=0;
    cJSON *root=cJSON_ParseWithLength(buf,(size_t)size);
    free(buf);
    if(root&&!cJSON_IsArray(cJSON_GetObjectItemCaseSensitive(root,"lvgl"))){
        cJSON_Delete(root);return NULL;
    }
    return root;
}
static esp_err_t widget_rows(httpd_req_t *r,bool mqtt){
    cJSON *root=read_widgets();
    const cJSON *arr=root?cJSON_GetObjectItemCaseSensitive(root,"lvgl"):NULL,*item=NULL;
    bool any=false;
    cJSON_ArrayForEach(item,arr){
        const cJSON *n=cJSON_GetObjectItemCaseSensitive(item,"name");
        const cJSON *t=cJSON_GetObjectItemCaseSensitive(item,"type");
        const cJSON *u=cJSON_GetObjectItemCaseSensitive(item,"url");
        const char *name=cJSON_IsString(n)?n->valuestring:"";
        const char *type=cJSON_IsString(t)?t->valuestring:"";
        const char *url=cJSON_IsString(u)?u->valuestring:"";
        if(!mqtt&&(!strcmp(type,"datetime")||!strcmp(type,"info")))continue;
        if(!*name&&strcmp(type,"datetime"))continue;
        any=true;
        if(mqtt){
            const char *label=type;
            if(!strcmp(type,"button")||!strcmp(type,"little_button"))label="Bouton";
            else if(!strcmp(type,"toggle")||!strcmp(type,"toogle"))label="Interrupteur";
            else if(!strcmp(type,"range")||!strcmp(type,"roundrange"))label="Curseur";
            else if(!strcmp(type,"list")||!strcmp(type,"listbutton"))label="Liste";
            else if(!strcmp(type,"color"))label="Couleur";
            else if(!strcmp(type,"info"))label="Information";
            else if(!strcmp(type,"datetime"))label="Date / heure";
            if(chunk(r,"<tr><td>")!=ESP_OK||
               escaped(r,*name?name:"Date et heure")!=ESP_OK||
               chunk(r,"</td><td>")!=ESP_OK||escaped(r,label)!=ESP_OK||
               chunk(r,"</td></tr>")!=ESP_OK)break;
        }else{
            if(chunk(r,"<div class=\"field\"><label>")!=ESP_OK||escaped(r,name)!=ESP_OK||
               chunk(r,"</label><input id=\"")!=ESP_OK||escaped(r,name)!=ESP_OK||
               chunk(r,"\" name=\"")!=ESP_OK||escaped(r,name)!=ESP_OK||
               chunk(r,"\" value=\"")!=ESP_OK||escaped(r,url)!=ESP_OK||
               chunk(r,"\" type=\"text\" placeholder=\"URL d'envoi d'état\"></div>")!=ESP_OK)break;
        }
    }
    if(root)cJSON_Delete(root);
    if(!any)return chunk(r,mqtt?"<tr><td colspan='2'>Aucun widget programme</td></tr>":
                      "<div class=\"empty\">Aucun widget avec retour HTTP n'est actuellement déclaré.</div>");
    return ESP_OK;
}
static void nvs_text(const char *ns,const char *key,const char *fallback,char *dst,size_t cap){
    snprintf(dst,cap,"%s",fallback?fallback:"");
    nvs_handle_t nv;
    if(nvs_open(ns,NVS_READONLY,&nv)==ESP_OK){
        size_t len=cap;
        (void)nvs_get_str(nv,key,dst,&len);
        nvs_close(nv);
    }
}
/* Keep the beta Wi-Fi editor in sync with the atomic native IP record.
 * Old per-field NVS keys still render if an older IDF build saved them. */
static bool atomic_web_field(const char *key,char *out,size_t cap){
    if(!key||!out||!cap)return false;
    nvs_handle_t nv;
    if(nvs_open("mira_idf_net",NVS_READONLY,&nv)!=ESP_OK)return false;
    char profile[512]={0};
    size_t n=sizeof(profile);
    bool stored=nvs_get_str(nv,"web_profile",profile,&n)==ESP_OK;
    nvs_close(nv);
    if(!stored)return false;
    cJSON *root=cJSON_Parse(profile);
    if(!root)return false;
    const cJSON *field=cJSON_GetObjectItemCaseSensitive(root,key);
    const bool ok=cJSON_IsString(field)&&field->valuestring&&
                  strlen(field->valuestring)<cap;
    if(ok)snprintf(out,cap,"%s",field->valuestring);
    cJSON_Delete(root);
    return ok;
}
static unsigned nvs_number(const char *ns,const char *key,unsigned fallback){
    nvs_handle_t nv;
    if(nvs_open(ns,NVS_READONLY,&nv)!=ESP_OK)return fallback;
    uint32_t v=0;uint8_t small=0;uint16_t medium=0;
    esp_err_t err=nvs_get_u8(nv,key,&small);
    if(err==ESP_OK)v=small;
    else if(nvs_get_u16(nv,key,&medium)==ESP_OK)v=medium;
    else if(nvs_get_u32(nv,key,&v)!=ESP_OK)v=fallback;
    nvs_close(nv);
    return v;
}
/* Wi-Fi discovery is never performed in esp_http_server's single
 * request thread. The old esp_wifi_scan_start(..., true) here stopped
 * rendering until channel scanning finished, delaying ALL Web requests.
 * Keep at most ten non-sensitive SSIDs and return instantly from HTML. */
#define MIRA_SCAN_MAX 10
static portMUX_TYPE wifi_scan_mux=portMUX_INITIALIZER_UNLOCKED;
static wifi_ap_record_t wifi_scan_results[MIRA_SCAN_MAX];
static uint16_t wifi_scan_count;
static bool wifi_scan_busy;
static bool wifi_scan_finished;
static bool wifi_scan_failed;
static int64_t wifi_scan_finished_us;
static void wifi_scan_worker(void *unused){
    (void)unused;
    wifi_scan_config_t scan={0};
    wifi_ap_record_t found[MIRA_SCAN_MAX]={0};
    uint16_t n=MIRA_SCAN_MAX;
    esp_err_t err=esp_wifi_scan_start(&scan,true);
    if(err==ESP_OK)err=esp_wifi_scan_get_ap_records(&n,found);
    if(err!=ESP_OK){
        ESP_LOGW(TAG,"Background Wi-Fi scan unavailable: %s",esp_err_to_name(err));
        n=0;
    }
    taskENTER_CRITICAL(&wifi_scan_mux);
    memcpy(wifi_scan_results,found,sizeof(found));
    wifi_scan_count=n;
    wifi_scan_failed=err!=ESP_OK;
    wifi_scan_finished_us=esp_timer_get_time();
    wifi_scan_finished=true;
    wifi_scan_busy=false;
    taskEXIT_CRITICAL(&wifi_scan_mux);
    vTaskDelete(NULL);
}
static void wifi_scan_start_background(void){
    bool start=false;
    const int64_t now=esp_timer_get_time();
    taskENTER_CRITICAL(&wifi_scan_mux);
    if(!wifi_scan_busy&&(!wifi_scan_finished||
                         now-wifi_scan_finished_us>60000000LL)){
        wifi_scan_busy=true;
        start=true;
    }
    taskEXIT_CRITICAL(&wifi_scan_mux);
    if(start&&xTaskCreate(wifi_scan_worker,"mira_scan",4096,NULL,2,NULL)!=pdPASS){
        taskENTER_CRITICAL(&wifi_scan_mux);
        wifi_scan_busy=false;
        wifi_scan_failed=true;
        wifi_scan_finished=true;
        wifi_scan_finished_us=esp_timer_get_time();
        taskEXIT_CRITICAL(&wifi_scan_mux);
        ESP_LOGW(TAG,"Unable to start background Wi-Fi scan worker");
    }
}
esp_err_t mira_beta_web_wifi_scan_get(httpd_req_t *req){
    wifi_scan_start_background();
    char ssids[MIRA_SCAN_MAX][33]={{0}};
    uint16_t n=0;
    bool busy=false,failed=false;
    taskENTER_CRITICAL(&wifi_scan_mux);
    busy=wifi_scan_busy;
    failed=wifi_scan_failed;
    if(!busy){
        n=wifi_scan_count;
        for(uint16_t i=0;i<n;i++)
            memcpy(ssids[i],wifi_scan_results[i].ssid,32);
    }
    taskEXIT_CRITICAL(&wifi_scan_mux);
    cJSON *root=cJSON_CreateObject();
    cJSON *arr=root?cJSON_AddArrayToObject(root,"networks"):NULL;
    if(!arr){if(root)cJSON_Delete(root);return html_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Scan Wi-Fi indisponible");}
    cJSON_AddBoolToObject(root,"busy",busy);
    cJSON_AddBoolToObject(root,"failed",!busy&&failed);
    for(uint16_t i=0;i<n;i++){
        if(!ssids[i][0])continue; // Hidden networks remain manually configurable.
        if(!cJSON_AddItemToArray(arr,cJSON_CreateString(ssids[i]))){
            cJSON_Delete(root);
            return html_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Mémoire scan Wi-Fi insuffisante");
        }
    }
    char *json=cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if(!json)return html_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Mémoire scan Wi-Fi insuffisante");
    httpd_resp_set_type(req,"application/json");
    httpd_resp_set_hdr(req,"Cache-Control","no-store");
    const esp_err_t sent=httpd_resp_sendstr(req,json);
    cJSON_free(json);
    return sent;
}
esp_err_t mira_beta_web_wifi_choices(httpd_req_t *req){
    wifi_scan_start_background();
    return chunk(req,
        "<label for=\"RS\">Réseaux détectés</label>"
        "<select id=\"RS\" name=\"RS\" class=\"box\" onchange=\"Click(this.value)\">"
        "<option value=\"\">Saisir le SSID manuellement (recherche en cours)</option></select>"
        "<script>(function(){"
        "var s=document.getElementById('RS'),attempts=0;if(!s)return;"
        "function refresh(){fetch('/mira/wifi/scan',{cache:'no-store'})"
        ".then(function(r){if(!r.ok)throw Error('scan');return r.json();})"
        ".then(function(d){"
        "if(d.busy&&attempts++<20){setTimeout(refresh,750);return;}"
        "s.options.length=1;s.options[0].text='Saisir le SSID manuellement';"
        "(d.networks||[]).forEach(function(name){"
        "var o=document.createElement('option');o.value=name;o.textContent=name;s.add(o);"
        "});"
        "if(d.failed)s.options[0].text='Recherche indisponible : saisir le SSID';"
        "}).catch(function(){s.options[0].text='Saisir le SSID manuellement';});"
        "}refresh();})();</script>");
}
static esp_err_t wifi_choices(httpd_req_t *req){
    return mira_beta_web_wifi_choices(req);
}
static int network_id(httpd_req_t *req){
    char query[160]={0},number[10]={0};
    if(httpd_req_get_url_query_str(req,query,sizeof(query))!=ESP_OK||
       httpd_query_key_value(query,"network",number,sizeof(number))!=ESP_OK||
       !*number)return -1;
    unsigned id=0;
    if(!decimal(number,999,&id))return -1;
    return (int)id;
}
static esp_err_t registered_network_rows(httpd_req_t *req){
    // Repair older first-run IDF installs without touching deleted profiles.
    esp_err_t recovered=mira_beta_networks_recover_station();
    if(recovered!=ESP_OK)
        ESP_LOGW(TAG,"Saved Wi-Fi list recovery unavailable: %s",esp_err_to_name(recovered));
    mira_beta_network_t *profiles=calloc(MIRA_NET_MAX,sizeof(*profiles));
    if(!profiles)return chunk(req,"<div class=\"empty\">Mémoire réseaux insuffisante.</div>");
    size_t count=0;
    if(mira_beta_networks_list(profiles,&count,MIRA_NET_MAX)!=ESP_OK){
        free(profiles);
        return chunk(req,"<div class=\"empty\">Impossible de lire les réseaux.</div>");
    }
    if(!count){
        free(profiles);
        return chunk(req,"<div class=\"empty\">Aucun réseau n'est enregistré.</div>");
    }
    // Match the original WT32 beta: a saved network is a clickable row
    // leading to edit/delete. Do not add a separate "Activer" command,
    // especially for the network already in use.
    for(size_t i=0;i<count;i++){
        char head[210]={0};
        int id=profiles[i].id;
        snprintf(head,sizeof(head),
                 "<div class=\"network-item\"><span class=\"network-id\">%d</span>"
                 "<button type=\"button\" class=\"network-button\" onclick=\"window.location.href='/load?nav=update&amp;network=%d'\">",
                 id,id);
        if(chunk(req,head)!=ESP_OK||escaped(req,profiles[i].ssid)!=ESP_OK||
           chunk(req,"</button></div>")!=ESP_OK){free(profiles);return ESP_FAIL;}
    }
    free(profiles);
    return ESP_OK;
}
static esp_err_t registered_network_field(httpd_req_t *req,const char *name){
    const int id=network_id(req);
    mira_beta_network_t profile={0};
    if(id<0||mira_beta_networks_get(id,&profile)!=ESP_OK)return ESP_OK;
    const char *val="";
    if(!strcmp(name,"SSIDUPDATE"))val=profile.ssid;
    else if(!strcmp(name,"PASSUPDATE"))val=""; // keep encrypted secret hidden
    else if(!strcmp(name,"IPMODEUPDATE"))val=profile.ip_mode;
    else if(!strcmp(name,"IPUPDATE"))val=profile.ip;
    else if(!strcmp(name,"MASKUPDATE"))val=profile.mask;
    else if(!strcmp(name,"GATEWAYUPDATE"))val=profile.gateway;
    else if(!strcmp(name,"DNS1UPDATE"))val=profile.dns1;
    else if(!strcmp(name,"DNS2UPDATE"))val=profile.dns2;
    return escaped(req,val);
}
static esp_err_t replacement(httpd_req_t *r,const char *name){
    static const struct {const char *slot,*key;unsigned fallback;} display[]={
        {"DISPLAYDAY","day_pct",85},{"DISPLAYNIGHT","night_pct",20},
        {"DISPLAYSLEEPTIMEOUT","sleep_delay",120},{"DISPLAYSLEEPBRIGHT","sleep_light",8},
        {"DISPLAYNIGHTCHECKED","night_auto",0},{"DISPLAYCLOCKCHECKED","sleep_clock",1}
    };
    if(!strcmp(name,"RESEAUXENREG"))return registered_network_rows(r);
    if(!strcmp(name,"SSIDUPDATE")||!strcmp(name,"PASSUPDATE")||
       !strcmp(name,"IPMODEUPDATE")||!strcmp(name,"IPUPDATE")||
       !strcmp(name,"MASKUPDATE")||!strcmp(name,"GATEWAYUPDATE")||
       !strcmp(name,"DNS1UPDATE")||!strcmp(name,"DNS2UPDATE"))
        return registered_network_field(r,name);
    if(!strcmp(name,"MDPAP")){
        char key[97]={0};size_t n=sizeof(key);
        nvs_handle_t nv;
        if(nvs_open("mira_idf_auth",NVS_READONLY,&nv)==ESP_OK){
            (void)nvs_get_str(nv,"jeedom_key",key,&n);
            nvs_close(nv);
        }
        esp_err_t result=escaped(r,key);
        memset(key,0,sizeof(key));
        return result;
    }
    if(!strcmp(name,"IPAP")){
        char ip[32]={0};
        nvs_text("mira_idf_net","ap_ip","192.168.4.1",ip,sizeof(ip));
        return escaped(r,ip);
    }
    if(!strcmp(name,"ACTIFAP")){
        const bool active=nvs_number("mira_idf_net","ap_enabled",0)!=0;
        return chunk(r,active?
            "<div class=\"switchline\"><div><strong>Point d'accès</strong><div class=\"hint\" id=\"texte\">Actif</div></div>"
            "<input type=\"checkbox\" onclick=\"ClickToogle(this)\" checked></div>"
            "<input class=\"hide\" id=\"actif\" name=\"actif\" value=\"1\" type=\"text\">":
            "<div class=\"switchline\"><div><strong>Point d'accès</strong><div class=\"hint\" id=\"texte\">Inactif</div></div>"
            "<input type=\"checkbox\" onclick=\"ClickToogle(this)\"></div>"
            "<input class=\"hide\" id=\"actif\" name=\"actif\" value=\"0\" type=\"text\">");
    }
    if(!strcmp(name,"SCANRESEAUX"))return wifi_choices(r);
    if(!strcmp(name,"SSID")){
        wifi_config_t sta={0};
        char ssid[33]={0};
        if(esp_wifi_get_config(WIFI_IF_STA,&sta)==ESP_OK)
            memcpy(ssid,sta.sta.ssid,sizeof(sta.sta.ssid));
        return escaped(r,ssid);
    }
    if(!strcmp(name,"IPMODE")){
        char value[24];
        if(!atomic_web_field("mode",value,sizeof(value)))
            nvs_text("mira_idf_net","web_mode","auto",value,sizeof(value));
        return escaped(r,value);
    }
    static const struct {const char *slot,*key,*atom;} network_fields[]={
        {"IP","web_ip","ip"},{"MASK","web_mask","mask"},
        {"GATEWAY","web_gw","gateway"},{"DNS1","web_dns1","dns1"},
        {"DNS2","web_dns2","dns2"}
    };
    for(size_t i=0;i<sizeof(network_fields)/sizeof(network_fields[0]);i++){
        if(!strcmp(name,network_fields[i].slot)){
            char value[40]={0};
            if(!atomic_web_field(network_fields[i].atom,value,sizeof(value)))
                nvs_text("mira_idf_net",network_fields[i].key,"",value,sizeof(value));
            return escaped(r,value);
        }
    }
    if(!strcmp(name,"WIFIACTIVE")){
        wifi_ap_record_t ap={0};
        if(esp_wifi_sta_get_ap_info(&ap)!=ESP_OK)return chunk(r,"Wi-Fi non connecté");
        esp_netif_t *netif=esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        esp_netif_ip_info_t ip={0};
        if(!netif||esp_netif_get_ip_info(netif,&ip)!=ESP_OK)return chunk(r,"Wi-Fi connecté");
        char state[130]={0};
        snprintf(state,sizeof(state),"IP " IPSTR " | RSSI %d dBm",IP2STR(&ip.ip),(int)ap.rssi);
        return escaped(r,state);
    }
    if(!strcmp(name,"VERSION"))return chunk(r,"0.7.37-IDF-beta");
    if(!strcmp(name,"LOCALIP")){
        esp_netif_t *netif=esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        esp_netif_ip_info_t ip={0};
        if(!netif||esp_netif_get_ip_info(netif,&ip)!=ESP_OK)
            return chunk(r,"Non connecté");
        char label[25]={0};
        snprintf(label,sizeof(label),IPSTR,IP2STR(&ip.ip));
        return escaped(r,label);
    }
    if(!strcmp(name,"APP"))return chunk(r,"Mira Panel");
    if(!strcmp(name,"LIGHTCOLOR"))return chunk(r,"#7193A0");
    if(!strcmp(name,"DARKCOLOR"))return chunk(r,"#1E5064");
    if(!strcmp(name,"ACTIVECOLOR"))return chunk(r,"#D39E55");
    if(!strcmp(name,"TEXTCOLOR"))return chunk(r,"#FFFFFF");
    if(!strcmp(name,"ACTIONS"))return mira_jeedom_render_beta_actions(r);
    if(!strcmp(name,"PARAMS"))return widget_rows(r,false);
    if(!strcmp(name,"MQTTTABLE"))return widget_rows(r,true);
    if(!strcmp(name,"DISPLAYNIGHTSTART")||!strcmp(name,"DISPLAYNIGHTEND")){
        char value[16]={0};
        nvs_text("mira_idf_ui",!strcmp(name,"DISPLAYNIGHTSTART")?"night_start":"night_end",
                 !strcmp(name,"DISPLAYNIGHTSTART")?"22:00":"07:00",value,sizeof(value));
        return escaped(r,value);
    }
    for(size_t i=0;i<sizeof(display)/sizeof(display[0]);i++){
        if(!strcmp(display[i].slot,name)){
            unsigned n=nvs_number("mira_idf_ui",display[i].key,display[i].fallback);
            if(strstr(name,"CHECKED"))return chunk(r,n?"checked":"");
            char value[16];snprintf(value,sizeof(value),"%u",n);
            return chunk(r,value);
        }
    }
    if(!strcmp(name,"MQTTCHECKED")||!strcmp(name,"MQTTACTIF")){
        unsigned v=nvs_number("mira_idf_mqtt","enabled",0);
        return chunk(r,!strcmp(name,"MQTTCHECKED")?(v?"checked":""):(v?"1":"0"));
    }
    if(!strcmp(name,"MQTTPASS"))return ESP_OK; // never echo stored MQTT passwords into HTML
    static const struct {const char *slot,*key,*fallback;} mqtt[]={
        {"MQTTNOM","screen","panel"},{"MQTTSERVEUR","host",""},
        {"MQTTPORT",NULL,"1883"},{"MQTTUSER","username",""}
    };
    for(size_t i=0;i<sizeof(mqtt)/sizeof(mqtt[0]);i++){
        if(!strcmp(name,mqtt[i].slot)){
            char value[160]={0};
            if(mqtt[i].key)nvs_text("mira_idf_mqtt",mqtt[i].key,mqtt[i].fallback,value,sizeof(value));
            else snprintf(value,sizeof(value),"%u",nvs_number("mira_idf_mqtt","port",1883));
            return escaped(r,value);
        }
    }
    ESP_LOGW(TAG,"Beta template placeholder not mapped: %s",name);
    return ESP_OK;
}
static esp_err_t render(httpd_req_t *r,const char *page){
    httpd_resp_set_type(r,"text/html; charset=utf-8");
    httpd_resp_set_hdr(r,"Cache-Control","no-store");
    const char *from=page,*p=page;
    while(*p){
        if(*p!='%'){p++;continue;}
        if(p>from&&httpd_resp_send_chunk(r,from,p-from)!=ESP_OK)return ESP_FAIL;
        if(p[1]=='%'){
            if(chunk(r,"%")!=ESP_OK)return ESP_FAIL;
            p+=2;from=p;continue;
        }
        const char *end=strchr(p+1,'%');
        if(!end||end-p>42){
            if(chunk(r,"%")!=ESP_OK)return ESP_FAIL;
            p++;from=p;continue;
        }
        char token[44]={0};
        memcpy(token,p+1,(size_t)(end-p-1));
        bool placeholder=token[0]!=0;
        for(const char *t=token;*t;t++){
            if(!((*t>='A'&&*t<='Z')||(*t>='0'&&*t<='9')||*t=='_')){
                placeholder=false;break;
            }
        }
        if(!placeholder){
            if(chunk(r,"%")!=ESP_OK)return ESP_FAIL;
            p++;from=p;continue;
        }
        if(replacement(r,token)!=ESP_OK)return ESP_FAIL;
        p=end+1;from=p;
    }
    if(p>from&&httpd_resp_send_chunk(r,from,p-from)!=ESP_OK)return ESP_FAIL;
    return httpd_resp_send_chunk(r,NULL,0);
}
// The original beta stores IDNetwork as a server global. Preserve the HTML
// exactly, but bind its form/link to the explicit ?network= id of this request.
// This avoids cross-client edits or deleting another network accidentally.
static esp_err_t render_network_edit(httpd_req_t *req){
    int id=network_id(req);
    mira_beta_network_t profile;
    if(id<0||mira_beta_networks_get(id,&profile)!=ESP_OK)
        return html_err(req,HTTPD_404_NOT_FOUND,"Réseau enregistré introuvable");
    const char *source=mira_beta_edit_network_html;
    const char *const original_form="action=\"/save?update=wifi\"";
    const char *const original_delete="window.location.href='/load?nav=removenetwork'";
    const char *form=strstr(source,original_form),*del=strstr(source,original_delete);
    if(!form||!del||del<form)
        return html_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Modèle réseau beta invalide");
    size_t n=strlen(source);
    char *patched=malloc(n+140);
    if(!patched)return html_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Mémoire insuffisante");
    size_t before=(size_t)(form-source);
    memcpy(patched,source,before);
    int put=snprintf(patched+before,n+140-before,
                     "action=\"/save?update=wifi&amp;network=%d\"",id);
    if(put<0||put>=140){free(patched);return ESP_FAIL;}
    size_t length=before+(size_t)put;
    const char *after=form+strlen(original_form);
    size_t middle=(size_t)(del-after);
    memcpy(patched+length,after,middle);
    length+=middle;
    put=snprintf(patched+length,n+140-length,
                 "window.location.href='/load?nav=removenetwork&network=%d'",id);
    if(put<0||put>=140){free(patched);return ESP_FAIL;}
    length+=(size_t)put;
    snprintf(patched+length,n+140-length,"%s",del+strlen(original_delete));
    esp_err_t result=render(req,patched);
    free(patched);
    return result;
}
static esp_err_t beta_network_remove_confirm(httpd_req_t *req){
    int id=network_id(req);
    mira_beta_network_t profile={0};
    if(id<0||mira_beta_networks_get(id,&profile)!=ESP_OK)
        return html_err(req,HTTPD_404_NOT_FOUND,"Réseau introuvable");
    network_action_nonce=esp_random();
    if(!network_action_nonce)network_action_nonce=1;
    network_nonce_time=esp_timer_get_time();
    char body[800]={0};
    int n=snprintf(body,sizeof(body),
       "<!doctype html><html lang=\"fr\"><meta charset=\"utf-8\">"
       "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
       "<title>Mira Panel - Supprimer réseau</title><body>"
       "<h1>Supprimer ce réseau enregistré ?</h1>"
       "<p>Identifiant %d. Ce réseau ne sera plus proposé dans Mira Panel.</p>"
       "<form method=\"POST\" action=\"/mira/network?action=remove&amp;network=%d\">"
       "<input type=\"hidden\" name=\"nonce\" value=\"%08lx\">"
       "<button type=\"submit\">Confirmer la suppression</button>"
       "</form><p><a href=\"/load?nav=listnetwork\">Annuler</a></p></body></html>",
       id,id,(unsigned long)network_action_nonce);
    if(n<0||n>=(int)sizeof(body))return ESP_FAIL;
    httpd_resp_set_type(req,"text/html; charset=utf-8");
    httpd_resp_set_hdr(req,"Cache-Control","no-store");
    return httpd_resp_sendstr(req,body);
}
/* Original firmware-beta: /load?nav=checkupdate.
 * Keep the real beta update-check button and the manual .bin installer,
 * but only ask the native IDF target manifest, never the Arduino release.
 */
static esp_err_t beta_checkupdate_page(httpd_req_t *req){
    httpd_resp_set_type(req,"text/html; charset=utf-8");
    httpd_resp_set_hdr(req,"Cache-Control","no-store");
    return httpd_resp_sendstr(req,
      "<!DOCTYPE html><html lang=\"fr\"><head><meta charset=\"utf-8\">"
      "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
      "<title>Mira Panel - Mise à jour</title><style>"
      ":root{--bg:#7193A0;--panel:#1E5064;--active:#D39E55}"
      "body{margin:0;background:var(--bg);font-family:Arial,sans-serif;color:#263238}"
      "header{background:var(--panel);color:white;padding:20px;font-weight:800;font-size:22px}"
      "main{max-width:800px;margin:28px auto;padding:25px;background:white;border-radius:20px}"
      "p{line-height:1.5}a{color:var(--panel)}"
      "button,input[type=submit]{background:var(--active);border:0;padding:12px 18px;"
      "border-radius:12px;color:white;font-size:16px;font-weight:bold;cursor:pointer}"
      ".status{background:#f5f8f9;border:1px solid #d9e2e6;padding:16px;"
      "border-radius:12px;white-space:pre-wrap}"
      "form{margin:15px 0}input[type=file]{max-width:100%}</style></head><body>"
      "<header>Mira Panel</header><main>"
      "<p><a href=\"/load?nav=system\">Retour au système</a></p>"
      "<h1>Mise à jour</h1>"
      "<p>Cette version utilise ESP-IDF 5.5.6 sur Waveshare 4.3C. "
      "Une mise à jour Arduino/WT32 ne doit jamais être installée ici.</p>"
      "<button type=\"button\" id=\"check\">Vérifier les mises à jour</button>"
      " <button type=\"button\" id=\"install\" disabled>Installer la mise à jour IDF</button>"
      "<p id=\"installResult\" class=\"status\" aria-live=\"polite\">"
      "Aucune mise à jour automatique lancée.</p>"
      "<p id=\"result\" class=\"status\" aria-live=\"polite\">Vérification non lancée.</p>"
      "<h2>Mise à jour manuelle</h2>"
      "<p>Sélectionne exclusivement un fichier OTA ESP-IDF 4.3C compatible.</p>"
      "<form method=\"POST\" action=\"/doUpdateFile\" enctype=\"multipart/form-data\">"
      "<input type=\"file\" name=\"update\" accept=\".bin\" required>"
      "<input type=\"submit\" value=\"Installer le fichier .bin\"></form>"
      "</main><script>"
      "async function check(){var e=document.getElementById('result');"
      "e.textContent='Vérification GitHub en cours...';"
      "try{var r=await fetch('/api/checkupdate',{cache:'no-store'});"
      "if(!r.ok)throw new Error('HTTP '+r.status);"
      "var j=await r.json();"
      "e.textContent=(j.message||j.status||'Statut inconnu')+"
      "(j.remote?'\\nVersion distante : '+j.remote:'');"
      "document.getElementById('install').disabled=j.status!=='available';"
      "}catch(err){e.textContent='Vérification impossible : '+err.message;}}"
      "document.getElementById('check').addEventListener('click',check);"
      "document.getElementById('install').onclick=async()=>{"
      "if(!confirm('Installer le firmware ESP-IDF officiel pour Waveshare 4.3C ?'))return;"
      "document.getElementById('install').disabled=true;"
      "let e=document.getElementById('installResult');"
      "try{let r=await fetch('/updatemodule',{method:'POST',"
      "credentials:'same-origin',headers:{'X-Mira-OTA':'1'}});"
      "if(!r.ok)throw Error('HTTP '+r.status+' : '+await r.text());"
      "e.textContent='Installation IDF lancée...';"
      "let timer=setInterval(async()=>{try{let state=await fetch('/api/otastatus',"
      "{cache:'no-store'});if(!state.ok)throw Error('HTTP '+state.status);"
      "let j=await state.json();e.textContent=j.message+' ('+j.received+'/'+j.total+' octets)';"
      "if(j.step==='error'||j.step==='reboot'){clearInterval(timer);"
      "if(j.step==='error')document.getElementById('install').disabled=false;}"
      "}catch(err){clearInterval(timer);e.textContent='Suivi OTA impossible : '+err.message;}},1600);"
      "}catch(err){e.textContent='Installation refusée : '+err.message;"
      "document.getElementById('install').disabled=false;}};"
      "check();</script></body></html>");
}
/* Hardware-specific adaptation of firmware-beta/HTML/HTML_GPIO.h.
 * Unlike WT32 EXT general-purpose GPIOs, Waveshare 4.3C exposes
 * 2 isolated digital inputs + 2 isolated outputs through CH32 @0x24.
 * The display driver owns the shared bus/output shadow.
 */
/* Four beta HTTP GPIO contracts, adapted to DI0/DI1 + DO0/DO1. Never
 * interpret the WT32 physical pin numbers 10/11/12... as CH32 channels.
 * Basic auth and a custom header are required for every state-changing POST.
 */
static bool gpio_post_authorized(httpd_req_t *req){
    if(!mira_beta_web_authorized(req))return false;
    char header[8]={0};
    if(httpd_req_get_hdr_value_str(req,"X-Mira-GPIO",header,sizeof(header))==ESP_OK&&
       !strcmp(header,"1"))return true;
    httpd_resp_send_err(req,HTTPD_403_FORBIDDEN,
                        "En-tete X-Mira-GPIO: 1 requis");
    return false;
}
static int gpio_channel(const char *name,const char *kind,bool allow_none){
    if(allow_none && name && !strcmp(name,"none"))return -1;
    if(!name)return -2;
    if(!strcmp(kind,"DO")){
        if(!strcmp(name,"DO0")||!strcmp(name,"0"))return 0;
        if(!strcmp(name,"DO1")||!strcmp(name,"1"))return 1;
    }else if(!strcmp(kind,"DI")){
        if(!strcmp(name,"DI0")||!strcmp(name,"0"))return 0;
        if(!strcmp(name,"DI1")||!strcmp(name,"1"))return 1;
    }
    return -2;
}
static esp_err_t gpio_post_result(httpd_req_t *req,esp_err_t err){
    if(err==ESP_OK){
        httpd_resp_set_type(req,"text/plain; charset=utf-8");
        return httpd_resp_sendstr(req,"OK");
    }
    if(err==ESP_ERR_INVALID_ARG)
        return html_err(req,HTTPD_400_BAD_REQUEST,"Parametres GPIO invalides");
    if(err==ESP_ERR_INVALID_STATE)
        return httpd_resp_set_status(req,"409 Conflict"),
               httpd_resp_sendstr(req,"Sortie desactivee ou non configuree");
    httpd_resp_set_status(req,"503 Service Unavailable");
    return httpd_resp_sendstr(req,"Sauvegarde ou acces CH32 indisponible");
}
esp_err_t mira_beta_web_gpio_state(httpd_req_t *req){
    if(!mira_beta_web_authorized(req))return ESP_FAIL;
    mira_gpio43_state_t state={0};
    esp_err_t err=mira_gpio43_state(&state);
    if(err!=ESP_OK){
        httpd_resp_set_status(req,"503 Service Unavailable");
        return httpd_resp_sendstr(req,"Lecture DI/DO CH32 impossible");
    }
    cJSON *root=cJSON_CreateObject(),*ins=cJSON_CreateArray(),
          *outs=cJSON_CreateArray(),*configs=cJSON_CreateArray(),
          *links=cJSON_CreateArray(),*multis=cJSON_CreateArray();
    if(!root||!ins||!outs||!configs||!links||!multis){
        cJSON_Delete(root);cJSON_Delete(ins);cJSON_Delete(outs);
        cJSON_Delete(configs);cJSON_Delete(links);cJSON_Delete(multis);
        return html_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"JSON GPIO indisponible");
    }
    cJSON_AddStringToObject(root,"hardware","waveshare-43c");
    cJSON_AddNumberToObject(root,"revision",state.revision);
    for(int i=0;i<2;i++){
        cJSON_AddItemToArray(ins,cJSON_CreateBool(state.input[i]));
        cJSON_AddItemToArray(outs,cJSON_CreateBool(state.output[i]));
        cJSON *c=cJSON_CreateObject();
        cJSON_AddBoolToObject(c,"enabled",state.config[i].enabled);
        cJSON_AddBoolToObject(c,"inverted",state.config[i].inverted);
        cJSON_AddNumberToObject(c,"boot",state.config[i].boot);
        cJSON_AddBoolToObject(c,"logical_on",state.config[i].logical_on);
        cJSON_AddItemToArray(configs,c);
    }
    mira_gpio43_link_t entries[24];
    mira_gpio43_multi_t bindings[2];
    mira_gpio43_describe(entries,bindings);
    for(unsigned i=0;i<24;i++){
        if(!entries[i].name[0])continue;
        cJSON *c=cJSON_CreateObject();
        cJSON_AddStringToObject(c,"name",entries[i].name);
        cJSON_AddNumberToObject(c,"output",entries[i].output);
        cJSON_AddNumberToObject(c,"kind",entries[i].kind);
        cJSON_AddItemToArray(links,c);
    }
    for(unsigned i=0;i<2;i++){
        cJSON *c=cJSON_CreateObject();
        cJSON_AddNumberToObject(c,"input",i);
        cJSON_AddStringToObject(c,"name",bindings[i].name);
        cJSON_AddBoolToObject(c,"visible",bindings[i].visible);
        cJSON_AddItemToArray(multis,c);
    }
    cJSON_AddItemToObject(root,"inputs",ins);
    cJSON_AddItemToObject(root,"outputs",outs);
    cJSON_AddItemToObject(root,"config",configs);
    cJSON_AddItemToObject(root,"links",links);
    cJSON_AddItemToObject(root,"multi",multis);
    char *json=cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if(!json)return html_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"JSON GPIO indisponible");
    httpd_resp_set_type(req,"application/json; charset=utf-8");
    httpd_resp_set_hdr(req,"Cache-Control","no-store");
    esp_err_t sent=httpd_resp_sendstr(req,json);
    cJSON_free(json);
    return sent;
}
esp_err_t mira_beta_web_gpio_set(httpd_req_t *req){
    if(!gpio_post_authorized(req))return ESP_FAIL;
    cJSON *form=read_form(req);
    if(!form)return html_err(req,HTTPD_400_BAD_REQUEST,"Formulaire GPIO invalide");
    const char *channel=field(form,"output"),*state=field(form,"state");
    int i=gpio_channel(channel,"DO",false);
    esp_err_t err=ESP_ERR_INVALID_ARG;
    if(i>=0&&state&&(!strcmp(state,"0")||!strcmp(state,"1")))
        err=mira_gpio43_write((unsigned)i,state[0]=='1');
    cJSON_Delete(form);
    return gpio_post_result(req,err);
}
esp_err_t mira_beta_web_gpio_raw(httpd_req_t *req){
    if(!gpio_post_authorized(req))return ESP_FAIL;
    cJSON *form=read_form(req);
    if(!form)return html_err(req,HTTPD_400_BAD_REQUEST,"Formulaire invalide");
    const int i=gpio_channel(field(form,"gpio"),"DO",false);
    const char *level=field(form,"level");
    esp_err_t err=ESP_ERR_INVALID_ARG;
    if(i>=0 && level && (!strcmp(level,"high")||!strcmp(level,"low")))
        err=mira_gpio43_write_physical((unsigned)i,!strcmp(level,"high"));
    cJSON_Delete(form);
    return gpio_post_result(req,err);
}
esp_err_t mira_beta_web_gpio_config(httpd_req_t *req){
    if(!gpio_post_authorized(req))return ESP_FAIL;
    cJSON *form=read_form(req);
    if(!form)return html_err(req,HTTPD_400_BAD_REQUEST,"Formulaire invalide");
    const int i=gpio_channel(field(form,"gpio"),"DO",false);
    const char *mode=field(form,"mode"),*inv=field(form,"inverted"),
               *boot=field(form,"boot");
    esp_err_t err=ESP_ERR_INVALID_ARG;
    if(i>=0&&mode&&inv&&boot&&(!strcmp(mode,"output")||!strcmp(mode,"off"))&&
       (!strcmp(inv,"0")||!strcmp(inv,"1"))){
        int b=!strcmp(boot,"off")?0:(!strcmp(boot,"restore")?1:
              (!strcmp(boot,"on")?2:-1));
        if(b>=0)err=mira_gpio43_configure((unsigned)i,!strcmp(mode,"output"),
                                         inv[0]=='1',(uint8_t)b);
    }
    cJSON_Delete(form);
    return gpio_post_result(req,err);
}
esp_err_t mira_beta_web_gpio_link(httpd_req_t *req){
    if(!gpio_post_authorized(req))return ESP_FAIL;
    cJSON *form=read_form(req);
    if(!form)return html_err(req,HTTPD_400_BAD_REQUEST,"Formulaire invalide");
    const char *name=field(form,"name"),*source=field(form,"source"),
               *gpio=field(form,"gpio"),*level=field(form,"level");
    int i=gpio_channel(gpio,"DO",true);
    esp_err_t err=ESP_ERR_INVALID_ARG;
    if(name && source && level &&
       (!strcmp(source,"gpio")||!strcmp(source,"jeedom"))){
        uint8_t kind=!strcmp(level,"high")?1:(!strcmp(level,"low")?0:
                     (!strcmp(level,"read")?2:3));
        if(kind<=2 && (i>=0||!strcmp(source,"jeedom")||i==-1))
            err=mira_gpio43_link(name,!strcmp(source,"jeedom")?-1:i,kind);
    }
    cJSON_Delete(form);
    return gpio_post_result(req,err);
}
esp_err_t mira_beta_web_gpio_multi(httpd_req_t *req){
    if(!gpio_post_authorized(req))return ESP_FAIL;
    cJSON *form=read_form(req);
    if(!form)return html_err(req,HTTPD_400_BAD_REQUEST,"Formulaire invalide");
    const char *name=field(form,"name"),*vis=field(form,"visible");
    const int i=gpio_channel(field(form,"gpio"),"DI",true);
    esp_err_t err=ESP_ERR_INVALID_ARG;
    if(name&&vis&&(i>=0||i==-1)&&(!strcmp(vis,"0")||!strcmp(vis,"1"))){
        err=mira_gpio43_multi(name,i,vis[0]=='1');
        if(err==ESP_OK)mira_full_request_reload();
    }
    cJSON_Delete(form);
    return gpio_post_result(req,err);
}
static esp_err_t beta_43c_gpio_page(httpd_req_t *req){
    httpd_resp_set_type(req,"text/html; charset=utf-8");
    httpd_resp_set_hdr(req,"Cache-Control","no-store");
    return httpd_resp_sendstr(req,
      "<!doctype html>\n"
      "<html lang=\"fr\"><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">\n"
      "<title>Mira Panel - GPIO Waveshare 4.3C</title>\n"
      "<style>\n"
      ":root{--bg:#7193A0;--panel:#1E5064;--active:#D39E55;--txt:#fff;--muted:#5f7987}\n"
      "*{box-sizing:border-box}body{margin:0;background:var(--bg);color:#24343d;font:15px Arial,sans-serif}\n"
      "header{background:var(--panel);color:var(--txt);position:sticky;top:0;z-index:5;box-shadow:0 2px 12px #0002}\n"
      "header .bar{max-width:1040px;margin:auto;min-height:66px;padding:0 18px;display:flex;flex-wrap:wrap;gap:14px;align-items:center;justify-content:space-between}\n"
      ".brand{font-weight:800;font-size:22px;color:#fff}\n"
      "nav{display:flex;gap:8px;flex-wrap:wrap}nav a{color:#fff;text-decoration:none;font-weight:bold;padding:10px 12px;border-radius:12px}\n"
      "nav a.active,nav a:hover{background:#ffffff24}\n"
      "main{max-width:1000px;margin:26px auto;padding:0 18px 40px}\n"
      "h1{margin:0 0 7px;font-size:31px}p{line-height:1.48}\n"
      ".intro{color:#455a64;margin:0 0 20px}\n"
      ".notice{padding:14px 16px;background:#fff6e7;border:1px solid #e9c990;border-radius:12px;margin-bottom:18px}\n"
      ".grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:14px}\n"
      ".card{background:white;border-radius:18px;padding:18px;box-shadow:0 4px 18px #0002;margin-bottom:14px}\n"
      ".card h2{font-size:19px;margin:0 0 12px}\n"
      ".row{display:flex;flex-wrap:wrap;align-items:center;gap:10px;margin:9px 0}\n"
      "select,input[type=text]{min-width:185px;max-width:100%;padding:9px;border:1px solid #cbd9df;border-radius:9px;background:#fff}\n"
      "input[type=checkbox]{width:17px;height:17px;accent-color:var(--active)}\n"
      "button{padding:10px 13px;background:var(--active);color:white;border:0;border-radius:10px;font-weight:bold;cursor:pointer}\n"
      "button.secondary{background:#607d8b}button:disabled{opacity:.45;cursor:default}\n"
      ".state{font-weight:bold;padding:6px 10px;border-radius:8px;background:#edf4f5}\n"
      ".state.high{background:#d8f5e5;color:#176b42}.state.low{background:#ffe9e8;color:#934040}\n"
      ".small{color:var(--muted);font-size:13px}\n"
      "#message{min-height:24px;font-weight:bold;margin:12px 0}\n"
      ".advanced{margin-top:18px}.advanced h2{margin:0 0 12px}\n"
      "@media(max-width:700px){.grid{grid-template-columns:1fr}h1{font-size:26px}header .bar{padding-top:12px;padding-bottom:12px}}\n"
      "</style></head>\n"
      "<body><header><div class=\"bar\"><div class=\"brand\">Mira Panel</div>\n"
      "<nav aria-label=\"Navigation Mira Panel\"><a href=\"/\">Accueil</a><a href=\"/load?nav=wifi\">Réseau</a><a href=\"/load?nav=mqtt\">MQTT</a><a href=\"/load?nav=params\">Paramètres</a><a href=\"/load?nav=system\">Système</a><a class=\"active\" href=\"/load?nav=gpio\" aria-current=\"page\">GPIO</a></nav>\n"
      "</div></header>\n"
      "<main><h1>GPIO · Waveshare 4,3C</h1>\n"
      "<p class=\"intro\">Pilotage local des deux entrées DI0/DI1 et des deux sorties DO0/DO1 du bornier isolé.</p>\n"
      "<div class=\"notice\"><b>Attention :</b> bornier isolé via contrôleur CH32 : DI0 et DI1 sont des entrées ; DO0 et DO1 sont des sorties. Vérifier la polarité et les limites électriques de la carte avant tout raccordement. Ne jamais brancher directement le secteur sur ces bornes. Les sorties démarrent au niveau bas par défaut ; les autres modes de démarrage sont à utiliser uniquement avec une charge adaptée.</div>\n"
      "<p id=\"message\" role=\"status\" aria-live=\"polite\"></p>\n"
      "<div class=\"grid\">\n"
      "<section class=\"card\"><h2>DI0 · Entrée isolée</h2><div class=\"row\"><span id=\"di0\" class=\"state\">Lecture…</span></div><p class=\"small\">Entrée EXIO0, lecture seule.</p></section>\n"
      "<section class=\"card\"><h2>DI1 · Entrée isolée</h2><div class=\"row\"><span id=\"di1\" class=\"state\">Lecture…</span></div><p class=\"small\">Entrée EXIO5, lecture seule.</p></section>\n"
      "<section class=\"card\"><h2>DO0 · Sortie isolée</h2>\n"
      "<form class=\"output-form\" data-output=\"0\" onsubmit=\"saveOutput(event)\">\n"
      "<div class=\"row\"><label>Mode <select name=\"mode\"><option value=\"off\">Désactivée</option><option value=\"output\">Sortie</option></select></label></div>\n"
      "<div class=\"row\"><label><input type=\"checkbox\" name=\"inverted\"> Inverser la commande</label></div>\n"
      "<div class=\"row\"><label>Démarrage <select name=\"boot\"><option value=\"off\">Arrêt</option><option value=\"restore\">Restaurer dernier état</option><option value=\"on\">Actif</option></select></label></div>\n"
      "<div class=\"row\"><span id=\"do0\" class=\"state\">Lecture…</span><button type=\"button\" id=\"b0\" onclick=\"toggleOutput(0)\" disabled>Changer DO0</button></div>\n"
      "<div class=\"row\"><button class=\"secondary\" type=\"submit\">Enregistrer DO0</button></div>\n"
      "</form></section>\n"
      "<section class=\"card\"><h2>DO1 · Sortie isolée</h2>\n"
      "<form class=\"output-form\" data-output=\"1\" onsubmit=\"saveOutput(event)\">\n"
      "<div class=\"row\"><label>Mode <select name=\"mode\"><option value=\"off\">Désactivée</option><option value=\"output\">Sortie</option></select></label></div>\n"
      "<div class=\"row\"><label><input type=\"checkbox\" name=\"inverted\"> Inverser la commande</label></div>\n"
      "<div class=\"row\"><label>Démarrage <select name=\"boot\"><option value=\"off\">Arrêt</option><option value=\"restore\">Restaurer dernier état</option><option value=\"on\">Actif</option></select></label></div>\n"
      "<div class=\"row\"><span id=\"do1\" class=\"state\">Lecture…</span><button type=\"button\" id=\"b1\" onclick=\"toggleOutput(1)\" disabled>Changer DO1</button></div>\n"
      "<div class=\"row\"><button class=\"secondary\" type=\"submit\">Enregistrer DO1</button></div>\n"
      "</form></section>\n"
      "</div>\n"
      "<p class=\"small\">États des quatre voies actualisés toutes les deux secondes. Les configurations de sorties sont mémorisées.</p>\n"
      "<div class=\"advanced card\"><h2>Liaisons locales de commandes</h2>\n"
      "<form onsubmit=\"saveGPIO(event,'/gpio/link')\">\n"
      "<div class=\"row\"><label>Nom de commande <input name=\"name\" maxlength=\"62\" required></label>\n"
      "<label>Source <select name=\"source\"><option value=\"gpio\">Sortie locale</option><option value=\"jeedom\">Supprimer la liaison</option></select></label></div>\n"
      "<div class=\"row\"><label>Sortie <select name=\"gpio\"><option>DO0</option><option>DO1</option><option value=\"none\">Aucune</option></select></label>\n"
      "<label>Niveau <select name=\"level\"><option value=\"high\">Haut</option><option value=\"low\">Bas</option><option value=\"read\">Lecture</option></select></label>\n"
      "<button type=\"submit\">Enregistrer liaison</button></div></form>\n"
      "<p class=\"small\">Pour un interrupteur, associer On_Nom et Off_Nom à la même sortie, avec des niveaux opposés.</p></div>\n"
      "<div class=\"advanced card\"><h2>Boutons multiactions isolés</h2>\n"
      "<form onsubmit=\"saveGPIO(event,'/gpio/multi/config')\">\n"
      "<div class=\"row\"><label>Nom de widget <input name=\"name\" maxlength=\"45\" required></label>\n"
      "<label>Entrée <select name=\"gpio\"><option>DI0</option><option>DI1</option><option value=\"none\">Désassocier</option></select></label>\n"
      "<label>Tuile tactile <select name=\"visible\"><option value=\"0\">Cachée</option><option value=\"1\">Visible</option></select></label></div>\n"
      "<button type=\"submit\">Enregistrer bouton</button></form>\n"
      "<p class=\"small\">Les entrées sont optocouplées : un contact sec seul ne suffit pas nécessairement à les piloter.</p></div>\n"
      "<div class=\"card\"><h2>État des paramètres</h2><pre id=\"details\" style=\"white-space:pre-wrap;overflow-wrap:anywhere\">Chargement…</pre></div>\n"
      "</main><script>\n"
      "let latest=null,busy=false,configInitialized=false;\n"
      "const msg=document.getElementById('message');\n"
      "function badge(id,on){const el=document.getElementById(id);el.textContent=on?'HIGH / ON':'LOW / OFF';el.className='state '+(on?'high':'low');}\n"
      "function syncForms(){if(!latest)return;document.querySelectorAll('form.output-form').forEach(form=>{\n"
      "const n=Number(form.dataset.output),conf=latest.config[n];form.elements.mode.value=conf.enabled?'output':'off';\n"
      "form.elements.inverted.checked=!!conf.inverted;\n"
      "form.elements.boot.value=conf.boot===2?'on':conf.boot===1?'restore':'off';\n"
      "});configInitialized=true;}\n"
      "async function read(){if(busy)return;\n"
      "try{const r=await fetch('/gpio/state',{credentials:'same-origin',cache:'no-store'});if(!r.ok)throw Error('HTTP '+r.status);\n"
      "const j=await r.json();if(j.hardware!=='waveshare-43c'||!j.inputs||!j.outputs||!j.config)throw Error('Profil matériel incompatible');\n"
      "latest=j;badge('di0',j.inputs[0]);badge('di1',j.inputs[1]);\n"
      "badge('do0',j.outputs[0]);badge('do1',j.outputs[1]);\n"
      "for(let i=0;i<2;i++)document.getElementById('b'+i).disabled=!j.config[i].enabled;\n"
      "if(!configInitialized)syncForms();\n"
      "document.getElementById('details').textContent=JSON.stringify({sorties:j.config,liaisons:j.links,multiactions:j.multi},null,2);\n"
      "}catch(e){msg.textContent='Lecture des E/S impossible : '+e.message;}}\n"
      "async function post(url,body){const r=await fetch(url,{method:'POST',credentials:'same-origin',\n"
      "headers:{'Content-Type':'application/x-www-form-urlencoded','X-Mira-GPIO':'1'},body});\n"
      "if(!r.ok)throw Error('HTTP '+r.status+' : '+await r.text());}\n"
      "async function toggleOutput(n){if(!latest||busy||!latest.config[n].enabled)return;\n"
      "const state=latest.config[n].logical_on?0:1;\n"
      "if(state&&!confirm('Activer physiquement DO'+n+' ?'))return;\n"
      "busy=true;try{await post('/gpio/set',new URLSearchParams({output:String(n),state:String(state)}));\n"
      "msg.textContent='DO'+n+' mis à '+state+'.';}catch(e){msg.textContent='Erreur : '+e.message;}\n"
      "finally{busy=false;await read();}}\n"
      "async function saveOutput(event){event.preventDefault();if(busy)return;\n"
      "const form=event.target,n=Number(form.dataset.output),values=new FormData(form),boot=values.get('boot');\n"
      "if((boot==='on'||boot==='restore')&&!confirm('Cette sortie pourra se réactiver au démarrage. Continuer ?'))return;\n"
      "const body=new URLSearchParams({gpio:'DO'+n,mode:String(values.get('mode')),\n"
      "inverted:form.elements.inverted.checked?'1':'0',boot:String(boot)});\n"
      "busy=true;try{await post('/gpio/config',body);msg.textContent='Configuration DO'+n+' enregistrée.';configInitialized=false;}\n"
      "catch(e){msg.textContent='Erreur : '+e.message;}finally{busy=false;await read();}}\n"
      "async function saveGPIO(event,url){event.preventDefault();if(busy)return;\n"
      "busy=true;try{await post(url,new URLSearchParams(new FormData(event.target)));msg.textContent='Paramètres enregistrés';}\n"
      "catch(e){msg.textContent='Erreur : '+e.message;}finally{busy=false;await read();}}\n"
      "read();setInterval(read,2000);\n"
      "</script></body></html>\n");
}

/* Keep the original beta System layout intact. The Waveshare 4.3C
 * GPIO link belongs in the existing Maintenance button row, not in an
 * extra card, and must never claim to expose WT32 EXT GPIOs. */
static esp_err_t render_system_43c(httpd_req_t *req){
    const char *page=mira_beta_system_html;
    const char *anchor="<button class=\"btn\" onclick=\"window.location.href='/load?nav=rst'\">Gestion des données</button>";
    const char *at=strstr(page,anchor);
    if(!at)return html_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,
                           "Modèle système Waveshare invalide");
    at+=strlen(anchor);
    const char *insert=
        "<a class=\"btn secondary\" href=\"/load?nav=gpio\">GPIO Waveshare 4,3C</a>";
    const size_t pre=(size_t)(at-page),len=strlen(page),extra=strlen(insert);
    char *copy=malloc(len+extra+1);
    if(!copy)return html_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,
                             "Mémoire HTML indisponible");
    memcpy(copy,page,pre);
    memcpy(copy+pre,insert,extra);
    memcpy(copy+pre+extra,at,len-pre+1);
    esp_err_t result=render(req,copy);
    free(copy);
    return result;
}

esp_err_t mira_beta_web_home(httpd_req_t *req){
    if(!mira_beta_web_authorized(req))return ESP_FAIL;
    return render(req,mira_beta_index_html);
}
esp_err_t mira_beta_web_load(httpd_req_t *req){
    if(!mira_beta_web_authorized(req))return ESP_FAIL;
    char query[128]={0},nav[24]="params";
    if(httpd_req_get_url_query_str(req,query,sizeof(query))==ESP_OK)
        (void)httpd_query_key_value(query,"nav",nav,sizeof(nav));
    if(!strcmp(nav,"params"))return render(req,mira_beta_params_html);
    if(!strcmp(nav,"id"))return mira_jeedom_render_http_reference(req);
    if(!strcmp(nav,"checkupdate"))return beta_checkupdate_page(req);
    if(!strcmp(nav,"gpio"))return beta_43c_gpio_page(req);
    if(!strcmp(nav,"mqtt"))return render(req,mira_beta_mqtt_html);
    if(!strcmp(nav,"wifi"))return render(req,mira_beta_wifi_manager_html);
    if(!strcmp(nav,"system"))return render_system_43c(req);
    if(!strcmp(nav,"rst"))return render(req,mira_beta_data_html);
    if(!strcmp(nav,"configAP"))return render(req,mira_beta_config_ap_html);
    if(!strcmp(nav,"listnetwork"))return render(req,mira_beta_known_networks_html);
    if(!strcmp(nav,"update"))return render_network_edit(req);
    if(!strcmp(nav,"removenetwork"))return beta_network_remove_confirm(req);
    if(!strcmp(nav,"rstnetwork")||!strcmp(nav,"rstparams")||!strcmp(nav,"rstall"))
        return mira_beta_data_confirm(req,nav);
    if(!strcmp(nav,"reboot")){
        httpd_resp_set_type(req,"text/plain; charset=utf-8");
        esp_err_t result=httpd_resp_sendstr(req,"Redémarrage de Mira Panel...");
        vTaskDelay(pdMS_TO_TICKS(450));
        esp_restart();
        return result;
    }
    return html_err(req,HTTPD_404_NOT_FOUND,"Page beta pas encore portee");
}
static esp_err_t go_to(httpd_req_t *req,const char *nav){
    httpd_resp_set_status(req,"303 See Other");
    httpd_resp_set_hdr(req,"Location",nav);
    httpd_resp_set_type(req,"text/plain");
    return httpd_resp_sendstr(req,"OK");
}
esp_err_t mira_beta_web_display_save(httpd_req_t *req){
    if(!mira_beta_web_authorized(req))return ESP_FAIL;
    cJSON *form=read_form(req);
    if(!form)return html_err(req,HTTPD_400_BAD_REQUEST,"Formulaire d'affichage invalide");
    static const struct {const char *form,*key;unsigned max;unsigned min;} numbers[]={
        {"DISPLAY_Jour","day_pct",100,1},{"DISPLAY_Nuit","night_pct",100,1},
        {"DISPLAY_Delai","sleep_delay",86400,0},{"DISPLAY_Veille","sleep_light",100,0}
    };
    unsigned values[4]={0};
    for(size_t i=0;i<4;i++){
        if(!decimal(field(form,numbers[i].form),numbers[i].max,&values[i])||
           values[i]<numbers[i].min){
            cJSON_Delete(form);
            return html_err(req,HTTPD_400_BAD_REQUEST,"Reglage d'affichage invalide");
        }
    }
    const char *start=field(form,"DISPLAY_DebutNuit"),*end=field(form,"DISPLAY_FinNuit");
    const char *times[]={start,end};
    for(int i=0;i<2;i++){
        const char *t=times[i];
        if(!t||strlen(t)!=5||t[2]!=':'||
           !isdigit((unsigned char)t[0])||!isdigit((unsigned char)t[1])||
           !isdigit((unsigned char)t[3])||!isdigit((unsigned char)t[4])||
           (t[0]-'0')*10+t[1]-'0'>23||(t[3]-'0')*10+t[4]-'0'>59){
            cJSON_Delete(form);
            return html_err(req,HTTPD_400_BAD_REQUEST,"Horaire jour/nuit invalide");
        }
    }
    unsigned night=field(form,"DISPLAY_NuitAuto_Check")!=NULL;
    unsigned clock=field(form,"DISPLAY_HorlogeVeille_Check")!=NULL;
    nvs_handle_t nv;
    esp_err_t err=nvs_open("mira_idf_ui",NVS_READWRITE,&nv);
    if(err==ESP_OK){
        for(size_t i=0;i<4&&err==ESP_OK;i++){
            if(values[i]>255)err=nvs_set_u32(nv,numbers[i].key,values[i]);
            else if(!strcmp(numbers[i].key,"sleep_delay"))err=nvs_set_u32(nv,numbers[i].key,values[i]);
            else err=nvs_set_u8(nv,numbers[i].key,(uint8_t)values[i]);
        }
        // Arduino refreshDisplayConfig() derives sleep activation from
        // DISPLAY_Delai on each Web configuration save. Keep the separate
        // local switch but reset its state when Web explicitly changes delay.
        if(err==ESP_OK)err=nvs_set_u8(nv,"sleep_enabled",values[2]>0?1:0);
        if(err==ESP_OK)err=nvs_set_u8(nv,"night_auto",(uint8_t)night);
        if(err==ESP_OK)err=nvs_set_u8(nv,"sleep_clock",(uint8_t)clock);
        if(err==ESP_OK)err=nvs_set_str(nv,"night_start",start);
        if(err==ESP_OK)err=nvs_set_str(nv,"night_end",end);
        if(err==ESP_OK)err=nvs_commit(nv);
        nvs_close(nv);
    }
    cJSON_Delete(form);
    if(err!=ESP_OK)return html_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Enregistrement affichage impossible");
    mira_full_request_settings_reload();
    return go_to(req,"/load?nav=params");
}
static esp_err_t save_mqtt(httpd_req_t *req,cJSON *form){
    const char *screen=field(form,"MQTT_Nom");
    const char *host=field(form,"MQTT_Serveur");
    const char *username=field(form,"MQTT_Utilisateur");
    const char *password=field(form,"MQTT_Mot_de_passe");
    const char *enabled=field(form,"MQTT_Actif");
    unsigned port=0,on=0;
    if(!screen||strlen(screen)>=48||!host||strlen(host)>=128||
       !username||strlen(username)>=64||
       !decimal(field(form,"MQTT_Port"),65535,&port)||port<1||
       !decimal(enabled,1,&on)){
        return html_err(req,HTTPD_400_BAD_REQUEST,"Parametres MQTT invalides");
    }
    /* Beta MiraMqtt::cleanScreenName() permits inner '/' and converts
     * '+'/'#' to underscores. Keep the original user-supplied setting;
     * mira_mqtt::read_config() normalizes before subscribing/publishing.
     */
    if(password&&strlen(password)>=96)
        return html_err(req,HTTPD_400_BAD_REQUEST,"Mot de passe MQTT trop long");
    nvs_handle_t nv;
    esp_err_t err=nvs_open("mira_idf_mqtt",NVS_READWRITE,&nv);
    if(err==ESP_OK){
        err=nvs_set_str(nv,"screen",screen);
        if(err==ESP_OK)err=nvs_set_str(nv,"host",host);
        if(err==ESP_OK)err=nvs_set_str(nv,"username",username);
        // An empty password leaves the existing secret intact.
        if(err==ESP_OK&&password&&*password)err=nvs_set_str(nv,"secret",password);
        if(err==ESP_OK)err=nvs_set_u16(nv,"port",(uint16_t)port);
        if(err==ESP_OK)err=nvs_set_u8(nv,"enabled",(uint8_t)on);
        if(err==ESP_OK)err=nvs_commit(nv);
        nvs_close(nv);
    }
    if(err!=ESP_OK)return html_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Sauvegarde MQTT impossible");
    (void)mira_mqtt_init();
    return go_to(req,"/load?nav=mqtt");
}
static esp_err_t save_urls(httpd_req_t *req,cJSON *form){
    cJSON *root=read_widgets();
    if(!root)return html_err(req,HTTPD_404_NOT_FOUND,"Configuration LVGL absente");
    cJSON *arr=cJSON_GetObjectItemCaseSensitive(root,"lvgl");
    cJSON *item=NULL;
    bool changed=false;
    cJSON_ArrayForEach(item,arr){
        const cJSON *name=cJSON_GetObjectItemCaseSensitive(item,"name");
        const cJSON *type=cJSON_GetObjectItemCaseSensitive(item,"type");
        if(!cJSON_IsString(name)||!name->valuestring||!cJSON_IsString(type))continue;
        if(!strcmp(type->valuestring,"info")||!strcmp(type->valuestring,"datetime"))continue;
        const char *url=field(form,name->valuestring);
        if(!url)continue;
        const cJSON *prior=cJSON_GetObjectItemCaseSensitive(item,"url");
        if(cJSON_IsString(prior)&&!strcmp(prior->valuestring,url))continue;
        cJSON *value=cJSON_CreateString(url);
        if(!value){cJSON_Delete(root);return html_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Memoire indisponible");}
        if(prior)cJSON_ReplaceItemInObjectCaseSensitive(item,"url",value);
        else cJSON_AddItemToObject(item,"url",value);
        changed=true;
    }
    if(changed){
        char *out=cJSON_PrintUnformatted(root);
        cJSON_Delete(root);
        if(!out)return html_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"JSON impossible");
        size_t len=strlen(out);
        if(len>MIRA_WEB_JSON_LIMIT){cJSON_free(out);return html_err(req,HTTPD_400_BAD_REQUEST,"Configuration trop volumineuse");}
        FILE *f=fopen(MIRA_WEB_TEMP,"wb");
        if(!f){cJSON_free(out);return html_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"SPIFFS inaccessible");}
        size_t bytes=fwrite(out,1,len,f);
        int closed=fclose(f);cJSON_free(out);
        if(bytes!=len||closed||rename(MIRA_WEB_TEMP,MIRA_WEB_PATH)!=0){
            remove(MIRA_WEB_TEMP);
            return html_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Enregistrement JSON impossible");
        }
        mira_full_request_reload();
    }else cJSON_Delete(root);
    return go_to(req,"/load?nav=params");
}
static esp_err_t save_beta_wifi(httpd_req_t *req,cJSON *form){
    const char *ssid=field(form,"ssid");
    const char *password=field(form,"pass");
    const char *mode=field(form,"ip_mode");
    const char *ip=field(form,"ip");
    const char *mask=field(form,"mask");
    const char *gw=field(form,"gateway");
    const char *dns1=field(form,"dns1");
    const char *dns2=field(form,"dns2");
    if(!ssid||!password||!mode)
        return html_err(req,HTTPD_400_BAD_REQUEST,"Formulaire réseau incomplet");
    // Check the saved-network directory BEFORE changing the live Wi-Fi.
    // The former code ignored list failures, silently overwrote profile 0
    // and held a 3+ KB array on the HTTP task's limited stack.
    mira_beta_network_t *previous=calloc(MIRA_NET_MAX,sizeof(*previous));
    if(!previous)
        return html_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Mémoire réseau indisponible");
    size_t count=0;
    esp_err_t list_err=mira_beta_networks_list(previous,&count,MIRA_NET_MAX);
    if(list_err!=ESP_OK){
        free(previous);
        return html_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Liste des réseaux indisponible");
    }
    int id=-1;
    for(size_t i=0;i<count;i++)
        if(!strcmp(previous[i].ssid,ssid)){id=previous[i].id;break;}
    if(id<0&&count>=MIRA_NET_MAX){
        free(previous);
        return html_err(req,HTTPD_400_BAD_REQUEST,"Limite de réseaux Mira atteinte");
    }
    if(id<0){
        for(int candidate=0;candidate<=999;candidate++){
            bool taken=false;
            for(size_t i=0;i<count;i++)
                if(previous[i].id==candidate){taken=true;break;}
            if(!taken){id=candidate;break;}
        }
    }
    free(previous);
    if(id<0)
        return html_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Identifiant réseau indisponible");
    esp_err_t result=mira_recovery_save_beta_wifi(ssid,password,mode,
        ip?ip:"",mask?mask:"",gw?gw:"",dns1?dns1:"",dns2?dns2:"");
    if(result==ESP_ERR_INVALID_ARG)
        return html_err(req,HTTPD_400_BAD_REQUEST,"Paramètres Wi-Fi ou IPv4 invalides");
    if(result!=ESP_OK)
        return html_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Enregistrement Wi-Fi impossible");
    mira_beta_network_t profile={0};
    profile.id=id;
    snprintf(profile.ssid,sizeof(profile.ssid),"%s",ssid);
    snprintf(profile.ip_mode,sizeof(profile.ip_mode),"%s",mode);
    snprintf(profile.ip,sizeof(profile.ip),"%s",ip?ip:"");
    snprintf(profile.mask,sizeof(profile.mask),"%s",mask?mask:"");
    snprintf(profile.gateway,sizeof(profile.gateway),"%s",gw?gw:"");
    snprintf(profile.dns1,sizeof(profile.dns1),"%s",dns1?dns1:"");
    snprintf(profile.dns2,sizeof(profile.dns2),"%s",dns2?dns2:"");
    result=mira_beta_networks_upsert(&profile,password);
    if(result!=ESP_OK){
        // Do not tell the user the saved-network operation succeeded when
        // the native NVS overlay could not be committed. The STA Wi-Fi
        // connection may have changed; report the persistence failure.
        ESP_LOGE(TAG,"Wi-Fi applied but native profile was NOT saved: %s",
                 esp_err_to_name(result));
        return html_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,
                        "Wi-Fi applique mais profil non enregistre; verifier NVS");
    }
    return go_to(req,"/load?nav=listnetwork");
}
static esp_err_t save_beta_ap(httpd_req_t *req,cJSON *form){
    const char *password=field(form,"pass");
    const char *ip=field(form,"ip");
    const char *active=field(form,"actif");
    if(!password||!ip||!active|| (strcmp(active,"0")&&strcmp(active,"1")))
        return html_err(req,HTTPD_400_BAD_REQUEST,"Configuration AP incomplète");
    esp_err_t err=mira_recovery_save_beta_ap(password,ip,!strcmp(active,"1"));
    if(err==ESP_ERR_INVALID_ARG)
        return html_err(req,HTTPD_400_BAD_REQUEST,"Clé AP (8-63 caractères) ou IP invalide");
    if(err!=ESP_OK)
        return html_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Sauvegarde AP impossible");
    httpd_resp_set_type(req,"text/html; charset=utf-8");
    esp_err_t answer=httpd_resp_sendstr(req,
        "<!doctype html><meta charset=\"utf-8\"><h2>Configuration du point d'accès enregistrée</h2>"
        "<p>Mira Panel redémarre pour appliquer la nouvelle configuration.</p>");
    vTaskDelay(pdMS_TO_TICKS(750));
    esp_restart();
    return answer;
}
static esp_err_t save_beta_network_update(httpd_req_t *req,cJSON *form){
    int id=network_id(req);
    mira_beta_network_t profile={0};
    if(id<0||mira_beta_networks_get(id,&profile)!=ESP_OK)
        return html_err(req,HTTPD_404_NOT_FOUND,"Réseau introuvable");
    const char *ssid=field(form,"ssid"),*password=field(form,"pass");
    const char *mode=field(form,"ip_mode");
    if(!ssid||!*ssid||strlen(ssid)>32||!password||!mode)
        return html_err(req,HTTPD_400_BAD_REQUEST,"Paramètres Wi-Fi incomplets");
    snprintf(profile.ssid,sizeof(profile.ssid),"%s",ssid);
    snprintf(profile.ip_mode,sizeof(profile.ip_mode),"%s",mode);
    const char *const fields[]={"ip","mask","gateway","dns1","dns2"};
    char *const values[]={profile.ip,profile.mask,profile.gateway,profile.dns1,profile.dns2};
    const size_t sizes[]={sizeof(profile.ip),sizeof(profile.mask),sizeof(profile.gateway),
                          sizeof(profile.dns1),sizeof(profile.dns2)};
    for(size_t i=0;i<5;i++){
        const char *value=field(form,fields[i]);
        if(!value||strlen(value)>=sizes[i])
            return html_err(req,HTTPD_400_BAD_REQUEST,"Adresse réseau invalide");
        snprintf(values[i],sizes[i],"%s",value);
    }
    if(strcmp(mode,"auto")&&strcmp(mode,"fixed_auto")&&strcmp(mode,"manual"))
        return html_err(req,HTTPD_400_BAD_REQUEST,"Mode IP invalide");
    esp_err_t saved=mira_beta_networks_upsert(&profile,password);
    if(saved!=ESP_OK)return html_err(req,HTTPD_400_BAD_REQUEST,"Réseau non enregistré");
    return go_to(req,"/load?nav=listnetwork");
}
esp_err_t mira_beta_web_network_post(httpd_req_t *req){
    if(!mira_beta_web_authorized(req))return ESP_FAIL;
    int id=network_id(req);
    char query[160]={0},action[20]={0};
    if(id<0||httpd_req_get_url_query_str(req,query,sizeof(query))!=ESP_OK||
       httpd_query_key_value(query,"action",action,sizeof(action))!=ESP_OK||
       (strcmp(action,"remove")&&strcmp(action,"activate")))
        return html_err(req,HTTPD_400_BAD_REQUEST,"Opération réseau invalide");
    cJSON *body=read_form(req);
    const char *nonce=body?field(body,"nonce"):NULL;
    unsigned long parsed=0;
    char trailing=0;
    bool verified=nonce&&sscanf(nonce,"%lx%c",&parsed,&trailing)==1&&
        parsed>0&&parsed<=UINT32_MAX&&(uint32_t)parsed==network_action_nonce&&
        network_nonce_time>0&&esp_timer_get_time()-network_nonce_time<180000000LL;
    if(body)cJSON_Delete(body);
    if(!verified)return html_err(req,HTTPD_403_FORBIDDEN,"Confirmation réseau expirée");
    network_action_nonce=0;network_nonce_time=0;
    if(!strcmp(action,"remove")){
        esp_err_t err=mira_beta_networks_remove(id);
        if(err!=ESP_OK)return html_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Suppression réseau impossible");
        return go_to(req,"/load?nav=listnetwork");
    }
    httpd_resp_set_type(req,"text/html; charset=utf-8");
    esp_err_t sent=httpd_resp_sendstr(req,
       "<!doctype html><meta charset=\"utf-8\"><h2>Changement de réseau</h2>"
       "<p>Mira Panel applique le réseau enregistré.</p>");
    vTaskDelay(pdMS_TO_TICKS(250));
    esp_err_t err=mira_beta_networks_activate(id);
    if(err!=ESP_OK)ESP_LOGW(TAG,"Activation réseau %d échouée: %s",id,esp_err_to_name(err));
    return sent;
}
esp_err_t mira_beta_web_save(httpd_req_t *req){
    if(!mira_beta_web_authorized(req))return ESP_FAIL;
    char query[96]={0},config[24]={0};
    if(httpd_req_get_url_query_str(req,query,sizeof(query))!=ESP_OK)
        return html_err(req,HTTPD_400_BAD_REQUEST,"Configuration beta manquante");
    bool editing=httpd_query_key_value(query,"update",config,sizeof(config))==ESP_OK;
    if(!editing)
        (void)httpd_query_key_value(query,"config",config,sizeof(config));
    if((editing&&strcmp(config,"wifi"))||
       (!editing&&strcmp(config,"params")&&strcmp(config,"wifi")&&strcmp(config,"AP")))
        return html_err(req,HTTPD_400_BAD_REQUEST,"Configuration beta non prise en charge");
    cJSON *form=read_form(req);
    if(!form)return html_err(req,HTTPD_400_BAD_REQUEST,"Formulaire invalide");
    esp_err_t err=editing?save_beta_network_update(req,form):
                 !strcmp(config,"AP")?save_beta_ap(req,form):
                 !strcmp(config,"wifi")?save_beta_wifi(req,form):
                 field(form,"MQTT_Actif")?save_mqtt(req,form):save_urls(req,form);
    cJSON_Delete(form);
    return err;
}
