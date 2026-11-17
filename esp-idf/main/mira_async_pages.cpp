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
#include <cctype>
#include <ctime>
#include <esp_timer.h>
#include <esp_system.h>
#include <esp_random.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <memory>
#include <algorithm>
#include "mira_beta_web_pages.h"
extern "C" {
#include "mira_beta_web.h"
#include "mira_beta_networks.h"
#include "mira_jeedom.h"
#include "mira_full.h"
#include "mira_recovery.h"
#include "mira_mqtt.h"
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
    uint32_t u32=0;uint8_t u8=0;uint16_t u16=0;
    if(nvs_open(ns,NVS_READONLY,&nv)!=ESP_OK)return fallback;
    esp_err_t err=nvs_get_u32(nv,key,&u32);
    if(err!=ESP_OK){
        err=nvs_get_u16(nv,key,&u16);
        if(err==ESP_OK)u32=u16;
    }
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
// Render the same widget controls as the original WT32/beta Web page.
// The HTTP transport changes, not the controls, styles or EventSource IDs.
static bool action_rows(PsramPage& out){
    char *json=nullptr;size_t len=0;
    if(mira_jeedom_async_index_snapshot(&json,&len)!=ESP_OK||!json)
        return out.add("<p>Aucune programmation Mira Panel.</p>");
    cJSON *root=cJSON_ParseWithLength(json,len);
    heap_caps_free(json);
    const cJSON *actions=root?cJSON_GetObjectItemCaseSensitive(root,"actions"):nullptr;
    if(!cJSON_IsArray(actions)){cJSON_Delete(root);return false;}
    const cJSON *item=nullptr;
    bool ok=true,any=false;
    char groups[16][96]={{0}};
    unsigned group_count=0;
    for(int i=0, count=cJSON_GetArraySize(actions);i<count&&ok;i++){
        item=cJSON_GetArrayItem(actions,i);
        const char *name=field(item,"nom"),*type=field(item,"type");
        const int id=number(item,"id");
        if(!*name)continue;
        char text[256];
        if((!strcmp(type,"button")||!strcmp(type,"little_button"))&&id>0){
            snprintf(text,sizeof(text),"<button class=\"%s\" id=\"%d\" onclick=\"Click(this)\">",
                     !strcmp(type,"little_button")?"LittleBouton":"Bouton",id);
            ok=out.add(text)&&out.escaped(name)&&out.add("</button>");
            any=true;
        }else if(!strcmp(type,"title")||!strcmp(type,"text_index")){
            int size=number(item,"size");if(size<10)size=20;if(size>72)size=72;
            snprintf(text,sizeof(text),"<div style=\"padding:10px 0;font-size:%dpx\">",size);
            ok=out.add(text)&&out.escaped(name)&&out.add("</div>");
            any=true;
        }else if(!strcmp(type,"inputbox")&&id>0){
            char cached[160]={0};
            if(!mira_full_cached_state(name,cached,sizeof(cached)))
                snprintf(cached,sizeof(cached),"%s",field(item,"value"));
            snprintf(text,sizeof(text),"</label><input class=\"inputbox\" type=\"text\" id=\"%d\" value=\"",id);
            ok=out.add("<label>")&&out.escaped(name)&&out.add(text)&&
               out.escaped(cached)&&out.add("\" onblur=\"InputBoxValidate(this)\"><br>");
            any=true;
        }else if(!strcmp(type,"infofa")&&id>0){
            snprintf(text,sizeof(text),": <i id=\"%d\" class=\"",id);
            char cached[160]={0};
            if(!mira_full_cached_state(name,cached,sizeof(cached)))
                snprintf(cached,sizeof(cached),"%s",field(item,"value"));
            ok=out.add("<div class=\"mira-info\">")&&out.escaped(name)&&out.add(text)&&
               out.escaped(cached)&&out.add("\"></i></div>");
            any=true;
        }else if(!strcmp(type,"color")&&id>0){
            unsigned c[3]={50,120,180};
            const char *keys[]={"r","g","b"};
            for(int j=0;j<3;j++){
                const cJSON *v=cJSON_GetObjectItemCaseSensitive(item,keys[j]);
                const char *raw=cJSON_IsString(v)?v->valuestring:nullptr;
                long n=raw?strtol(raw,nullptr,10):(cJSON_IsNumber(v)?v->valueint:c[j]);
                if(n<0||n>255)n=0;
                c[j]=(unsigned)n;
            }
            char cached[160]={0};
            if(mira_full_cached_state(name,cached,sizeof(cached))&&
               cached[0]=='#'&&strlen(cached)==7){
                unsigned rr=0,gg=0,bb=0;
                if(sscanf(cached+1,"%2x%2x%2x",&rr,&gg,&bb)==3){
                    c[0]=rr;c[1]=gg;c[2]=bb;
                }
            }
            snprintf(text,sizeof(text),
                     "<br><input class=\"input_color\" type=\"color\" id=\"%d\" value=\"#%02x%02x%02x\" "
                     "onchange=\"printColor(this,event)\"></div>",id,c[0],c[1],c[2]);
            ok=out.add("<div class=\"FondBlanc_Color\">")&&out.escaped(name)&&out.add(text);
            any=true;
        }else if(!strcmp(type,"toggle")){
            const int off=number(item,"id0"),on=number(item,"id1");
            if(off<=0||on<=0)continue;
            char current[160]={0};
            bool known=mira_full_cached_state(name,current,sizeof(current));
            bool checked=known&&(!strcasecmp(current,"On")||!strcmp(current,"1")||
                                !strcasecmp(current,"true"));
            snprintf(text,sizeof(text),
                "</div><label class=\"switch\"><input type=\"checkbox\" id=\"%d\" "
                "onclick=\"ClickToogle(this,'Off','On','%d','%d')\"%s>"
                "<span class=\"slider round\"></span></label></div>",
                off,off,on,checked?" checked":"");
            ok=out.add("<div class=\"div_toogle\"><div class=\"div_toogle_name\">")&&
               out.escaped(name)&&out.add(text);
            any=true;
        }else if(!strcmp(type,"info")&&id>0){
            char cached[160]={0};
            if(!mira_full_cached_state(name,cached,sizeof(cached)))
                snprintf(cached,sizeof(cached),"%s",field(item,"value"));
            snprintf(text,sizeof(text),": <b><span id=\"%d\">",id);
            ok=out.add("<div class=\"mira-info\">")&&out.escaped(name)&&
               out.add(text)&&out.escaped(cached)&&out.add("</span></b> ")&&
               out.escaped(field(item,"unit"))&&out.add("</div>");
            any=true;
        }else if(!strcmp(type,"range")&&id>0){
            int lo=number(item,"min"),hi=number(item,"max"),val=number(item,"value");
            if(lo>=hi||hi>100000||lo<-100000)continue;
            if(val<lo)val=lo;
            if(val>hi)val=hi;
            char cached[160]={0};
            if(mira_full_cached_state(name,cached,sizeof(cached))){
                char *tail=nullptr;long n=strtol(cached,&tail,10);
                if(tail&&tail!=cached&&!*tail&&n>=lo&&n<=hi)val=(int)n;
            }
            snprintf(text,sizeof(text),"<div><label for=\"%d\">",id);
            ok=out.add(text)&&out.escaped(name);
            snprintf(text,sizeof(text),
                     "</label><input type=\"range\" id=\"%d\" min=\"%d\" max=\"%d\" "
                     "value=\"%d\" onchange=\"ValidRange(this)\"></div>",
                     id,lo,hi,val);
            ok=ok&&out.add(text);
            any=true;
        }else if(!strcmp(type,"list")&&id>0){
            const char *group=field(item,"nomliste");
            if(!*group)continue;
            bool seen=false;
            for(unsigned j=0;j<group_count;j++)
                if(!strcmp(groups[j],group)){seen=true;break;}
            if(seen||group_count>=16)continue;
            snprintf(groups[group_count++],sizeof(groups[0]),"%s",group);
            ok=out.add("<div><label>")&&out.escaped(group)&&
               out.add("</label><select class=\"box\" id=\"")&&out.escaped(group)&&
               out.add("\" onchange=\"Liste(this)\">");
            const cJSON *option=nullptr;
            cJSON_ArrayForEach(option,actions){
                if(strcmp(field(option,"type"),"list")||
                   strcmp(field(option,"nomliste"),group))continue;
                int id_opt=number(option,"id");
                if(id_opt<1)continue;
                snprintf(text,sizeof(text),"<option value=\"%d\">",id_opt);
                ok=ok&&out.add(text)&&out.escaped(field(option,"nom"))&&
                   out.add("</option>");
                if(!ok)break;
            }
            ok=ok&&out.add("</select></div>");
            any=true;
        }
    }
    if(ok&&!any)ok=out.add("<p>Aucune commande programmée.</p>");
    // Restore the beta's per-ID EventSource listeners: live passive states
    // must update switches, info labels, sliders and list selections.
    if(ok)ok=out.add("<script>(function(){if(!window.EventSource)return;"
                     "var source=new EventSource('/events');");
    const cJSON *event=nullptr;
    cJSON_ArrayForEach(event,actions){
        if(!ok)break;
        const char *type=field(event,"type");
        char js[600]={0};
        if(!strcmp(type,"toggle")){
            int off=number(event,"id0"),on=number(event,"id1");
            if(off<1||on<1)continue;
            snprintf(js,sizeof(js),
              "source.addEventListener('%d',function(){var e=document.getElementById('%d');if(e)e.checked=false;});"
              "source.addEventListener('%d',function(){var e=document.getElementById('%d');if(e)e.checked=true;});",
              off,off,on,off);
        }else if(!strcmp(type,"info")||!strcmp(type,"range")||
                 !strcmp(type,"inputbox")||!strcmp(type,"infofa")||
                 !strcmp(type,"color")){
            int id=number(event,"id");
            if(id<1)continue;
            const char *prop=!strcmp(type,"info")?"textContent":
                             !strcmp(type,"infofa")?"className":"value";
            snprintf(js,sizeof(js),
                "source.addEventListener('%d',function(v){var e=document.getElementById('%d');"
                "if(e)e.%s=v.data;});",id,id,prop);
        }else if(!strcmp(type,"list")){
            int id=number(event,"id");
            if(id<1)continue;
            snprintf(js,sizeof(js),
                "source.addEventListener('%d',function(){var o=document.querySelector('option[value=\"%d\"]');"
                "if(o&&o.parentElement)o.parentElement.value='%d';});",id,id,id);
        }else continue;
        ok=out.add(js);
    }
    if(ok)ok=out.add("})();</script>");
    cJSON_Delete(root);
    return ok;
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
    // /load?nav=id is rendered by render_http_reference().
    if(nav=="checkupdate")return mira_beta_system_html;
    return nullptr;
}
// Preserve the historic WT32 "Requetes HTTP" command-reference page.
// Previously /load?nav=id accidentally displayed the Parameters page.
static bool render_http_reference(AsyncWebServerRequest *r){
    if(r->url()!="/load"||q(r,"nav")!="id")return false;
    char pass[97]={0};
    bool authenticated=mira_beta_web_auth_key_copy(pass,sizeof(pass))&&
                       r->authenticate("admin",pass);
    memset(pass,0,sizeof(pass));
    if(!authenticated){r->requestAuthentication();return true;}
    PsramPage out;
    bool ok=out.add(R"HTML(<!doctype html><html lang="fr"><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Mira Panel - Requêtes HTTP</title>
<style>body{margin:0;background:#7193A0;font-family:Arial,sans-serif;color:#263238}
header{background:#1E5064;color:white;padding:20px;font-size:22px;font-weight:700}
main{max-width:900px;margin:30px auto;padding:20px;background:white;border-radius:18px}
article{padding:14px 0;border-bottom:1px solid #d9e2e6}
code{overflow-wrap:anywhere;background:#f5f8f9;padding:5px}
a{color:#1E5064}p{line-height:1.5}</style>
<header>Mira Panel · Requêtes HTTP</header><main>
<p><a href="/load?nav=params">Retour aux paramètres</a></p>
<p>Identifiants issus de la programmation Mira active. Remplace
<code>VOTRE_CLE</code> dans les exemples par la clé locale ; elle n'est jamais affichée ici.</p>)HTML");
    char *raw=nullptr;size_t len=0;
    const bool index_ok=mira_jeedom_async_index_snapshot(&raw,&len)==ESP_OK&&raw;
    cJSON *doc=index_ok?cJSON_ParseWithLength(raw,len):nullptr;
    heap_caps_free(raw);
    const cJSON *actions=doc?cJSON_GetObjectItemCaseSensitive(doc,"actions"):nullptr;
    bool any=false;
    const cJSON *action=nullptr;
    cJSON_ArrayForEach(action,actions){
        if(!ok)break;
        const char *type=field(action,"type"),*name=field(action,"nom");
        if(!*name)continue;
        int id=number(action,"id");
        char example[300]={0};
        if(!strcmp(type,"toggle")){
            int off=number(action,"id0"),on=number(action,"id1");
            if(off<1||on<1)continue;
            snprintf(example,sizeof(example),
                     "/update?key=VOTRE_CLE&amp;toggle=%d (OFF) / %d (ON)",off,on);
        }else if((!strcmp(type,"button")||!strcmp(type,"little_button"))&&id>0)
            snprintf(example,sizeof(example),"/update?key=VOTRE_CLE&amp;button=%d",id);
        else if(!strcmp(type,"info")&&id>0)
            snprintf(example,sizeof(example),"/update?key=VOTRE_CLE&amp;info=%d&amp;value=25",id);
        else if(!strcmp(type,"range")&&id>0)
            snprintf(example,sizeof(example),"/update?key=VOTRE_CLE&amp;range=%d&amp;value=50",id);
        else if(!strcmp(type,"color")&&id>0)
            snprintf(example,sizeof(example),
                     "/update?key=VOTRE_CLE&amp;color=%d&amp;r=255&amp;g=0&amp;b=0",id);
        else if(!strcmp(type,"list")&&id>0)
            snprintf(example,sizeof(example),
                     "/update?key=VOTRE_CLE&amp;listvalue=...&amp;value=...");
        else continue;
        ok=out.add("<article><strong>")&&out.escaped(name)&&
           out.add("</strong> · ")&&out.escaped(type)&&
           out.add("<p><code>")&&out.add(example)&&
           out.add("</code></p></article>");
        any=true;
    }
    cJSON_Delete(doc);
    if(ok&&!any)ok=out.add("<p>Aucune commande HTTP programmée.</p>");
    if(ok)ok=out.add("</main></html>");
    if(!ok||!out.data){
        r->send(503,"text/plain","Memoire HTML insuffisante");return true;
    }
    size_t size=out.len;
    auto owner=std::shared_ptr<char>(out.release(),[](char *p){heap_caps_free(p);});
    auto *response=r->beginResponse("text/html; charset=utf-8",size,
       [owner,size](uint8_t *dst,size_t max,size_t offset)->size_t{
           if(offset>=size)return 0;
           size_t n=std::min(max,size-offset);
           memcpy(dst,owner.get()+offset,n);
           return n;
       });
    response->addHeader("Cache-Control","no-store");
    r->send(response);
    return true;
}

bool mira_async_render_page(AsyncWebServerRequest *r){
    if(render_http_reference(r))return true;
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


// Native configured beta form handlers. Existing NVS keys and validation
// contracts are retained; flash writes run off the AsyncTCP event callbacks.
static bool authorized(AsyncWebServerRequest *r){
    char key[97]={0};
    bool ok=mira_beta_web_auth_key_copy(key,sizeof(key))&&
            r->authenticate("admin",key);
    memset(key,0,sizeof(key));
    if(!ok)r->requestAuthentication();
    return ok;
}
static String post(AsyncWebServerRequest *r,const char *name){
    const auto *p=r->getParam(name,true);
    return p?p->value():String();
}
static bool param_exists(AsyncWebServerRequest *r,const char *name){
    return r->hasParam(name,true);
}
static void redirect(AsyncWebServerRequest *r,const char *url){
    r->redirect(url);
}
static void delayed_restart(void *){
    vTaskDelay(pdMS_TO_TICKS(1350));esp_restart();
}
static void queue_restart(){
    xTaskCreate(delayed_restart,"mira_web_restart",3072,NULL,2,NULL);
}
static bool save_network(AsyncWebServerRequest *r,bool editing){
    String ssid=post(r,"ssid"),pass=post(r,"pass");
    String mode=post(r,"ip_mode"),ip=post(r,"ip"),mask=post(r,"mask");
    String gateway=post(r,"gateway"),dns1=post(r,"dns1"),dns2=post(r,"dns2");
    if(!ssid.length()||ssid.length()>32||pass.length()>63||
       (pass.length()&&pass.length()<8)||
       (mode!="auto"&&mode!="manual"&&mode!="fixed_auto")){
        r->send(400,"text/plain","Configuration Wi-Fi invalide");return true;
    }
    mira_beta_network_t *known=(mira_beta_network_t*)heap_caps_calloc(
        MIRA_NET_MAX,sizeof(mira_beta_network_t),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!known){r->send(503,"text/plain","Memoire Wi-Fi indisponible");return true;}
    size_t count=0;esp_err_t err=mira_beta_networks_list(known,&count,MIRA_NET_MAX);
    int id=-1;
    if(editing){
        String network=q(r,"network");
        id=network.length()?network.toInt():-1;
        mira_beta_network_t found={};
        if(id<0||mira_beta_networks_get(id,&found)!=ESP_OK)err=ESP_ERR_NOT_FOUND;
    }else if(err==ESP_OK){
        for(size_t i=0;i<count;i++)
            if(ssid==known[i].ssid){id=known[i].id;break;}
    }
    if(id<0&&err==ESP_OK&&!editing){
        if(count==MIRA_NET_MAX)err=ESP_ERR_INVALID_SIZE;
        for(int candidate=0;err==ESP_OK&&candidate<=999;candidate++){
            bool found=false;
            for(size_t i=0;i<count;i++)
                if(candidate==known[i].id){found=true;break;}
            if(!found){id=candidate;break;}
        }
    }
    heap_caps_free(known);
    if(err!=ESP_OK||id<0){r->send(400,"text/plain","Profil Wi-Fi indisponible");return true;}
    mira_beta_network_t profile={};
    profile.id=id;
    snprintf(profile.ssid,sizeof(profile.ssid),"%s",ssid.c_str());
    snprintf(profile.ip_mode,sizeof(profile.ip_mode),"%s",mode.c_str());
    snprintf(profile.ip,sizeof(profile.ip),"%s",ip.c_str());
    snprintf(profile.mask,sizeof(profile.mask),"%s",mask.c_str());
    snprintf(profile.gateway,sizeof(profile.gateway),"%s",gateway.c_str());
    snprintf(profile.dns1,sizeof(profile.dns1),"%s",dns1.c_str());
    snprintf(profile.dns2,sizeof(profile.dns2),"%s",dns2.c_str());
    // Editing a saved profile does not immediately change the live Wi-Fi.
    if(!editing){
        err=mira_recovery_save_beta_wifi(ssid.c_str(),pass.c_str(),mode.c_str(),
             ip.c_str(),mask.c_str(),gateway.c_str(),dns1.c_str(),dns2.c_str());
    }
    if(err==ESP_OK)err=mira_beta_networks_upsert(&profile,pass.c_str());
    pass=String();
    if(err!=ESP_OK){r->send(503,"text/plain","Sauvegarde Wi-Fi impossible");return true;}
    redirect(r,"/load?nav=listnetwork");return true;
}
static bool save_mqtt(AsyncWebServerRequest *r){
    String name=post(r,"MQTT_Nom"),host=post(r,"MQTT_Serveur");
    String user=post(r,"MQTT_Utilisateur"),pass=post(r,"MQTT_Mot_de_passe");
    String port=post(r,"MQTT_Port"),active=post(r,"MQTT_Actif");
    long p=port.toInt();
    if(name.length()>=48||host.length()>=128||user.length()>=64||
       pass.length()>=96||p<1||p>65535||
       (active!="0"&&active!="1")){
        r->send(400,"text/plain","Configuration MQTT invalide");return true;
    }
    nvs_handle_t nv;
    esp_err_t err=nvs_open("mira_idf_mqtt",NVS_READWRITE,&nv);
    if(err==ESP_OK){
        err=nvs_set_str(nv,"screen",name.c_str());
        if(err==ESP_OK)err=nvs_set_str(nv,"host",host.c_str());
        if(err==ESP_OK)err=nvs_set_str(nv,"username",user.c_str());
        if(err==ESP_OK&&pass.length())err=nvs_set_str(nv,"secret",pass.c_str());
        if(err==ESP_OK)err=nvs_set_u16(nv,"port",(uint16_t)p);
        if(err==ESP_OK)err=nvs_set_u8(nv,"enabled",active=="1"?1:0);
        if(err==ESP_OK)err=nvs_commit(nv);
        nvs_close(nv);
    }
    pass=String();
    if(err!=ESP_OK){r->send(503,"text/plain","MQTT non sauvegarde");return true;}
    (void)mira_mqtt_init();
    redirect(r,"/load?nav=mqtt");return true;
}
static bool save_urls(AsyncWebServerRequest *r){
    size_t len=0;
    char *raw=mira_jeedom_async_lvgl_snapshot(&len);
    if(!raw){r->send(404,"text/plain","Widgets absents");return true;}
    cJSON *root=cJSON_ParseWithLength(raw,len);
    free(raw);
    if(!root){r->send(400,"text/plain","JSON widgets invalide");return true;}
    cJSON *arr=cJSON_GetObjectItemCaseSensitive(root,"lvgl");
    cJSON *item=nullptr;
    cJSON_ArrayForEach(item,arr){
        const char *name=field(item,"name"),*type=field(item,"type");
        if(!*name||!strcmp(type,"info")||!strcmp(type,"datetime")||
           !param_exists(r,name))continue;
        String url=post(r,name);
        cJSON *value=cJSON_CreateString(url.c_str());
        if(value)cJSON_ReplaceItemInObjectCaseSensitive(item,"url",value);
    }
    char *json=cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if(!json){r->send(503,"text/plain","Memoire configuration insuffisante");return true;}
    char answer[112]={0};
    int code=mira_jeedom_async_programui(json,answer,sizeof(answer));
    cJSON_free(json);
    if(code!=200){r->send(code,"text/plain",answer);return true;}
    redirect(r,"/load?nav=params");return true;
}
static bool save_display(AsyncWebServerRequest *r){
    static const struct {const char *form,*key;unsigned min,max;} keys[]={
        {"DISPLAY_Jour","day_pct",1,100},
        {"DISPLAY_Nuit","night_pct",1,100},
        {"DISPLAY_Delai","sleep_delay",0,86400},
        {"DISPLAY_Veille","sleep_light",0,100}
    };
    unsigned values[4]={0};
    for(int i=0;i<4;i++){
        String raw=post(r,keys[i].form);
        if(!raw.length()){r->send(400,"text/plain","Reglage affichage absent");return true;}
        for(unsigned j=0;j<raw.length();j++)if(!isdigit((unsigned char)raw[j])){
            r->send(400,"text/plain","Reglage affichage non numerique");return true;
        }
        long v=raw.toInt();
        if(v<(long)keys[i].min||v>(long)keys[i].max){
            r->send(400,"text/plain","Reglage affichage hors limites");return true;
        }
        values[i]=(unsigned)v;
    }
    String start=post(r,"DISPLAY_DebutNuit"),end=post(r,"DISPLAY_FinNuit");
    auto time_valid=[](const String& s)->bool{
        if(s.length()!=5||s[2]!=':')return false;
        for(int i=0;i<5;i++)if(i!=2&&!isdigit((unsigned char)s[i]))return false;
        return s.substring(0,2).toInt()<24&&s.substring(3).toInt()<60;
    };
    if(!time_valid(start)||!time_valid(end)){
        r->send(400,"text/plain","Horaire jour/nuit invalide");return true;
    }
    bool night=param_exists(r,"DISPLAY_NuitAuto_Check");
    bool clock=param_exists(r,"DISPLAY_HorlogeVeille_Check");
    nvs_handle_t nv;
    esp_err_t err=nvs_open("mira_idf_ui",NVS_READWRITE,&nv);
    if(err==ESP_OK){
        for(int i=0;i<4&&err==ESP_OK;i++)
            err=i==2?nvs_set_u32(nv,keys[i].key,values[i]):
                     nvs_set_u8(nv,keys[i].key,(uint8_t)values[i]);
        if(err==ESP_OK)err=nvs_set_u8(nv,"sleep_enabled",values[2]?1:0);
        if(err==ESP_OK)err=nvs_set_u8(nv,"night_auto",night?1:0);
        if(err==ESP_OK)err=nvs_set_u8(nv,"sleep_clock",clock?1:0);
        if(err==ESP_OK)err=nvs_set_str(nv,"night_start",start.c_str());
        if(err==ESP_OK)err=nvs_set_str(nv,"night_end",end.c_str());
        if(err==ESP_OK)err=nvs_commit(nv);
        nvs_close(nv);
    }
    if(err!=ESP_OK){r->send(503,"text/plain","Reglages non sauvegardes");return true;}
    mira_full_request_settings_reload();
    redirect(r,"/load?nav=params");return true;
}

// Same nonblocking Wi-Fi selector behavior as the WT32 web manager.
// The scan runs in a temporary task and never blocks the AsyncTCP thread.
static portMUX_TYPE wifi_scan_lock=portMUX_INITIALIZER_UNLOCKED;
static bool wifi_scan_busy=false,wifi_scan_ready=false,wifi_scan_failed=false;
static int64_t wifi_scan_finished_us=0;
static char wifi_scan_ssids[12][33]={{0}};
static uint16_t wifi_scan_count=0;
static void async_wifi_scan_task(void *){
    wifi_ap_record_t records[12]={};
    uint16_t count=12;
    const esp_err_t result=esp_wifi_scan_start(nullptr,true);
    esp_err_t read=result;
    if(result==ESP_OK)read=esp_wifi_scan_get_ap_records(&count,records);
    if(read!=ESP_OK)count=0;
    portENTER_CRITICAL(&wifi_scan_lock);
    wifi_scan_count=count;
    for(uint16_t i=0;i<count;i++){
        memcpy(wifi_scan_ssids[i],records[i].ssid,32);
        wifi_scan_ssids[i][32]=0;
    }
    wifi_scan_failed=read!=ESP_OK;
    wifi_scan_finished_us=esp_timer_get_time();
    wifi_scan_ready=true;
    wifi_scan_busy=false;
    portEXIT_CRITICAL(&wifi_scan_lock);
    vTaskDelete(nullptr);
}
void mira_async_wifi_scan(AsyncWebServerRequest *r){
    bool start=false;
    char names[12][33]={{0}};
    uint16_t count=0;
    bool busy=false,failed=false;
    const int64_t now=esp_timer_get_time();
    portENTER_CRITICAL(&wifi_scan_lock);
    if(!wifi_scan_busy&&
       (!wifi_scan_ready||now-wifi_scan_finished_us>60000000LL)){
        wifi_scan_busy=true;
        start=true;
    }
    busy=wifi_scan_busy;
    failed=wifi_scan_failed;
    if(wifi_scan_ready&&!busy){
        count=wifi_scan_count;
        for(uint16_t i=0;i<count;i++)memcpy(names[i],wifi_scan_ssids[i],33);
    }
    portEXIT_CRITICAL(&wifi_scan_lock);
    if(start&&xTaskCreate(async_wifi_scan_task,"mira_scan_wifi",4096,
                          NULL,2,NULL)!=pdPASS){
        portENTER_CRITICAL(&wifi_scan_lock);
        wifi_scan_busy=false;wifi_scan_failed=true;wifi_scan_ready=true;
        wifi_scan_finished_us=now;
        portEXIT_CRITICAL(&wifi_scan_lock);
        busy=false;failed=true;
    }
    cJSON *root=cJSON_CreateObject();
    cJSON *array=root?cJSON_AddArrayToObject(root,"networks"):nullptr;
    if(!array){cJSON_Delete(root);r->send(503,"text/plain","Scan impossible");return;}
    cJSON_AddBoolToObject(root,"busy",busy);
    cJSON_AddBoolToObject(root,"failed",!busy&&failed);
    for(uint16_t i=0;i<count;i++)
        if(names[i][0])cJSON_AddItemToArray(array,cJSON_CreateString(names[i]));
    char *json=cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if(!json){r->send(503,"text/plain","JSON scan indisponible");return;}
    r->send(200,"application/json",json);
    cJSON_free(json);
}

static volatile uint32_t network_nonce=0;
static volatile int64_t network_nonce_started=0;
bool mira_async_handle_beta(AsyncWebServerRequest *r){
    const String path=r->url();
    if(path=="/update"&&r->method()==AsyncWebRequestMethod::HTTP_POST){
        if(!authorized(r))return true;
        static const char *const kinds[]={
            "send","send_range","send_list","send_color","send_textbox"
        };
        for(const char *kind:kinds){
            if(!r->hasParam(kind))continue;
            const String target=q(r,kind);
            const String list=q(r,"list"),value=q(r,"value");
            const String text=q(r,"text");
            const String red=q(r,"r"),green=q(r,"g"),blue=q(r,"b");
            const int status=mira_jeedom_async_browser_action(
                kind,target.c_str(),list.c_str(),
                !strcmp(kind,"send_textbox")?text.c_str():value.c_str(),
                red.c_str(),green.c_str(),blue.c_str());
            r->send(status,"text/plain; charset=utf-8",
                    status==200?"OK":"Commande Mira indisponible");
            return true;
        }
        r->send(400,"text/plain","Type de commande historique absent");
        return true;
    }

    if(path=="/mira/wifi/scan"&&r->method()==AsyncWebRequestMethod::HTTP_GET){
        if(!authorized(r))return true;
        r->send(200,"application/json",
                "{\"busy\":false,\"failed\":false,\"networks\":[]}");
        return true;
    }
    if(path=="/mira/network"&&r->method()==AsyncWebRequestMethod::HTTP_POST){
        if(!authorized(r))return true;
        String action=q(r,"action"),id=q(r,"network"),nonce=post(r,"nonce");
        const uint32_t key=(uint32_t)strtoul(nonce.c_str(),nullptr,16);
        if((action!="remove"&&action!="activate")||!id.length()||
           !network_nonce||key!=network_nonce||
           esp_timer_get_time()-network_nonce_started>180000000LL){
            r->send(403,"text/plain","Confirmation reseau absente ou expiree");return true;
        }
        network_nonce=0;
        esp_err_t err=action=="remove"?
            mira_beta_networks_remove(id.toInt()):
            mira_beta_networks_activate(id.toInt());
        if(err!=ESP_OK){r->send(503,"text/plain","Operation reseau impossible");return true;}
        redirect(r,"/load?nav=listnetwork");return true;
    }
    if(path=="/load"&&q(r,"nav")=="removenetwork"){
        if(!authorized(r))return true;
        String id=q(r,"network");
        mira_beta_network_t p={};
        if(!id.length()||mira_beta_networks_get(id.toInt(),&p)!=ESP_OK){
            r->send(404,"text/plain","Reseau inconnu");return true;
        }
        network_nonce=esp_random()|1u;network_nonce_started=esp_timer_get_time();
        char html[780]={0};
        snprintf(html,sizeof(html),
          "<!doctype html><meta charset='utf-8'><h2>Confirmer la suppression du reseau %.32s</h2>"
          "<form method='post' action='/mira/network?action=remove&amp;network=%d'>"
          "<input type='hidden' name='nonce' value='%08x'>"
          "<button type='submit'>Supprimer</button></form>",
          p.ssid,p.id,(unsigned)network_nonce);
        r->send(200,"text/html; charset=utf-8",html);
        return true;
    }
    if(path=="/save"&&r->method()==AsyncWebRequestMethod::HTTP_POST){
        if(!authorized(r))return true;
        String conf=q(r,"config"),update=q(r,"update");
        if(update=="wifi")return save_network(r,true);
        if(conf=="wifi")return save_network(r,false);
        if(conf=="AP"){
            String pass=post(r,"pass"),ip=post(r,"ip"),active=post(r,"actif");
            if(active!="0"&&active!="1"){
                r->send(400,"text/plain","Point d'acces invalide");return true;
            }
            esp_err_t err=mira_recovery_save_beta_ap(pass.c_str(),ip.c_str(),active=="1");
            pass=String();
            if(err!=ESP_OK){r->send(400,"text/plain","Configuration AP invalide");return true;}
            r->send(200,"text/html","<h2>Point d'acces enregistre. Redemarrage...</h2>");
            queue_restart();
            return true;
        }
        if(conf=="params"&&param_exists(r,"MQTT_Actif"))return save_mqtt(r);
        if(conf=="params")return save_urls(r);
        r->send(400,"text/plain","Formulaire inconnu");return true;
    }
    if(path=="/display-save"&&r->method()==AsyncWebRequestMethod::HTTP_POST){
        if(!authorized(r))return true;
        return save_display(r);
    }
    if(path=="/reboot"&&r->method()==AsyncWebRequestMethod::HTTP_GET){
        if(!authorized(r))return true;
        r->send(200,"text/plain","Mira Panel redemarre");
        queue_restart();return true;
    }
    if(path=="/api/checkupdate"&&r->method()==AsyncWebRequestMethod::HTTP_GET){
        if(!authorized(r))return true;
        r->send(200,"application/json",
                "{\"status\":\"manual\",\"message\":\"Utiliser le formulaire OTA\"}");
        return true;
    }
    if(path=="/mira/clock/status"&&r->method()==AsyncWebRequestMethod::HTTP_GET){
        if(!authorized(r))return true;
        time_t now=time(nullptr);
        char msg[100];
        snprintf(msg,sizeof(msg),"{\"epoch\":%lld,\"valid\":%s}",
                 (long long)now,now>1700000000?"true":"false");
        r->send(200,"application/json",msg);
        return true;
    }
    return false;
}
