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
static esp_err_t wifi_choices(httpd_req_t *req){
    if(chunk(req,"<label for=\"RS\">Réseaux détectés</label><select id=\"RS\" name=\"RS\" class=\"box\" onchange=\"Click(this.value)\"><option value=\"\">Saisir le SSID manuellement</option>")!=ESP_OK)
        return ESP_FAIL;
    wifi_scan_config_t scan={0};
    if(esp_wifi_scan_start(&scan,true)==ESP_OK){
        wifi_ap_record_t records[10]={0};
        uint16_t count=10;
        if(esp_wifi_scan_get_ap_records(&count,records)==ESP_OK){
            for(uint16_t i=0;i<count;i++){
                char ssid[33]={0};
                memcpy(ssid,records[i].ssid,32);
                if(!ssid[0])continue;
                if(chunk(req,"<option value=\"")!=ESP_OK||escaped(req,ssid)!=ESP_OK||
                   chunk(req,"\">")!=ESP_OK||escaped(req,ssid)!=ESP_OK||
                   chunk(req,"</option>")!=ESP_OK)return ESP_FAIL;
            }
        }
    }
    return chunk(req,"</select>");
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
    mira_beta_network_t profiles[MIRA_NET_MAX]={0};
    size_t count=0;
    if(mira_beta_networks_list(profiles,&count,MIRA_NET_MAX)!=ESP_OK)
        return chunk(req,"<div class=\"empty\">Impossible de lire les réseaux.</div>");
    if(!count)return chunk(req,"<div class=\"empty\">Aucun réseau n'est enregistré.</div>");
    network_action_nonce=esp_random();
    if(!network_action_nonce)network_action_nonce=1;
    network_nonce_time=esp_timer_get_time();
    for(size_t i=0;i<count;i++){
        char head[210]={0},tail[260]={0};
        int id=profiles[i].id;
        snprintf(head,sizeof(head),
                 "<div class=\"network-item\"><span class=\"network-id\">%d</span>"
                 "<button class=\"network-button\" onclick=\"window.location.href='/load?nav=update&amp;network=%d'\">",
                 id,id);
        if(chunk(req,head)!=ESP_OK||escaped(req,profiles[i].ssid)!=ESP_OK||
           chunk(req,"</button>")!=ESP_OK)return ESP_FAIL;
        snprintf(tail,sizeof(tail),
                 "<form method=\"POST\" action=\"/mira/network?action=activate&amp;network=%d\" "
                 "style=\"display:inline\">"
                 "<input type=\"hidden\" name=\"nonce\" value=\"%08lx\">"
                 "<button class=\"btn secondary\" type=\"submit\">Activer</button></form></div>",
                 id,(unsigned long)network_action_nonce);
        if(chunk(req,tail)!=ESP_OK)return ESP_FAIL;
    }
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
        nvs_text("mira_idf_net","web_mode","auto",value,sizeof(value));
        return escaped(r,value);
    }
    static const struct {const char *slot,*key;} network_fields[]={
        {"IP","web_ip"},{"MASK","web_mask"},{"GATEWAY","web_gw"},
        {"DNS1","web_dns1"},{"DNS2","web_dns2"}
    };
    for(size_t i=0;i<sizeof(network_fields)/sizeof(network_fields[0]);i++){
        if(!strcmp(name,network_fields[i].slot)){
            char value[40]={0};
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
    if(!strcmp(nav,"mqtt"))return render(req,mira_beta_mqtt_html);
    if(!strcmp(nav,"wifi"))return render(req,mira_beta_wifi_manager_html);
    if(!strcmp(nav,"system"))return render(req,mira_beta_system_html);
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
    if(!screen||!*screen||strlen(screen)>=48||!host||strlen(host)>=128||
       !username||strlen(username)>=64||
       !decimal(field(form,"MQTT_Port"),65535,&port)||port<1||
       !decimal(enabled,1,&on)){
        return html_err(req,HTTPD_400_BAD_REQUEST,"Parametres MQTT invalides");
    }
    for(const char *p=screen;*p;p++){
        if(*p=='/'||*p=='+'||*p=='#')return html_err(req,HTTPD_400_BAD_REQUEST,"Nom MQTT invalide");
    }
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
    esp_err_t result=mira_recovery_save_beta_wifi(ssid,password,mode,
        ip?ip:"",mask?mask:"",gw?gw:"",dns1?dns1:"",dns2?dns2:"");
    if(result==ESP_ERR_INVALID_ARG)
        return html_err(req,HTTPD_400_BAD_REQUEST,"Paramètres Wi-Fi ou IPv4 invalides");
    if(result!=ESP_OK)
        return html_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Enregistrement Wi-Fi impossible");
    return go_to(req,"/load?nav=wifi");
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
esp_err_t mira_beta_web_save(httpd_req_t *req){
    if(!mira_beta_web_authorized(req))return ESP_FAIL;
    char query[96]={0},config[24]={0};
    if(httpd_req_get_url_query_str(req,query,sizeof(query))!=ESP_OK||
       httpd_query_key_value(query,"config",config,sizeof(config))!=ESP_OK||
       (strcmp(config,"params")&&strcmp(config,"wifi")&&strcmp(config,"AP")))
        return html_err(req,HTTPD_400_BAD_REQUEST,"Configuration beta non prise en charge");
    cJSON *form=read_form(req);
    if(!form)return html_err(req,HTTPD_400_BAD_REQUEST,"Formulaire invalide");
    esp_err_t err=!strcmp(config,"AP")?save_beta_ap(req,form):
                 !strcmp(config,"wifi")?save_beta_wifi(req,form):
                 field(form,"MQTT_Actif")?save_mqtt(req,form):save_urls(req,form);
    cJSON_Delete(form);
    return err;
}
