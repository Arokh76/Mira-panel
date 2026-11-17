// Mira Panel 7STD: WT32-compatible event-driven public HTTP transport.
// ESPAsyncWebServer on :80 owns /getindex /updatenr /events natively.
// Unported legacy endpoints are transparently proxied by bounded workers to
// the ESP-IDF compatibility server on 127.0.0.1:8081. No plugin redirect.
// First-run and low-memory rescue stay on native :80 (no async front end).
#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <esp_http_client.h>
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
#include "mira_async_web.h"
extern "C" {
#include "mira_jeedom.h"
#include "mira_beta_web.h"
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

struct ProxyBody {
    size_t used;
    size_t total;
    uint8_t data[1];
};
struct ProxyJob {
    AsyncWebServerRequestPtr request;
};
struct ProxyResult {
    uint8_t *bytes=nullptr;
    size_t size=0,capacity=0;
    bool failed=false;
    char type[96]="text/plain; charset=utf-8";
    char location[256]={0};
    char authenticate[192]={0};
    char cache_control[96]={0};
    char retry_after[32]={0};
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

// The following helpers keep all other HTTP routes (including browser /,
// /get, /programui, /update, setup, media and controls) reachable from :80.
// Network IO is NEVER performed in AsyncTCP's callback thread.
static void append_encoded(std::string& out,const String& str){
    static const char *hex="0123456789ABCDEF";
    for(size_t i=0;i<str.length();i++){
        unsigned char v=(unsigned char)str[i];
        if((v>='A'&&v<='Z')||(v>='a'&&v<='z')||
           (v>='0'&&v<='9')||v=='-'||v=='_'||v=='.'||v=='~')
            out.push_back((char)v);
        else {out.push_back('%');out.push_back(hex[v>>4]);out.push_back(hex[v&15]);}
    }
}
static bool proxy_url(AsyncWebServerRequest *r,std::string& url){
    url="http://127.0.0.1:8081";
    url+=r->url().c_str();
    bool first=true;
    for(size_t i=0;i<r->params();i++){
        const auto *p=r->getParam(i);
        if(!p||(p->isPost()||p->isFile()))continue;
        url.push_back(first?'?':'&');first=false;
        append_encoded(url,p->name());url.push_back('=');
        append_encoded(url,p->value());
        if(url.length()>3500)return false;
    }
    return true;
}
static bool proxy_body(AsyncWebServerRequest *r,std::string& body){
    if(r->contentLength()>MIRA_PROXY_MAX_BODY)return false;
    auto *captured=(ProxyBody *)r->_tempObject;
    if(captured&&captured->total==r->contentLength()&&
       captured->used==captured->total){
        body.assign((const char *)captured->data,captured->used);
        return true;
    }
    // ESPAsyncWebServer parses application/x-www-form-urlencoded into
    // request params itself and does not fire onBody for those POSTs.
    bool first=true;
    for(size_t i=0;i<r->params();i++){
        const auto *p=r->getParam(i);
        if(!p||!p->isPost())continue;
        if(!first)body.push_back('&');
        first=false;
        append_encoded(body,p->name());body.push_back('=');
        append_encoded(body,p->value());
        if(body.length()>MIRA_PROXY_MAX_BODY)return false;
    }
    return !r->contentLength()||!body.empty();
}
static esp_err_t proxy_event(esp_http_client_event_t *e){
    ProxyResult *p=(ProxyResult *)e->user_data;
    if(!p)return ESP_FAIL;
    if(e->event_id==HTTP_EVENT_ON_HEADER&&e->header_key&&e->header_value){
        if(!strcasecmp(e->header_key,"Content-Type"))
            snprintf(p->type,sizeof(p->type),"%s",e->header_value);
        if(!strcasecmp(e->header_key,"Location"))
            snprintf(p->location,sizeof(p->location),"%s",e->header_value);
        if(!strcasecmp(e->header_key,"WWW-Authenticate"))
            snprintf(p->authenticate,sizeof(p->authenticate),"%s",e->header_value);
        if(!strcasecmp(e->header_key,"Cache-Control"))
            snprintf(p->cache_control,sizeof(p->cache_control),"%s",e->header_value);
        if(!strcasecmp(e->header_key,"Retry-After"))
            snprintf(p->retry_after,sizeof(p->retry_after),"%s",e->header_value);
    }else if(e->event_id==HTTP_EVENT_ON_DATA&&e->data_len>0){
        size_t n=(size_t)e->data_len;
        if(p->size+n>MIRA_PROXY_MAX_RESPONSE){p->failed=true;return ESP_FAIL;}
        if(p->size+n>p->capacity){
            size_t cap=std::max(p->size+n,p->capacity?p->capacity*2u:4096u);
            cap=std::min(cap,(size_t)MIRA_PROXY_MAX_RESPONSE);
            uint8_t *expanded=(uint8_t *)heap_caps_realloc(p->bytes,cap,
                                      MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
            if(!expanded){p->failed=true;return ESP_FAIL;}
            p->bytes=expanded;p->capacity=cap;
        }
        memcpy(p->bytes+p->size,e->data,n);p->size+=n;
    }
    return ESP_OK;
}

// Authenticated Jeedom programming: no HTTP loopback; processed in PSRAM worker.
static bool basic_auth(AsyncWebServerRequest *r);
static bool native_jeedom_request(AsyncWebServerRequest *r){
    const String path=r->url();
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
    if(!program&&!batch&&!single&&!toggle&&!init&&!readstate&&!readparam&&!writeparam&&!saveparam)
        return false;
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

static void proxy_worker(void *unused){
    (void)unused;
    ProxyJob *job=nullptr;
    for(;;){
        if(xQueueReceive(proxy_queue,&job,portMAX_DELAY)!=pdTRUE||!job)continue;
        std::unique_ptr<ProxyJob> owner(job);
        auto request=job->request.lock();
        if(!request)continue; // client disconnected while queued
        if(native_jeedom_request(request.get()))continue;
        std::string url,body;
        if(!proxy_url(request.get(),url)){
            request->send(414,"text/plain","URI trop longue");continue;
        }
        bool is_body=request->method()==AsyncWebRequestMethod::HTTP_POST||
                     request->method()==AsyncWebRequestMethod::HTTP_PUT||
                     request->method()==AsyncWebRequestMethod::HTTP_PATCH;
        if(is_body&&!proxy_body(request.get(),body)){
            request->send(413,"text/plain","Contenu trop volumineux");continue;
        }
        ProxyResult result;
        esp_http_client_config_t conf={};
        conf.url=url.c_str();
        conf.timeout_ms=3500;
        conf.event_handler=proxy_event;
        conf.user_data=&result;
        conf.disable_auto_redirect=true;
        esp_http_client_handle_t h=esp_http_client_init(&conf);
        if(!h){
            request->send(503,"text/plain","Proxy HTTP indisponible");continue;
        }
        esp_http_client_method_t method=HTTP_METHOD_GET;
        if(request->method()==AsyncWebRequestMethod::HTTP_POST)method=HTTP_METHOD_POST;
        else if(request->method()==AsyncWebRequestMethod::HTTP_PUT)method=HTTP_METHOD_PUT;
        else if(request->method()==AsyncWebRequestMethod::HTTP_DELETE)method=HTTP_METHOD_DELETE;
        else if(request->method()==AsyncWebRequestMethod::HTTP_PATCH)method=HTTP_METHOD_PATCH;
        esp_http_client_set_method(h,method);
        for(const char *key:{"Authorization","Content-Type","X-Mira-GPIO","Cookie"}){
            if(request->hasHeader(key)){
                String value=request->header(key);
                esp_http_client_set_header(h,key,value.c_str());
            }
        }
        if(is_body){
            if(!request->hasHeader("Content-Type"))
                esp_http_client_set_header(h,"Content-Type",
                                           "application/x-www-form-urlencoded");
            esp_http_client_set_post_field(h,body.data(),(int)body.size());
        }
        esp_err_t err=esp_http_client_perform(h);
        int status=esp_http_client_get_status_code(h);
        esp_http_client_cleanup(h);
        // A remote browser may close before the worker completes.
        if(!request->client()||!request->client()->connected()){
            heap_caps_free(result.bytes);continue;
        }
        if(err!=ESP_OK||result.failed||status<100){
            heap_caps_free(result.bytes);
            request->send(504,"text/plain","Mira legacy route indisponible");
            continue;
        }
        if(result.size==0){
            heap_caps_free(result.bytes);
            AsyncWebServerResponse *resp=request->beginResponse(
                status,result.type,"");
            if(result.location[0])resp->addHeader("Location",result.location);
            if(result.authenticate[0])resp->addHeader("WWW-Authenticate",result.authenticate);
            if(result.cache_control[0])resp->addHeader("Cache-Control",result.cache_control);
            if(result.retry_after[0])resp->addHeader("Retry-After",result.retry_after);
            request->send(resp);continue;
        }
        // Keep bytes alive until the async response has completely drained.
        auto response_owner=std::shared_ptr<uint8_t>(
            result.bytes,[](uint8_t *p){heap_caps_free(p);});
        size_t length=result.size;
        AsyncWebServerResponse *resp=request->beginResponse(
            result.type,length,
            [response_owner,length](uint8_t *out,size_t max,size_t offset)->size_t{
                if(offset>=length)return 0;
                size_t n=std::min(max,length-offset);
                memcpy(out,response_owner.get()+offset,n);
                return n;
            });
        resp->setCode(status);
        if(result.location[0])resp->addHeader("Location",result.location);
        if(result.authenticate[0])resp->addHeader("WWW-Authenticate",result.authenticate);
        if(result.cache_control[0])resp->addHeader("Cache-Control",result.cache_control);
        if(result.retry_after[0])resp->addHeader("Retry-After",result.retry_after);
        request->send(resp);
    }
}
static void proxy_request(AsyncWebServerRequest *r){
    if(!proxy_queue){r->send(503,"text/plain","Proxy non initialise");return;}
    if(r->contentLength()>MIRA_PROXY_MAX_BODY){
        // Huge firmware uploads cannot be buffered behind a reverse proxy.
        // Preserve native OTA through explicit legacy URL on :8081.
        String host=r->host();
        int port=host.indexOf(':');
        if(port>=0)host=host.substring(0,port);
        std::string legacy;
        if(!proxy_url(r,legacy)){r->send(414,"text/plain","URI trop longue");return;}
        const std::string prefix="http://127.0.0.1:8081";
        String target="http://"+host+":8081"+String(legacy.c_str()+prefix.size());
        AsyncWebServerResponse *resp=r->beginResponse(307,"text/plain",
                "OTA sur serveur de compatibilite :8081");
        resp->addHeader("Location",target);
        r->send(resp);return;
    }
    ProxyJob *job=new(std::nothrow) ProxyJob;
    if(!job){r->send(503,"text/plain","Memoire proxy epuisee");return;}
    job->request=r->pause();
    if(xQueueSend(proxy_queue,&job,0)!=pdTRUE){
        r->send(503,"text/plain","File HTTP occupee");
        delete job;
    }
}
static void body_receive(AsyncWebServerRequest *r,uint8_t *data,
                         size_t len,size_t index,size_t total){
    if(!total||total>MIRA_PROXY_MAX_BODY||index>total||len>total-index)return;
    ProxyBody *buf=(ProxyBody *)r->_tempObject;
    if(index==0&&!buf){
        buf=(ProxyBody *)heap_caps_malloc(sizeof(ProxyBody)+total,
                                           MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        if(!buf)return;
        buf->used=0;buf->total=total;r->_tempObject=buf;
    }
    if(!buf||buf->total!=total||buf->used!=index)return;
    memcpy(buf->data+index,data,len);buf->used+=len;
}
static bool basic_auth(AsyncWebServerRequest *r){
    char password[97]={0};
    bool ok=mira_beta_web_auth_key_copy(password,sizeof(password));
    if(!ok)return false;
    bool match=r->authenticate("admin",password);
    memset(password,0,sizeof(password));
    return match;
}
extern "C" void mira_async_web_emit_direct(int id,const char *value){
    if(!started||id<1||id>4096||!value)return;
    char event_name[16];
    snprintf(event_name,sizeof(event_name),"%d",id);
    // Same CR/LF stripping and 160-byte state limit as native SSE. Never
    // allow a Jeedom value to inject its own EventSource framing lines.
    char safe[160];size_t n=0;
    while(value[n]&&n<sizeof(safe)-1){
        safe[n]=(value[n]=='\\r'||value[n]=='\\n')?' ':value[n];n++;
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
        char msg[160];
        snprintf(msg,sizeof(msg),
           "{\"transport\":\"ESPAsyncWebServer\",\"port\":80,\"legacy\":8081,"
           "\"internal_free\":%lu,\"psram_free\":%lu,\"sse_clients\":%u}",
           (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),
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
    // Native 8081 continues to host configuration, file OTA and legacy APIs,
    // but the Jeedom client and browser stay on :80 (no redirect).
    server.onNotFound(proxy_request);
    server.onRequestBody(body_receive);
    server.begin();
    started=true;
    ESP_LOGI(TAG,"ASYNC_HTTP_PORT80_READY: direct /getindex /updatenr /events, proxy :8081");
}
