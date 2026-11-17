// Mira Panel 7STD: WT32-compatible event-driven public HTTP transport.
// ESPAsyncWebServer on :80 owns /getindex /updatenr /events natively.
// Unported legacy endpoints are transparently proxied by bounded workers to
// the ESP-IDF compatibility server on 127.0.0.1:8081. No plugin redirect.
// First-run and low-memory rescue stay on native :80 (no async front end).
#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <memory>
#include <new>
#include <string>
#include <strings.h>
#include <cstring>
#include <algorithm>
#include <esp_ota_ops.h>
#include <esp_app_desc.h>
#include <esp_system.h>
#include <esp_wifi.h>
#include <esp_netif.h>
#include <nvs.h>
#include "mira_beta_setup_pages.h"

#include "mira_async_web.h"
extern "C" {
#include "mira_jeedom.h"
#include "mira_beta_web.h"
#include "mira_beta_setup.h"
#include "mira_recovery.h"
}

static const char *TAG="mira7-async";
static AsyncWebServer server(80);
static AsyncEventSource events("/events");
static bool started=false;
static QueueHandle_t proxy_queue=nullptr;
#define MIRA_PROXY_MAX_BODY 131072u
#define MIRA_PROXY_MAX_RESPONSE 262144u
#define MIRA_SSE_LIMIT 640u
#define MIRA_SSE_BURST 20u

struct ProxyJob {
    AsyncWebServerRequestPtr request;
};
struct SsePrevious {int id;char value[160];};
static SsePrevious *previous=nullptr;
static uint32_t replay_epoch=0;

static bool public_key(AsyncWebServerRequest *r){
    const auto *p=r->getParam("key");
    return p&&mira_jeedom_async_key_valid(p->value().c_str());
}
static void forbidden(AsyncWebServerRequest *r){
    r->send(403,"text/plain; charset=utf-8","Cle Mira invalide");
}
static String param_or_empty(AsyncWebServerRequest *r,const char *name){
    const auto *p=r->getParam(name);
    return p?p->value():String();
}
static const char* selected_kind(AsyncWebServerRequest *r,String& value){
    static const char *types[]={"info","toggle","toogle","range","list","listvalue","color","button"};
    for(const char *kind:types){
        const auto *p=r->getParam(kind);
        if(p){value=p->value();return kind;}
    }
    return nullptr;
}
static void index_get(AsyncWebServerRequest *r){
    if(!public_key(r)){forbidden(r);return;}
    char *buf=nullptr;
    size_t len=0;
    esp_err_t err=mira_jeedom_async_index_snapshot(&buf,&len);
    if(err!=ESP_OK||!buf){
        r->send(err==ESP_ERR_NOT_FOUND?404:503,"text/plain",
            err==ESP_ERR_NOT_FOUND?"Index Mira absent":"Index Mira occupe");
        return;
    }
    // The asynchronous response callback may be invoked long after this
    // request handler returns. shared_ptr owns the PSRAM snapshot until done.
    auto owner=std::shared_ptr<char>(buf,[](char *p){heap_caps_free(p);});
    AsyncWebServerResponse *resp=r->beginResponse(
        "application/json",len,
        [owner,len](uint8_t *out,size_t max,size_t offset)->size_t{
            if(offset>=len)return 0;
            size_t n=std::min(max,len-offset);
            memcpy(out,owner.get()+offset,n);
            return n;
        });
    resp->addHeader("Cache-Control","no-store");
    r->send(resp);
}
static void updatenr_get(AsyncWebServerRequest *r){
    if(!public_key(r)){forbidden(r);return;}
    String selector;
    const char *kind=selected_kind(r,selector);
    if(!kind){r->send(400,"text/plain","Type d'action absent");return;}
    String value=param_or_empty(r,"value");
    String red=param_or_empty(r,"r");
    String green=param_or_empty(r,"g");
    String blue=param_or_empty(r,"b");
    // Preserve absence vs empty value, and the legacy numeric/list semantics.
    const auto *incoming=r->getParam("value");
    const auto *rp=r->getParam("r"),*gp=r->getParam("g"),*bp=r->getParam("b");
    char response[64]={0};
    int code=mira_jeedom_async_updatenr(kind,selector.c_str(),
        incoming?value.c_str():nullptr,
        rp?red.c_str():nullptr,gp?green.c_str():nullptr,bp?blue.c_str():nullptr,
        response,sizeof(response));
    r->send(code,"text/plain; charset=utf-8",response);
}
static void getmodule_get(AsyncWebServerRequest *r){
    if(!public_key(r)){forbidden(r);return;}
    r->send(200,"text/plain","Mira Panel");
}
static void testmodule_get(AsyncWebServerRequest *r){
    if(!public_key(r)){forbidden(r);return;}
    r->send(200,"text/plain","Tout est OK");
}

// WT32-style first-run wizard on the same AsyncWebServer. Legacy native
// HTTPD never binds port 80, including while the device is unconfigured.
static bool setup_mode(){
    return !mira_beta_setup_runtime_configured()&&mira_beta_setup_unconfigured();
}
static bool setup_client_allowed(AsyncWebServerRequest *r){
    (void)r;
    if(!setup_mode())return false;
    // When the user key is absent, recovery.c starts Wi-Fi in AP-only
    // mode. The STA interface therefore cannot expose the open wizard.
    esp_netif_t *sta=esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t sta_ip={};
    wifi_mode_t mode=WIFI_MODE_NULL;
    return sta&&esp_wifi_get_mode(&mode)==ESP_OK&&
           (mode==WIFI_MODE_AP||mode==WIFI_MODE_APSTA)&&
           esp_netif_get_ip_info(sta,&sta_ip)==ESP_OK&&
           sta_ip.ip.addr==0;
}
static void setup_wifi_page(AsyncWebServerRequest *r){
    String html(mira_beta_wifi_page);
    html.replace("MIRA_SCAN_SLOT",
        "<label>Reseau Wi-Fi</label><p>Saisir le SSID ci-dessous</p>");
    r->send(200,"text/html; charset=utf-8",html);
}
static void restart_after_response(void *){
    vTaskDelay(pdMS_TO_TICKS(1500));
    esp_restart();
}
static void schedule_restart(){
    xTaskCreate(restart_after_response,"mira_async_restart",3072,NULL,2,NULL);
}
static bool async_first_run(AsyncWebServerRequest *r){
    if(!setup_mode())return false;
    const String path=r->url();
    if(path=="/"||path=="/load"){
        if(!setup_client_allowed(r)){forbidden(r);return true;}
        String nav=param_or_empty(r,"nav");
        if(nav=="reboot"){
            int code=mira_beta_setup_async_keep_ap();
            if(code==200){
                r->send(200,"text/html; charset=utf-8",
                        "<h2>Mira Panel</h2><p>Redemarrage du panneau...</p>");
                schedule_restart();
            }else r->send(code,"text/plain","Echec configuration AP");
            return true;
        }
        if(nav=="key"){
            r->send_P(200,"text/html; charset=utf-8",mira_beta_key_page);
            return true;
        }
        if(mira_beta_web_auth_key_present()){
            setup_wifi_page(r);
        }else r->send_P(200,"text/html; charset=utf-8",mira_beta_key_page);
        return true;
    }
    if(path=="/mira/wifi/scan"){
        if(!setup_client_allowed(r)){forbidden(r);return true;}
        r->send(200,"application/json",
                "{\"networks\":[],\"busy\":false,\"failed\":false}");
        return true;
    }
    if(path=="/save"&&r->method()==AsyncWebRequestMethod::HTTP_POST){
        if(!setup_client_allowed(r)){forbidden(r);return true;}
        String config=param_or_empty(r,"config");
        auto field=[r](const char *name)->String {
            const auto *p=r->getParam(name,true);
            return p?p->value():String();
        };
        if(config=="key"){
            String key=field("key"),confirm=field("ver");
            int code=mira_beta_setup_async_key_save(key.c_str(),confirm.c_str());
            key=String();confirm=String();
            if(code!=200){r->send(code,"text/plain","Cle ou confirmation invalide");return true;}
            wifi_config_t sta={};
            if(esp_wifi_get_config(WIFI_IF_STA,&sta)==ESP_OK&&sta.sta.ssid[0]){
                r->send(200,"text/html; charset=utf-8",
                        "<h2>Cle Mira restauree</h2><p>Redemarre Mira Panel pour reprendre l'interface.</p>");
                return true;
            }
            setup_wifi_page(r);
            return true;
        }
        if(config=="wifi"){
            String ssid=field("ssid"),password=field("pass");
            String mode=field("ip_mode"),ip=field("ip"),mask=field("mask");
            String gw=field("gateway"),dns1=field("dns1"),dns2=field("dns2");
            if(!mode.length())mode=ip.length()?"fixed_auto":"auto";
            int code=mira_beta_setup_async_wifi_save(
               ssid.c_str(),password.c_str(),mode.c_str(),ip.c_str(),
               mask.c_str(),gw.c_str(),dns1.c_str(),dns2.c_str());
            password=String();
            if(code==200){
                r->send(200,"text/html; charset=utf-8",
                        "<h2>Mira Panel</h2><p>Wi-Fi enregistre. Redemarrage...</p>");
                schedule_restart();
            }else r->send(code,"text/plain","Profil Wi-Fi invalide ou indisponible");
            return true;
        }
        r->send(400,"text/plain","Configuration inconnue");
        return true;
    }
    // Never expose configured pages or OTA on an unconfigured AP.
    r->send(404,"text/plain","Configuration Mira en cours");
    return true;
}


static bool basic_auth(AsyncWebServerRequest *r);
// WT32 manual OTA contract: POST /doUpdateFile, multipart part "update".
// Each firmware block is streamed to the inactive ESP-IDF OTA partition;
// the full binary is NEVER buffered in RAM, and no HTTPD task is involved.
struct AsyncOtaState {
    esp_ota_handle_t handle=0;
    const esp_partition_t *target=nullptr;
    size_t written=0;
    bool begun=false;
    bool failed=false;
    bool completed=false;
    bool verified=false;
    bool owns_slot=false;
};
static bool ota_busy=false;
static volatile unsigned ota_last_status=0;
static volatile size_t ota_last_bytes=0;
static void ota_upload(AsyncWebServerRequest *r,const String& filename,
                       size_t index,uint8_t *data,size_t len,bool final){
    AsyncOtaState *ctx=(AsyncOtaState *)r->_tempObject;
    if(index==0){
        if(ctx){ctx->failed=true;return;}
        ctx=(AsyncOtaState *)heap_caps_calloc(1,sizeof(*ctx),
                                            MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        if(!ctx)return;
        r->_tempObject=ctx;
        if(!basic_auth(r)||filename.length()<5||
           !filename.endsWith(".bin")||
           !r->contentLength()||r->contentLength()<65536){
            ctx->failed=true;
            ota_last_status=403;
            return;
        }
        if(__atomic_exchange_n(&ota_busy,true,__ATOMIC_ACQ_REL)){
            ctx->failed=true;ota_last_status=409;return;
        }
        ctx->owns_slot=true;
        ctx->target=esp_ota_get_next_update_partition(NULL);
        if(!ctx->target||r->contentLength()>ctx->target->size+4096u||
           !len||!data||data[0]!=0xe9){
            ctx->failed=true;ota_last_status=400;
            return;
        }
        esp_err_t err=esp_ota_begin(ctx->target,OTA_SIZE_UNKNOWN,&ctx->handle);
        if(err!=ESP_OK){
            ctx->failed=true;ota_last_status=503;
            ESP_LOGW(TAG,"ASYNC_OTA_BEGIN_FAILED: %s",esp_err_to_name(err));
            return;
        }
        ctx->begun=true;
        ESP_LOGI(TAG,"ASYNC_OTA_STARTED: partition=%s",ctx->target->label);
    }
    if(!ctx||ctx->failed)return;
    if(len>0){
        if(index!=ctx->written||ctx->written+len>ctx->target->size||
           esp_ota_write(ctx->handle,data,len)!=ESP_OK){
            ctx->failed=true;ota_last_status=500;
            return;
        }
        ctx->written+=len;
    }
    if(!final)return;
    if(!ctx->begun||ctx->written<65536||ctx->written>ctx->target->size){
        ctx->failed=true;ota_last_status=400;return;
    }
    esp_err_t err=esp_ota_end(ctx->handle);
    ctx->begun=false;
    if(err!=ESP_OK){
        ctx->failed=true;ota_last_status=400;
        ESP_LOGW(TAG,"ASYNC_OTA_IMAGE_INVALID: %s",esp_err_to_name(err));
        return;
    }
    esp_app_desc_t descriptor={};
    const esp_app_desc_t *running=esp_app_get_description();
    if(!running||
       esp_ota_get_partition_description(ctx->target,&descriptor)!=ESP_OK||
       strcmp(descriptor.project_name,running->project_name)||
       strcmp(descriptor.project_name,"mira_idf_7std")){
        ctx->failed=true;ota_last_status=400;
        ESP_LOGW(TAG,"ASYNC_OTA_REJECTED: not a Mira 7 STD application");
        return;
    }
    err=esp_ota_set_boot_partition(ctx->target);
    if(err!=ESP_OK){
        ctx->failed=true;ota_last_status=500;
        ESP_LOGE(TAG,"ASYNC_OTA_SET_BOOT_FAILED: %s",esp_err_to_name(err));
        return;
    }
    ctx->verified=true;
    ctx->completed=true;
    ota_last_bytes=ctx->written;
    ota_last_status=200;
    ESP_LOGI(TAG,"ASYNC_OTA_VERIFIED: %u bytes",(unsigned)ctx->written);
}
static void ota_upload_complete(AsyncWebServerRequest *r){
    auto *ctx=(AsyncOtaState *)r->_tempObject;
    if(!ctx){
        r->send(503,"text/plain; charset=utf-8","OTA non initialise");
        return;
    }
    bool ok=ctx->completed&&ctx->verified&&!ctx->failed;
    if(ctx->begun)esp_ota_abort(ctx->handle);
    ctx->begun=false;
    r->_tempObject=nullptr;
    const bool owned=ctx->owns_slot;
    heap_caps_free(ctx);
    if(owned)__atomic_store_n(&ota_busy,false,__ATOMIC_RELEASE);
    if(!basic_auth(r)){r->requestAuthentication();return;}
    if(!ok){
        r->send(400,"text/plain; charset=utf-8",
                "UPDATE_FAILED - firmware precedent conserve");
        return;
    }
    r->send(200,"text/plain; charset=utf-8","OK - Mira redemarre");
    schedule_restart();
}
static void ota_page(AsyncWebServerRequest *r){
    if(!basic_auth(r)){r->requestAuthentication();return;}
    static const char html[]=R"HTML(<!doctype html><html lang="fr">
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Mira Panel - Firmware</title>
<style>body{background:#7193A0;color:#263238;font-family:Arial,sans-serif;padding:24px}
main{max-width:680px;margin:auto;background:#fff;border-radius:18px;padding:26px}
button{background:#D39E55;border:0;border-radius:10px;padding:12px;cursor:pointer}
progress{width:100%}</style>
<main><h2>Mira Panel — mise a jour OTA</h2>
<p>Firmware OTA <code>.bin</code> pour Waveshare 7 STD uniquement. Les parametres sont conserves.</p>
<form id="ota" method="post" action="/doUpdateFile" enctype="multipart/form-data">
<input type="file" name="update" accept=".bin" required>
<button type="submit">Installer le firmware</button></form>
<progress id="p" max="100" value="0"></progress><p id="status">Pret</p></main>
<script>document.getElementById("ota").onsubmit=function(e){
e.preventDefault();const x=new XMLHttpRequest();x.open("POST","/doUpdateFile");
x.upload.onprogress=function(v){if(v.lengthComputable)document.getElementById("p").value=100*v.loaded/v.total};
x.onload=function(){document.getElementById("status").textContent=x.status+" "+x.responseText};
x.onerror=function(){document.getElementById("status").textContent="Connexion perdue"};
x.send(new FormData(document.getElementById("ota")));}</script></html>)HTML";
    r->send_P(200,"text/html; charset=utf-8",html);
}

// Authenticated Jeedom programming: no HTTP loopback; processed in PSRAM worker.
static bool basic_auth(AsyncWebServerRequest *r);
static bool native_jeedom_request(AsyncWebServerRequest *r){
    if(async_first_run(r))return true;
    const String path=r->url();
    const bool stage_clear=path=="/removelistlvgl"||path=="/screenformat";
    const bool stage_add=path=="/addlistlvgl"||path=="/screenadd";
    const bool stage_save=path=="/savelistlvgl"||path=="/screensave";
    const bool health=path=="/mira-health";
    const bool jeedom_status=path=="/mira/jeedom/status";
    const bool camera_config=path=="/camera/config";
    const bool allparams=path=="/getallparams";
    const bool navpages=path=="/navpages";
    const bool navfavorites=path=="/navfavorites";
    const bool sync_toggle=path=="/sync/toggle";
    const bool lvgl=path=="/getlvgl";
    const bool display=path=="/displayinfo";
    const bool program=path=="/programui";
    const bool batch=path=="/actions/bindings";
    const bool single=path=="/setjeedomid";
    const bool toggle=path=="/setjeedomtoggleid";
    const bool init=path=="/sendjeedomorderid";
    const bool readstate=path=="/get"&&(r->hasParam("toggle")||
         r->hasParam("toogle")||r->hasParam("info")||r->hasParam("infofa"));
    const bool readparam=path=="/getparam"||
                         (path=="/get"&&r->hasParam("param"));
    const bool writeparam=path=="/updateparam"||path=="/addparam"||
                          path=="/addlistparam"||path=="/updatelistparam";
    const bool saveparam=path=="/savelistparam"||path=="/createlistparam";
    if(!health&&!jeedom_status&&!camera_config&&!stage_clear&&!stage_add&&!stage_save&&!allparams&&!navpages&&!navfavorites&&!sync_toggle&&!lvgl&&!display&&!program&&!batch&&!single&&!toggle&&!init&&!readstate&&!readparam&&!writeparam&&!saveparam)
        return false;
    if(jeedom_status){
        if(!public_key(r)){forbidden(r);return true;}
        size_t len=0;
        char *json=mira_jeedom_async_status_json(&len);
        if(!json){r->send(503,"text/plain","Diagnostic Jeedom indisponible");return true;}
        auto owner=std::shared_ptr<char>(json,[](char *p){heap_caps_free(p);});
        auto *resp=r->beginResponse("application/json",len,
            [owner,len](uint8_t *out,size_t max,size_t off)->size_t{
                if(off>=len)return 0;
                size_t n=std::min(max,len-off);
                memcpy(out,owner.get()+off,n);return n;
            });
        resp->addHeader("Cache-Control","no-store");
        r->send(resp);return true;
    }
    if(health){
        size_t len=0;
        char *json=mira_jeedom_async_health_json(&len);
        if(!json){r->send(503,"text/plain","Diagnostic indisponible");return true;}
        auto owner=std::shared_ptr<char>(json,[](char *p){heap_caps_free(p);});
        auto *resp=r->beginResponse("application/json",len,
            [owner,len](uint8_t *out,size_t max,size_t pos)->size_t{
                if(pos>=len)return 0;
                size_t n=std::min(max,len-pos);
                memcpy(out,owner.get()+pos,n);return n;
            });
        resp->addHeader("Cache-Control","no-store");
        r->send(resp);return true;
    }
    if(camera_config){
        if(r->method()!=AsyncWebRequestMethod::HTTP_POST)return false;
        const auto *key=r->getParam("key",true);
        const auto *name=r->getParam("name",true);
        const auto *url=r->getParam("url",true);
        if(!key||!mira_jeedom_async_key_valid(key->value().c_str())){
            forbidden(r);return true;
        }
        if(!name||!url){r->send(400,"text/plain","Champs camera absents");return true;}
        int code=mira_jeedom_async_camera_config(name->value().c_str(),
                                                 url->value().c_str());
        r->send(code,"text/plain; charset=utf-8",
                code==200?"OK":"Configuration camera invalide");
        return true;
    }
    if(stage_clear||stage_add||stage_save){
        if(!public_key(r)){forbidden(r);return true;}
        int code=400;
        if(stage_clear){
            code=mira_jeedom_async_screen_clear();
            r->send(code,"text/plain; charset=utf-8",
                    code==200?(path=="/screenformat"?"Liste vidée!":"OK"):
                    "Impossible de vider la liste");
            return true;
        }
        if(stage_save){
            code=mira_jeedom_async_screen_save();
            r->send(code,"text/plain; charset=utf-8",
                    code==200?(path=="/screensave"?"Liste sauvegardée!":"OK"):
                    "Sauvegarde de liste impossible");
            return true;
        }
        static const char *const fields[]={
            "type","name","loc","min","max","value","unit",
            "style","layout","labelMode","icon","displayName",
            "lx","ly","lw","lh"
        };
        String v[16];
        for(unsigned i=0;i<16;i++){
            if(i<7&&!r->hasParam(fields[i])){
                r->send(400,"text/plain","Champs widget manquants");
                return true;
            }
            v[i]=param_or_empty(r,fields[i]);
        }
        char *preview=nullptr;
        code=mira_jeedom_async_screen_add(
             v[0].c_str(),v[1].c_str(),v[2].c_str(),
             v[3].c_str(),v[4].c_str(),v[5].c_str(),v[6].c_str(),
             v[7].c_str(),v[8].c_str(),v[9].c_str(),v[10].c_str(),
             v[11].c_str(),v[12].c_str(),v[13].c_str(),v[14].c_str(),
             v[15].c_str(),path=="/screenadd"?&preview:nullptr);
        if(code!=200||!preview){
            if(preview)free(preview);
            r->send(code,"text/plain; charset=utf-8",
                    code==200?"OK":"Programmation widget invalide");
            return true;
        }
        const size_t length=strlen(preview);
        auto owner=std::shared_ptr<char>(preview,[](char *p){free(p);});
        auto *resp=r->beginResponse("text/plain; charset=utf-8",length,
            [owner,length](uint8_t *out,size_t max,size_t off)->size_t{
                if(off>=length)return 0;
                size_t n=std::min(max,length-off);
                memcpy(out,owner.get()+off,n);
                return n;
            });
        r->send(resp);
        return true;
    }
    if(allparams){
        if(!public_key(r)){forbidden(r);return true;}
        size_t len=0;
        char *json=mira_jeedom_async_allparams(&len);
        if(!json){r->send(503,"text/plain","Parametres indisponibles");return true;}
        auto owner=std::shared_ptr<char>(json,[](char *p){free(p);});
        auto *resp=r->beginResponse("application/json",len,
            [owner,len](uint8_t *out,size_t max,size_t offset)->size_t{
                if(offset>=len)return 0;
                size_t n=std::min(max,len-offset);
                memcpy(out,owner.get()+offset,n);return n;
            });
        resp->addHeader("Cache-Control","no-store");
        r->send(resp);
        return true;
    }
    if(navpages||navfavorites){
        const bool favorites=navfavorites;
        if(r->method()==AsyncWebRequestMethod::HTTP_GET){
            if(!public_key(r)){forbidden(r);return true;}
            char body[96]={0};
            int status=mira_jeedom_async_nav_get(favorites,body,sizeof(body));
            r->send(status,"application/json",body);
            return true;
        }
        // The legacy browser POST sends an authenticated form, not a JSON
        // request body. Do not silently weaken this authentication rule.
        if(r->method()!=AsyncWebRequestMethod::HTTP_POST)return false;
        const auto *key=r->getParam("key",true);
        const auto *value=r->getParam(favorites?"json":"mask",true);
        if(!key||!mira_jeedom_async_key_valid(key->value().c_str())){
            forbidden(r);return true;
        }
        if(!value){r->send(400,"text/plain","Formulaire absent");return true;}
        int status=mira_jeedom_async_nav_post(favorites,value->value().c_str());
        r->send(status,"text/plain; charset=utf-8",
                status==200?"OK":"Parametre navigation invalide");
        return true;
    }
    if(sync_toggle){
        if(!public_key(r)){forbidden(r);return true;}
        const String id=param_or_empty(r,"id");
        char answer[64]={0};
        int code=mira_jeedom_async_updatenr("toggle",id.c_str(),
                                             nullptr,nullptr,nullptr,nullptr,
                                             answer,sizeof(answer));
        r->send(code,"text/plain; charset=utf-8",answer);
        return true;
    }
    if(readstate){
        if(!public_key(r)){forbidden(r);return true;}
        static const char *types[]={"toggle","toogle","info","infofa"};
        for(const char *type:types){
            const auto *p=r->getParam(type);
            if(!p)continue;
            char value[160]={0};
            if(!mira_jeedom_async_get_state(type,p->value().c_str(),
                                            value,sizeof(value)))return false;
            r->send(200,"text/plain; charset=utf-8",value);
            return true;
        }
    }
    if(readparam||writeparam||saveparam){
        if(!public_key(r)){forbidden(r);return true;}
        if(saveparam){r->send(200,"text/plain","OK");return true;}
        if(readparam){
            const String name=param_or_empty(r,path=="/getparam"?"value":"param");
            char value[256]={0};
            const bool found=mira_jeedom_async_param_get(name.c_str(),
                                                         value,sizeof(value));
            r->send(200,"text/plain; charset=utf-8",found?value:"Null");
            memset(value,0,sizeof(value));return true;
        }
        const String name=param_or_empty(r,"param");
        const String value=param_or_empty(r,"value");
        const int code=mira_jeedom_async_param_set(name.c_str(),value.c_str());
        r->send(code,"text/plain; charset=utf-8",
                code==200?"OK":"Parametre Jeedom invalide");
        return true;
    }
    if(lvgl||display){
        if(!public_key(r)){forbidden(r);return true;}
        if(display){
            r->send(200,"application/json",
                "{\"profile\":\"waveshare-esp32s3-touch-lcd-7\","
                "\"width\":800,\"height\":480,"
                "\"contentWidth\":800,\"contentHeight\":430,\"navHeight\":50,"
                "\"rotation\":0,\"logicalWidth\":1000,\"logicalHeight\":1000,"
                "\"httpPrimary\":true,\"mqttOptional\":true,\"freeLayout\":true,"
                "\"widgetLabelModes\":\"auto,text,icon,both\","
                "\"widgetIcons\":\"auto,temperature,humidity,pressure,light,energy,plug,door\"}");
            return true;
        }
        size_t len=0;
        char *buf=mira_jeedom_async_lvgl_snapshot(&len);
        if(!buf){
            r->send(404,"text/plain","Configuration Mira absente");return true;
        }
        auto owner=std::shared_ptr<char>(buf,[](char *p){free(p);});
        AsyncWebServerResponse *resp=r->beginResponse(
            "application/json",len,
            [owner,len](uint8_t *out,size_t max,size_t offset)->size_t{
                if(offset>=len)return 0;
                size_t count=std::min(max,len-offset);
                memcpy(out,owner.get()+offset,count);
                return count;
            });
        resp->addHeader("Cache-Control","no-store");
        r->send(resp);
        return true;
    }
    if(program){
        const auto *key=r->getParam("key",true);
        const auto *json=r->getParam("json",true);
        if(!key||!mira_jeedom_async_key_valid(key->value().c_str())){
            forbidden(r);return true;
        }
        // Only supported x-www-form-urlencoded data is consumed natively.
        if(!json)return false;
        char answer[112]={0};
        const int code=mira_jeedom_async_programui(json->value().c_str(),
                                                   answer,sizeof(answer));
        r->send(code,"text/plain; charset=utf-8",answer);
        return true;
    }
    if(batch){
        if(!mira_beta_web_auth_key_present()){
            r->send(503,"text/plain; charset=utf-8","Cle Mira indisponible");
            return true;
        }
        if(!basic_auth(r)){
            AsyncWebServerResponse *resp=r->beginResponse(
                401,"text/plain; charset=utf-8","Authentification Mira Panel requise");
            resp->addHeader("WWW-Authenticate",
                            "Basic realm=\"Mira Panel\", charset=\"UTF-8\"");
            r->send(resp);
            return true;
        }
        if(!r->hasHeader("X-Mira-Bindings")||
           r->header("X-Mira-Bindings")!="1"){
            r->send(400,"text/plain","En-tete bindings absent");
            return true;
        }
        const auto *param=r->getParam("json",true);
        // Unknown body formats retain the original compatible native handler.
        if(!param)return false;
        const String json=param->value();
        char answer[112]={0};
        const int code=mira_jeedom_async_bindings_json(json.c_str(),
                                                       answer,sizeof(answer));
        r->send(code,"text/plain; charset=utf-8",answer);
        return true;
    }
    if(!public_key(r)){forbidden(r);return true;}
    const String id=param_or_empty(r,"id");
    const String value=param_or_empty(r,"value");
    int code=400;
    if(single)code=mira_jeedom_async_set_binding(id.c_str(),value.c_str());
    else if(toggle){
        const String off=param_or_empty(r,"value0");
        const String on=param_or_empty(r,"value1");
        code=mira_jeedom_async_set_toggle_binding(id.c_str(),off.c_str(),on.c_str());
    }else if(init)code=mira_jeedom_async_set_init_id(value.c_str());
    r->send(code,"text/plain; charset=utf-8",
            code==200?"OK":"Association Jeedom impossible");
    return true;
}

bool mira_async_render_page(AsyncWebServerRequest *r);
bool mira_async_handle_beta(AsyncWebServerRequest *r);
bool mira_async_render_page(AsyncWebServerRequest *r);
static void proxy_worker(void *unused){
    (void)unused;
    ProxyJob *job=nullptr;
    for(;;){
        if(xQueueReceive(proxy_queue,&job,portMAX_DELAY)!=pdTRUE||!job)
            continue;
        std::unique_ptr<ProxyJob> owner(job);
        auto request=job->request.lock();
        if(!request)continue;
        // All work runs here in a PSRAM-backed task, not AsyncTCP callbacks.
        if(native_jeedom_request(request.get()))continue;
        if(mira_async_handle_beta(request.get()))continue;
        if(mira_async_render_page(request.get()))continue;
        request->send(404,"text/plain; charset=utf-8",
            "Route Mira absente sur ESPAsyncWebServer");
    }
}
static void proxy_request(AsyncWebServerRequest *r){
    if(!proxy_queue){r->send(503,"text/plain","Serveur Mira indisponible");return;}
    if(r->contentLength()>MIRA_PROXY_MAX_BODY){
        r->send(413,"text/plain","Formulaire trop volumineux");return;
    }
    ProxyJob *job=new(std::nothrow) ProxyJob;
    if(!job){r->send(503,"text/plain","Memoire HTTP epuisee");return;}
    job->request=r->pause();
    if(xQueueSend(proxy_queue,&job,0)!=pdTRUE){
        r->send(503,"text/plain","File HTTP occupee");
        delete job;
    }
}

static bool basic_auth(AsyncWebServerRequest *r){
    char password[97]={0};
    bool ok=mira_beta_web_auth_key_copy(password,sizeof(password));
    if(!ok)return false;
    bool match=r->authenticate("admin",password);
    memset(password,0,sizeof(password));
    return match;
}
extern "C" bool mira_async_web_ready(void){return started;}
extern "C" void mira_async_web_emit_direct(int id,const char *value){
    if(!started||id<1||id>4096||!value)return;
    char event_name[16];
    snprintf(event_name,sizeof(event_name),"%d",id);
    // Same CR/LF stripping and 160-byte state limit as native SSE. Never
    // allow a Jeedom value to inject its own EventSource framing lines.
    char safe[160];size_t n=0;
    while(value[n]&&n<sizeof(safe)-1){
        safe[n]=(value[n]=='\r'||value[n]=='\n')?' ':value[n];n++;
    }
    safe[n]=0;
    events.send(safe,event_name);
}
static void sse_worker(void *unused){
    (void)unused;
    uint32_t handled_epoch=0;
    size_t cursor=0;
    bool force=false;
    for(;;){
        vTaskDelay(pdMS_TO_TICKS(300));
        if(!started||events.count()==0||!previous)continue;
        uint32_t epoch=__atomic_load_n(&replay_epoch,__ATOMIC_RELAXED);
        if(epoch!=handled_epoch){handled_epoch=epoch;cursor=0;force=true;}
        size_t count=std::min(mira_jeedom_async_event_count(),(size_t)MIRA_SSE_LIMIT);
        if(!count)continue;
        unsigned sent=0,scanned=0;
        while(scanned<MIRA_SSE_BURST&&cursor<count){
            size_t i=cursor++;scanned++;
            int id=0;char value[160]={0};
            if(!mira_jeedom_async_event_current(i,&id,value,sizeof(value)))continue;
            if(force||previous[i].id!=id||strcmp(previous[i].value,value)){
                char name[16];snprintf(name,sizeof(name),"%d",id);
                events.send(value,name);
                previous[i].id=id;
                snprintf(previous[i].value,sizeof(previous[i].value),"%s",value);
                sent++;
            }
        }
        if(cursor>=count){cursor=0;force=false;}
        (void)sent;
    }
}
extern "C" void mira_async_web_start(void){
    if(started)return;
    initArduino(); // Use Arduino as a component, not as the network owner.
    // Reserve PSRAM for state replay; never touch RGB-DMA internal memory.
    previous=(SsePrevious *)heap_caps_calloc(
        MIRA_SSE_LIMIT,sizeof(*previous),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    proxy_queue=xQueueCreateWithCaps(6,sizeof(ProxyJob *),
                                     MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!previous||!proxy_queue){
        ESP_LOGE(TAG,"ASYNC_HTTP_ALLOC_FAILED");
        return;
    }
    if(xTaskCreateWithCaps(proxy_worker,"mira_async_proxy",12288,NULL,3,
                           nullptr,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)!=pdPASS){
        ESP_LOGE(TAG,"ASYNC_PROXY_TASK_FAILED");return;
    }
    if(xTaskCreateWithCaps(sse_worker,"mira_async_sse",8192,NULL,3,
                           nullptr,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)!=pdPASS)
        ESP_LOGW(TAG,"ASYNC_SSE_REPLAY_TASK_FAILED (direct events still active)");
    server.on("/camera/config",AsyncWebRequestMethod::HTTP_POST,proxy_request);
    server.on("/addlistlvgl",AsyncWebRequestMethod::HTTP_GET,proxy_request);
    server.on("/screenadd",AsyncWebRequestMethod::HTTP_GET,proxy_request);
    server.on("/removelistlvgl",AsyncWebRequestMethod::HTTP_GET,proxy_request);
    server.on("/screenformat",AsyncWebRequestMethod::HTTP_GET,proxy_request);
    server.on("/savelistlvgl",AsyncWebRequestMethod::HTTP_GET,proxy_request);
    server.on("/screensave",AsyncWebRequestMethod::HTTP_GET,proxy_request);
    server.on("/getallparams",AsyncWebRequestMethod::HTTP_GET,proxy_request);
    server.on("/navpages",AsyncWebRequestMethod::HTTP_GET,proxy_request);
    server.on("/navpages",AsyncWebRequestMethod::HTTP_POST,proxy_request);
    server.on("/navfavorites",AsyncWebRequestMethod::HTTP_GET,proxy_request);
    server.on("/navfavorites",AsyncWebRequestMethod::HTTP_POST,proxy_request);
    server.on("/sync/toggle",AsyncWebRequestMethod::HTTP_GET,proxy_request);
    server.on("/getlvgl",AsyncWebRequestMethod::HTTP_GET,proxy_request);
    server.on("/displayinfo",AsyncWebRequestMethod::HTTP_GET,proxy_request);
    server.on("/doUpdateFile",AsyncWebRequestMethod::HTTP_POST,
              ota_upload_complete,ota_upload);
    server.on("/update",AsyncWebRequestMethod::HTTP_GET,[](AsyncWebServerRequest *r){
        if(r->hasParam("key"))updatenr_get(r);
        else ota_page(r);
    });
    server.on("/updatemodule",AsyncWebRequestMethod::HTTP_GET,ota_page);
    server.on("/api/otastatus",AsyncWebRequestMethod::HTTP_GET,[](AsyncWebServerRequest *r){
        if(!basic_auth(r)){r->requestAuthentication();return;}
        char answer[120];
        snprintf(answer,sizeof(answer),
            "{\"last_status\":%u,\"last_bytes\":%u,\"active\":%s}",
            ota_last_status,(unsigned)ota_last_bytes,
            __atomic_load_n(&ota_busy,__ATOMIC_ACQUIRE)?"true":"false");
        r->send(200,"application/json",answer);
    });
    server.on("/mira-health",AsyncWebRequestMethod::HTTP_GET,proxy_request);
    server.on("/mira/jeedom/status",AsyncWebRequestMethod::HTTP_GET,proxy_request);
    server.on("/getindex",AsyncWebRequestMethod::HTTP_GET,index_get);
    server.on("/updatenr",AsyncWebRequestMethod::HTTP_GET,updatenr_get);
    server.on("/get",AsyncWebRequestMethod::HTTP_GET,proxy_request);
    server.on("/getparam",AsyncWebRequestMethod::HTTP_GET,proxy_request);
    server.on("/updateparam",AsyncWebRequestMethod::HTTP_GET,proxy_request);
    server.on("/addparam",AsyncWebRequestMethod::HTTP_GET,proxy_request);
    server.on("/addlistparam",AsyncWebRequestMethod::HTTP_GET,proxy_request);
    server.on("/updatelistparam",AsyncWebRequestMethod::HTTP_GET,proxy_request);
    server.on("/savelistparam",AsyncWebRequestMethod::HTTP_GET,proxy_request);
    server.on("/createlistparam",AsyncWebRequestMethod::HTTP_GET,proxy_request);
    server.on("/programui",AsyncWebRequestMethod::HTTP_POST,proxy_request);
    server.on("/actions/bindings",AsyncWebRequestMethod::HTTP_POST,proxy_request);
    server.on("/setjeedomid",AsyncWebRequestMethod::HTTP_GET,proxy_request);
    server.on("/setjeedomtoggleid",AsyncWebRequestMethod::HTTP_GET,proxy_request);
    server.on("/sendjeedomorderid",AsyncWebRequestMethod::HTTP_GET,proxy_request);
    server.on("/getmodule",AsyncWebRequestMethod::HTTP_GET,getmodule_get);
    server.on("/testmodule",AsyncWebRequestMethod::HTTP_GET,testmodule_get);
    server.on("/mira/async-health",AsyncWebRequestMethod::HTTP_GET,[](AsyncWebServerRequest *r){
        char msg[256];
        snprintf(msg,sizeof(msg),
           "{\"transport\":\"ESPAsyncWebServer\",\"port\":80,\"httpd_active\":false,"
           "\"internal_free\":%lu,\"internal_min_free\":%lu,"
           "\"internal_largest_block\":%lu,\"dma_min_free\":%lu,"
           "\"psram_free\":%lu,\"sse_clients\":%u}",
           (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),
           (unsigned long)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),
           (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),
           (unsigned long)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_DMA),
           (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
           (unsigned)events.count());
        r->send(200,"application/json",msg);
    });
    events.authorizeConnect([](AsyncWebServerRequest *r){return basic_auth(r);});
    events.onConnect([](AsyncEventSourceClient *c){
        c->send("ready","transport",millis(),3000);
        __atomic_add_fetch(&replay_epoch,1u,__ATOMIC_RELAXED);
    });
    server.addHandler(&events);
    // WT32 architecture: a single ESPAsyncWebServer handles every public route.
    server.onNotFound(proxy_request);
    server.begin();
    started=true;
    ESP_LOGI(TAG,"ASYNC_ONLY_PORT80_READY: Jeedom, SSE, setup, UI and OTA; no HTTPD");
}
