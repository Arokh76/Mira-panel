// ESPAsyncWebServer renderer for the ORIGINAL Mira beta HTML templates.
// No esp_http_server request, sockets, port 8081 or HTTP loopback.
#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <esp_heap_caps.h>
#include <esp_netif.h>
#include <esp_wifi.h>
#include <nvs.h>
#include <cJSON.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <memory>
#include <algorithm>
#include "mira_beta_web_pages.h"
extern "C" {
#include "mira_beta_web.h"
#include "mira_beta_networks.h"
#include "mira_jeedom.h"
#include "mira_full.h"
}

struct PsramPage {
    char *data=nullptr;
    size_t len=0,capacity=0;
    ~PsramPage(){heap_caps_free(data);}
    bool add(const char *s,size_t count){
        if(!s)return true;
        if(len+count>262144u)return false;
        if(len+count+1>capacity){
            size_t n=std::max(len+count+1,capacity?capacity*2u:8192u);
            n=std::min(n,(size_t)262145);
            char *grown=(char *)heap_caps_realloc(data,n,
                                MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
            if(!grown)return false;
            data=grown;capacity=n;
        }
        memcpy(data+len,s,count);
        len+=count;data[len]=0;
        return true;
    }
    bool add(const char *s){return s?add(s,strlen(s)):true;}
    bool escaped(const char *s){
        if(!s)return true;
        for(;*s;s++){
            const char *entity=nullptr;
            switch(*s){
                case '&': entity="&amp;";break;
                case '<': entity="&lt;";break;
                case '>': entity="&gt;";break;
                case '"': entity="&quot;";break;
                case '\'':entity="&#39;";break;
            }
            if(entity){if(!add(entity))return false;}
            else if(!add(s,1))return false;
        }
        return true;
    }
    char *release(){char *x=data;data=nullptr;len=capacity=0;return x;}
};
static String q(AsyncWebServerRequest *r,const char *name){
    const auto *p=r->getParam(name);
    return p?p->value():String();
}
static void nvs_text(const char *ns,const char *key,const char *fallback,
                     char *out,size_t cap){
    snprintf(out,cap,"%s",fallback?fallback:"");
    nvs_handle_t nv;
    if(nvs_open(ns,NVS_READONLY,&nv)==ESP_OK){
        size_t n=cap;
        if(nvs_get_str(nv,key,out,&n)!=ESP_OK)
            snprintf(out,cap,"%s",fallback?fallback:"");
        nvs_close(nv);
    }
}
static unsigned nvs_number(const char *ns,const char *key,unsigned fallback){
    nvs_handle_t nv;
    uint32_t u32=0;uint8_t u8=0;
    if(nvs_open(ns,NVS_READONLY,&nv)!=ESP_OK)return fallback;
    esp_err_t err=nvs_get_u32(nv,key,&u32);
    if(err!=ESP_OK){
        err=nvs_get_u8(nv,key,&u8);
        if(err==ESP_OK)u32=u8;
    }
    nvs_close(nv);
    return err==ESP_OK?u32:fallback;
}
static const char *field(const cJSON *obj,const char *k){
    const cJSON *v=cJSON_GetObjectItemCaseSensitive(obj,k);
    return cJSON_IsString(v)&&v->valuestring?v->valuestring:"";
}
static int number(const cJSON *obj,const char *k){
    const cJSON *v=cJSON_GetObjectItemCaseSensitive(obj,k);
    return cJSON_IsNumber(v)?v->valueint:-1;
}
static bool widget_rows(PsramPage& out,bool mqtt){
    size_t len=0;
    char *raw=mira_jeedom_async_lvgl_snapshot(&len);
    cJSON *root=raw?cJSON_ParseWithLength(raw,len):nullptr;
    free(raw);
    const cJSON *widgets=root?cJSON_GetObjectItemCaseSensitive(root,"lvgl"):nullptr;
    const cJSON *item=nullptr;
    bool any=false,ok=true;
    cJSON_ArrayForEach(item,widgets){
        const char *name=field(item,"name"),*type=field(item,"type");
        const char *url=field(item,"url");
        if(!mqtt&&(!strcmp(type,"datetime")||!strcmp(type,"info")))continue;
        if(!*name&&strcmp(type,"datetime"))continue;
        any=true;
        if(mqtt){
            ok=out.add("<tr><td>")&&out.escaped(*name?name:"Date et heure")&&
               out.add("</td><td>")&&out.escaped(type)&&out.add("</td></tr>");
        }else{
            ok=out.add("<div class=\"field\"><label>")&&out.escaped(name)&&
               out.add("</label><input id=\"")&&out.escaped(name)&&
               out.add("\" name=\"")&&out.escaped(name)&&
               out.add("\" value=\"")&&out.escaped(url)&&
               out.add("\" type=\"text\" placeholder=\"URL d'envoi d'état\"></div>");
        }
        if(!ok)break;
    }
    if(root)cJSON_Delete(root);
    if(!ok)return false;
    if(!any)return out.add(mqtt?"<tr><td colspan='2'>Aucun widget programme</td></tr>":
                           "<div class='empty'>Aucun widget programme</div>");
    return true;
}
static bool action_rows(PsramPage& out){
    char *json=nullptr;size_t len=0;
    if(mira_jeedom_async_index_snapshot(&json,&len)!=ESP_OK||!json)
        return out.add("<p>Aucune programmation Mira Panel.</p>");
    cJSON *root=cJSON_ParseWithLength(json,len);
    heap_caps_free(json);
    const cJSON *actions=root?cJSON_GetObjectItemCaseSensitive(root,"actions"):nullptr;
    const cJSON *item=nullptr;
    bool ok=true,any=false;
    cJSON_ArrayForEach(item,actions){
        const char *name=field(item,"nom"),*type=field(item,"type");
        const int id=number(item,"id");
        if(!*name)continue;
        char local[256];
        if((!strcmp(type,"button")||!strcmp(type,"little_button"))&&id>0){
            snprintf(local,sizeof(local),"<button class=\"Bouton\" id=\"%d\" onclick=\"Click(this)\">",id);
            ok=out.add(local)&&out.escaped(name)&&out.add("</button>");
            any=true;
        }else if(!strcmp(type,"title")||!strcmp(type,"text_index")){
            ok=out.add("<div style='padding:10px 0'>")&&out.escaped(name)&&out.add("</div>");
            any=true;
        }else if(!strcmp(type,"info")&&id>0){
            snprintf(local,sizeof(local),"<div class='mira-info'>%d : ",id);
            char cached[160]={0};
            (void)mira_full_cached_state(name,cached,sizeof(cached));
            ok=out.add(local)&&out.escaped(name)&&out.add(" <span id='")&&
               out.add(local+0,0); // keep IDs supplied by actual index below
            if(ok){
                snprintf(local,sizeof(local),"%d",id);
                ok=out.add(local)&&out.add("'>")&&out.escaped(cached)&&
                   out.add("</span></div>");
            }
            any=true;
        }else if(!strcmp(type,"toggle")){
            int off=number(item,"id0"),on=number(item,"id1");
            if(off<=0||on<=0)continue;
            snprintf(local,sizeof(local),
                "<div><button class='Bouton' id='%d' onclick='Click(this)'>OFF</button> "
                "<button class='Bouton' id='%d' onclick='Click(this)'>ON</button> ",off,on);
            ok=out.add(local)&&out.escaped(name)&&out.add("</div>");
            any=true;
        }
        if(!ok)break;
    }
    cJSON_Delete(root);
    return ok&&(any||out.add("<p>Aucune commande Web programmee.</p>"));
}
static bool network_rows(PsramPage& out){
    mira_beta_network_t *entries=(mira_beta_network_t *)heap_caps_calloc(
        MIRA_NET_MAX,sizeof(mira_beta_network_t),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!entries)return out.add("<div>Memoire reseau insuffisante</div>");
    size_t count=0;bool ok=true;
    const esp_err_t err=mira_beta_networks_list(entries,&count,MIRA_NET_MAX);
    if(err!=ESP_OK||!count)ok=out.add("<div class='empty'>Aucun reseau enregistre.</div>");
    else for(size_t i=0;i<count;i++){
        char str[220];
        snprintf(str,sizeof(str),
          "<div class='network-item'><span class='network-id'>%d</span>"
          "<button type='button' class='network-button' onclick=\"location.href='/load?nav=update&amp;network=%d'\">",
          entries[i].id,entries[i].id);
        ok=out.add(str)&&out.escaped(entries[i].ssid)&&out.add("</button></div>");
        if(!ok)break;
    }
    heap_caps_free(entries);
    return ok;
}
static bool replace_token(PsramPage& out,const char *token,
                          AsyncWebServerRequest *r){
    struct Constant {const char *name,*value;};
    static const Constant defaults[]={
        {"APP","Mira Panel"},{"VERSION","0.7.37-IDF-7STD-beta"},
        {"LIGHTCOLOR","#7193A0"},{"DARKCOLOR","#1E5064"},
        {"ACTIVECOLOR","#D39E55"},{"TEXTCOLOR","#FFFFFF"},
        {"TITRE","Mira Panel"},{"MESSAGE","Configuration"}
    };
    for(const auto& entry:defaults)
        if(!strcmp(token,entry.name))return out.add(entry.value);
    if(!strcmp(token,"PARAMS"))return widget_rows(out,false);
    if(!strcmp(token,"MQTTTABLE"))return widget_rows(out,true);
    if(!strcmp(token,"ACTIONS"))return action_rows(out);
    if(!strcmp(token,"RESEAUXENREG"))return network_rows(out);
    if(!strcmp(token,"SCANRESEAUX"))
        return out.add("<label>SSID</label><select id='RS' name='RS' "
            "onchange=\"document.getElementById('ssid').value=this.value\">"
            "<option value=''>Saisir le nom du reseau</option></select>");
    if(!strcmp(token,"MQTTPASS")||!strcmp(token,"PASSUPDATE"))return true;
    if(!strcmp(token,"WIFIACTIVE")||!strcmp(token,"LOCALIP")){
        esp_netif_t *netif=esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        esp_netif_ip_info_t ip={};
        if(!netif||esp_netif_get_ip_info(netif,&ip)!=ESP_OK||!ip.ip.addr)
            return out.add("Non connecte");
        char local[28]={0};
        snprintf(local,sizeof(local),IPSTR,IP2STR(&ip.ip));
        return out.escaped(local);
    }
    if(!strcmp(token,"SSID")){
        wifi_config_t sta={};
        if(esp_wifi_get_config(WIFI_IF_STA,&sta)!=ESP_OK)return true;
        char ssid[33]={0};
        memcpy(ssid,sta.sta.ssid,32);
        return out.escaped(ssid);
    }
    const char *netkeys[]={"IPMODE","IP","MASK","GATEWAY","DNS1","DNS2"};
    const char *prefs[]={"web_mode","web_ip","web_mask","web_gw","web_dns1","web_dns2"};
    for(size_t i=0;i<6;i++)if(!strcmp(token,netkeys[i])){
        char val[64]={0};
        nvs_text("mira_idf_net",prefs[i],i==0?"auto":"",val,sizeof(val));
        return out.escaped(val);
    }
    if(!strcmp(token,"MDPAP")){
        char key[97]={0};
        if(!mira_beta_web_auth_key_copy(key,sizeof(key)))return true;
        bool ok=out.escaped(key);memset(key,0,sizeof(key));return ok;
    }
    if(!strcmp(token,"IPAP")){
        char ip[40]={0};nvs_text("mira_idf_net","ap_ip","192.168.4.1",ip,sizeof(ip));
        return out.escaped(ip);
    }
    if(!strcmp(token,"ACTIFAP")){
        bool yes=nvs_number("mira_idf_net","ap_enabled",0)!=0;
        return out.add(yes?"<input type='checkbox' checked onchange='ClickToogle(this)'>":
                           "<input type='checkbox' onchange='ClickToogle(this)'>");
    }
    static const struct {const char *slot,*key;unsigned fallback;} display[]={
        {"DISPLAYDAY","day_pct",85},{"DISPLAYNIGHT","night_pct",20},
        {"DISPLAYSLEEPTIMEOUT","sleep_delay",120},
        {"DISPLAYSLEEPBRIGHT","sleep_light",8},
        {"DISPLAYNIGHTCHECKED","night_auto",0},
        {"DISPLAYCLOCKCHECKED","sleep_clock",1}
    };
    for(const auto& x:display)if(!strcmp(token,x.slot)){
        unsigned v=nvs_number("mira_idf_ui",x.key,x.fallback);
        if(strstr(token,"CHECKED"))return out.add(v?"checked":"");
        char text[16];snprintf(text,sizeof(text),"%u",v);
        return out.add(text);
    }
    if(!strcmp(token,"DISPLAYNIGHTSTART")||!strcmp(token,"DISPLAYNIGHTEND")){
        char t[24]={0};bool start=!strcmp(token,"DISPLAYNIGHTSTART");
        nvs_text("mira_idf_ui",start?"night_start":"night_end",
                 start?"22:00":"07:00",t,sizeof(t));
        return out.escaped(t);
    }
    if(!strcmp(token,"MQTTCHECKED")||!strcmp(token,"MQTTACTIF")){
        bool on=nvs_number("mira_idf_mqtt","enabled",0)!=0;
        return out.add(!strcmp(token,"MQTTCHECKED")?(on?"checked":""):
                       (on?"1":"0"));
    }
    static const struct {const char *name,*key,*fallback;} mqtt[]={
        {"MQTTNOM","screen","panel"},{"MQTTSERVEUR","host",""},
        {"MQTTUSER","username",""}
    };
    for(const auto& x:mqtt)if(!strcmp(token,x.name)){
        char val[160]={0};
        nvs_text("mira_idf_mqtt",x.key,x.fallback,val,sizeof(val));
        return out.escaped(val);
    }
    if(!strcmp(token,"MQTTPORT")){
        char port[16];snprintf(port,sizeof(port),"%u",
                               nvs_number("mira_idf_mqtt","port",1883));
        return out.add(port);
    }
    const char *fields[]={"SSIDUPDATE","IPMODEUPDATE","IPUPDATE","MASKUPDATE",
                          "GATEWAYUPDATE","DNS1UPDATE","DNS2UPDATE"};
    String network=q(r,"network");
    int id=network.toInt();
    mira_beta_network_t profile={};
    if(network.length()&&mira_beta_networks_get(id,&profile)==ESP_OK){
        const char *values[]={profile.ssid,profile.ip_mode,profile.ip,
                               profile.mask,profile.gateway,profile.dns1,profile.dns2};
        for(size_t i=0;i<7;i++)if(!strcmp(fields[i],token))
            return out.escaped(values[i]);
    }
    // Preserve raw percent-delimiters for unsupported template keys? No:
    // empty prevents exposing tokens as literal password strings.
    return true;
}
static const char* choose_template(AsyncWebServerRequest *r){
    if(r->url()=="/")return mira_beta_index_html;
    if(r->url()!="/load")return nullptr;
    const String nav=q(r,"nav");
    if(!nav.length()||nav=="params")return mira_beta_params_html;
    if(nav=="mqtt")return mira_beta_mqtt_html;
    if(nav=="wifi")return mira_beta_wifi_manager_html;
    if(nav=="system")return mira_beta_system_html;
    if(nav=="rst")return mira_beta_data_html;
    if(nav=="configAP")return mira_beta_config_ap_html;
    if(nav=="listnetwork")return mira_beta_known_networks_html;
    if(nav=="update")return mira_beta_edit_network_html;
    if(nav=="id")return mira_beta_params_html;
    if(nav=="checkupdate")return mira_beta_system_html;
    return nullptr;
}
bool mira_async_render_page(AsyncWebServerRequest *r){
    const char *page=choose_template(r);
    if(!page)return false;
    char key[97]={0};
    bool auth=mira_beta_web_auth_key_copy(key,sizeof(key))&&
              r->authenticate("admin",key);
    memset(key,0,sizeof(key));
    if(!auth){r->requestAuthentication();return true;}
    PsramPage out;
    const char *start=page,*p=page;
    bool ok=true;
    while(*p&&ok){
        if(*p!='%'){p++;continue;}
        if(p>start&&!out.add(start,p-start)){ok=false;break;}
        if(p[1]=='%'){
            ok=out.add("%");p+=2;start=p;continue;
        }
        const char *end=strchr(p+1,'%');
        if(!end||end-p>42){
            ok=out.add("%");p++;start=p;continue;
        }
        char token[44]={0};
        memcpy(token,p+1,(size_t)(end-p-1));
        bool candidate=token[0]!=0;
        for(const char *x=token;*x;x++)
            if(!((*x>='A'&&*x<='Z')||(*x>='0'&&*x<='9')||*x=='_')){
                candidate=false;break;
            }
        if(!candidate){
            ok=out.add("%");p++;start=p;continue;
        }
        ok=replace_token(out,token,r);
        p=end+1;start=p;
    }
    if(ok&&p>start)ok=out.add(start,p-start);
    if(!ok||!out.data){
        r->send(503,"text/plain","Memoire HTML PSRAM indisponible");
        return true;
    }
    const size_t len=out.len;
    auto owner=std::shared_ptr<char>(out.release(),[](char *p){heap_caps_free(p);});
    auto *response=r->beginResponse("text/html; charset=utf-8",len,
       [owner,len](uint8_t *dst,size_t max,size_t offset)->size_t{
           if(offset>=len)return 0;
           size_t n=std::min(max,len-offset);
           memcpy(dst,owner.get()+offset,n);
           return n;
       });
    response->addHeader("Cache-Control","no-store");
    r->send(response);
    return true;
}
