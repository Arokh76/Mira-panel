// Stage 3 — isolated Waveshare 7 UI + minimal ESP-IDF Wi-Fi diagnostic.
// No Jeedom, MQTT, OTA, camera, auto-join or writable settings endpoints.
// The RGB/LVGL/GT911 driver and real Mira UI are initialized BEFORE Wi-Fi.
// AP SSID intentionally differs from the legacy "Mira Panel" Windows profile.
#include "mira_net_diag.h"
#include "sdkconfig.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <inttypes.h>
#include "esp_err.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "mira_display.h"
#include "mira_full.h"

#define DIAG_SSID "Mira Panel TEST"
#define DIAG_IP_A 192
#define DIAG_IP_B 168
#define DIAG_IP_C 4
#define DIAG_IP_D 1

static const char *TAG="mira7-net-diag";
static httpd_handle_t server;
static unsigned connected_clients;
static unsigned assigned_ips;

static void net_event(void *ctx, esp_event_base_t base, int32_t id, void *data)
{
    (void)ctx;
    (void)data;
    if(base==WIFI_EVENT && id==WIFI_EVENT_AP_STACONNECTED) {
        connected_clients++;
        ESP_LOGI(TAG,"AP_STACONNECTED: total=%u",connected_clients);
    } else if(base==WIFI_EVENT && id==WIFI_EVENT_AP_STADISCONNECTED) {
        if(connected_clients) connected_clients--;
        ESP_LOGW(TAG,"AP_STADISCONNECTED: remaining=%u",connected_clients);
    } else if(base==IP_EVENT && id==IP_EVENT_AP_STAIPASSIGNED) {
        assigned_ips++;
        ESP_LOGI(TAG,"AP_STAIPASSIGNED: DHCP leases=%u",assigned_ips);
    }
}

static esp_err_t root_handler(httpd_req_t *req)
{
    const char *html =
        "<!doctype html><html lang='fr'><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>Mira 7 - Test Wi-Fi</title>"
        "<style>body{font:18px system-ui;margin:30px;max-width:700px;"
        "background:#101b2c;color:#eff7fa}h1{color:#48dcaf}"
        "a{color:#7bdcff}</style>"
        "<h1>MIRA 7 - Wi-Fi OK</h1>"
        "<p>ESP-IDF / Waveshare 7 pouces - etape 3.</p>"
        "<p>Ce portail prouve uniquement que ton PC ou telephone peut "
        "s'associer au point d'acces, obtenir une IP et atteindre HTTP.</p>"
        "<p>SSID : <b>Mira Panel TEST</b> - reseau temporaire ouvert.</p>"
        "<p><a href='/status'>Voir les statistiques RGB/LVGL</a></p>"
        "<p>Jeedom, MQTT, camera, OTA et ecriture de parametres sont "
        "volontairement desactives. L'interface Mira reste sur l'ecran.</p>"
        "</html>";
    httpd_resp_set_type(req,"text/html; charset=utf-8");
    httpd_resp_set_hdr(req,"Cache-Control","no-store");
    return httpd_resp_sendstr(req,html);
}

static esp_err_t status_handler(httpd_req_t *req)
{
    mira_display_stats_t d={0};
    mira_display_stats(&d);
    char buf[400];
    const int n=snprintf(buf,sizeof(buf),
        "{\"stage\":3,\"ssid\":\"Mira Panel TEST\","
        "\"rgb_refreshes\":%" PRIu32 ",\"gt911_ready\":%s,"
        "\"widgets\":%" PRIu32 ",\"associated\":%u,"
        "\"dhcp_leases_total\":%u,\"dma_internal_free\":%u,"
        "\"dma_internal_largest_block\":%u}\n",
        d.frames, d.touch_ready?"true":"false",
        mira_full_widget_count(),connected_clients,assigned_ips,
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_DMA),
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_DMA));
    if(n<0||(size_t)n>=sizeof(buf))
        return httpd_resp_send_500(req);
    httpd_resp_set_type(req,"application/json");
    httpd_resp_set_hdr(req,"Cache-Control","no-store");
    return httpd_resp_send(req,buf,n);
}

// All A/IN captive portal queries from the test AP subnet resolve locally.
// Only serve the 192.168.4.0/24 network; do not expose a public resolver.
// This is a temporary diagnostic AP: it does not provide Internet access.
static void dns_worker(void *arg)
{
    (void)arg;
    const int fd=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
    if(fd<0) {
        ESP_LOGE(TAG,"DNS socket failed");
        vTaskDelete(NULL);
        return;
    }
    struct sockaddr_in local={0};
    local.sin_family=AF_INET;
    local.sin_addr.s_addr=htonl(INADDR_ANY);
    local.sin_port=htons(53);
    struct timeval timeout={.tv_sec=1,.tv_usec=0};
    (void)setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));
    if(bind(fd,(struct sockaddr*)&local,sizeof(local))!=0) {
        ESP_LOGE(TAG,"DNS port 53 bind failed");
        close(fd);
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG,"Captive DNS listening on AP only");
    for(;;) {
        unsigned char packet[512]={0};
        struct sockaddr_in peer={0};
        socklen_t peer_len=sizeof(peer);
        const int len=recvfrom(fd,packet,sizeof(packet),0,
                               (struct sockaddr*)&peer,&peer_len);
        if(len<17)continue;
        if((ntohl(peer.sin_addr.s_addr)&0xffffff00u)!=0xc0a80400u)continue;
        if((packet[2]&0x80)!=0||packet[4]!=0||packet[5]!=1)continue;
        int pos=12;
        bool valid=false;
        while(pos<len) {
            const unsigned size=packet[pos++];
            if(size==0){valid=true;break;}
            if(size>63||pos+(int)size>=len){valid=false;break;}
            pos+=(int)size;
        }
        if(!valid||pos+4>len||
           packet[pos]!=0||packet[pos+1]!=1||
           packet[pos+2]!=0||packet[pos+3]!=1)continue;
        pos+=4;
        static const uint8_t answer[16]={
            0xc0,0x0c,0,1,0,1,0,0,0,10,0,4,
            DIAG_IP_A,DIAG_IP_B,DIAG_IP_C,DIAG_IP_D
        };
        if(pos+(int)sizeof(answer)>(int)sizeof(packet))continue;
        packet[2]=0x81;packet[3]=0x80;
        packet[4]=0;packet[5]=1;
        packet[6]=0;packet[7]=1;
        memset(packet+8,0,4);
        memcpy(packet+pos,answer,sizeof(answer));
        (void)sendto(fd,packet,(size_t)pos+sizeof(answer),0,
                     (struct sockaddr*)&peer,peer_len);
    }
}

static esp_err_t register_get(const char *path, esp_err_t (*handler)(httpd_req_t*))
{
    httpd_uri_t uri={
        .uri=path,
        .method=HTTP_GET,
        .handler=handler,
        .user_ctx=NULL,
    };
    return httpd_register_uri_handler(server,&uri);
}

esp_err_t mira_net_diag_start(void)
{
    ESP_LOGI(TAG,"Launching isolated AP AFTER RGB/LVGL/GT911: %s",DIAG_SSID);
    ESP_LOGI(TAG,"Internal DMA remaining before Wi-Fi: %u, largest=%u",
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_DMA),
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_DMA));
    esp_err_t err=esp_netif_init();
    if(err!=ESP_OK)return err;
    err=esp_event_loop_create_default();
    if(err!=ESP_OK && err!=ESP_ERR_INVALID_STATE)return err;
    esp_netif_t *ap=esp_netif_create_default_wifi_ap();
    if(!ap)return ESP_ERR_NO_MEM;
    ESP_LOGI(TAG,"WIFI_CONFIG: PSRAM allocation preferred, RX static=%d, RX dynamic=%d, TX static=%d, RX management=%d",
        CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM, CONFIG_ESP_WIFI_DYNAMIC_RX_BUFFER_NUM,
        CONFIG_ESP_WIFI_STATIC_TX_BUFFER_NUM, CONFIG_ESP_WIFI_RX_MGMT_BUF_NUM_DEF);
    ESP_LOGI(TAG,"BEFORE_WIFI_INIT: internal DMA free=%u largest=%u, PSRAM free=%u",
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_DMA),
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_DMA),
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    const wifi_init_config_t init=WIFI_INIT_CONFIG_DEFAULT();
    err=esp_wifi_init(&init);
    ESP_LOGI(TAG,"AFTER_WIFI_INIT: err=%s, internal DMA free=%u largest=%u, PSRAM free=%u",
        esp_err_to_name(err),
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_DMA),
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_DMA),
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    if(err!=ESP_OK)return err;
    // The Wi-Fi driver stays on its ESP-IDF default core 0; this stage does
    // not change the validated LVGL task scheduling or RGB timings.
    err=esp_event_handler_register(WIFI_EVENT,ESP_EVENT_ANY_ID,net_event,NULL);
    if(err!=ESP_OK)return err;
    err=esp_event_handler_register(IP_EVENT,IP_EVENT_AP_STAIPASSIGNED,net_event,NULL);
    if(err!=ESP_OK)return err;
    err=esp_wifi_set_mode(WIFI_MODE_AP);
    if(err!=ESP_OK)return err;
    wifi_config_t config={0};
    const size_t ssid_len=strlen(DIAG_SSID);
    memcpy(config.ap.ssid,DIAG_SSID,ssid_len);
    config.ap.ssid_len=ssid_len;
    config.ap.channel=6;
    config.ap.authmode=WIFI_AUTH_OPEN;
    config.ap.ssid_hidden=0;
    config.ap.max_connection=4;
    err=esp_wifi_set_config(WIFI_IF_AP,&config);
    if(err!=ESP_OK)return err;
    err=esp_wifi_start();
    if(err!=ESP_OK)return err;

    esp_netif_ip_info_t ip={0};
    if(esp_netif_get_ip_info(ap,&ip)==ESP_OK) {
        ESP_LOGI(TAG,"AP IP: " IPSTR " ; expected http://192.168.4.1",
                 IP2STR(&ip.ip));
    }

    httpd_config_t web=HTTPD_DEFAULT_CONFIG();
    web.server_port=80;
    web.max_uri_handlers=12;
    web.stack_size=6144;
    err=httpd_start(&server,&web);
    if(err!=ESP_OK)return err;
    err=register_get("/",root_handler);
    if(err!=ESP_OK)return err;
    err=register_get("/status",status_handler);
    if(err!=ESP_OK)return err;
    // Operating system captive-portal detection endpoints.
    static const char *const portals[]={
        "/generate_204","/hotspot-detect.html",
        "/connecttest.txt","/ncsi.txt","/fwlink",
        "/canonical.html","/success.txt"
    };
    for(size_t i=0;i<sizeof(portals)/sizeof(portals[0]);i++){
        err=register_get(portals[i],root_handler);
        if(err!=ESP_OK)return err;
    }
    if(xTaskCreatePinnedToCore(dns_worker,"mira7_dns_diag",4096,NULL,3,NULL,0)!=pdPASS)
        ESP_LOGW(TAG,"Captive DNS unavailable; direct URL still works");

    ESP_LOGI(TAG,"NET_DIAG_READY: open SSID '%s', manual http://192.168.4.1",
             DIAG_SSID);
    ESP_LOGI(TAG,"No flash erase, no settings changes, no Jeedom/MQTT or camera");
    return ESP_OK;
}
