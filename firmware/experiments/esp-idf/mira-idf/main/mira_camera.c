// ESP-IDF native Mira camera - asynchronous RGB24 BMP snapshot decoder.
// Works with the same HTTP BMP snapshot/proxy URLs used by Mira 0.7.37.
// JPEG/MJPEG is NOT decoded by this initial native backend.
// Never call LVGL from the network task.
#include "mira_camera.h"
#include "esp_http_client.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define CAM_MAX_URL 768
#define CAM_WIDTH 800
#define CAM_HEIGHT 428

static const char *TAG="mira-idf-camera";
static SemaphoreHandle_t lock;
static char active_url[CAM_MAX_URL+1];
static uint32_t generation;
static bool enabled;
static mira_camera_frame_t latest;

static uint16_t le16(const uint8_t *b){return (uint16_t)b[0]|((uint16_t)b[1]<<8);}
static uint32_t le32(const uint8_t *b){
    return (uint32_t)b[0]|((uint32_t)b[1]<<8)|((uint32_t)b[2]<<16)|((uint32_t)b[3]<<24);
}
bool mira_camera_url_valid(const char *url){
    if(!url)return false;
    size_t n=strlen(url);
    if(!n)return true; // An empty URL deliberately disables the stream.
    if(n>CAM_MAX_URL||strncmp(url,"http://",7)!=0)return false;
    for(const char *p=url;*p;p++)
        if(*p=='\n'||*p=='\r'||*p==' '||*p=='@')return false;
    return true;
}
static bool camera_key(const char *name,char key[16]) {
    if(!name||!*name||strlen(name)>100)return false;
    uint32_t hash=2166136261u;
    for(const unsigned char *p=(const unsigned char*)name;*p;p++){
        hash^=*p;hash*=16777619u;
    }
    snprintf(key,16,"c%08" PRIx32,hash);
    return true;
}
bool mira_camera_save_url(const char *name,const char *url) {
    char key[16];
    if(!camera_key(name,key)||!mira_camera_url_valid(url))return false;
    nvs_handle_t nvs;
    if(nvs_open("miracam",NVS_READWRITE,&nvs)!=ESP_OK)return false;
    esp_err_t err=nvs_set_str(nvs,key,url);
    if(err==ESP_OK)err=nvs_commit(nvs);
    nvs_close(nvs);
    return err==ESP_OK;
}
static bool load_url(const char *name,char *out,size_t size){
    char key[16];
    if(!camera_key(name,key))return false;
    nvs_handle_t nvs;
    if(nvs_open("miracam",NVS_READONLY,&nvs)!=ESP_OK)return false;
    size_t n=size;
    esp_err_t err=nvs_get_str(nvs,key,out,&n);
    nvs_close(nvs);
    return err==ESP_OK && out[0]!=0 && mira_camera_url_valid(out);
}
static bool read_exact(esp_http_client_handle_t client,uint8_t *buf,size_t size){
    size_t n=0;
    const int64_t deadline=esp_timer_get_time()+5000000LL;
    while(n<size && esp_timer_get_time()<deadline){
        int r=esp_http_client_read(client,(char*)buf+n,(int)(size-n));
        if(r<0)return false;
        if(r==0){vTaskDelay(pdMS_TO_TICKS(10));continue;}
        n+=(size_t)r;
    }
    return n==size;
}
static bool fetch_bmp(const char *url,mira_camera_frame_t *out){
    memset(out,0,sizeof(*out));
    esp_http_client_config_t cfg={
        .url=url,.timeout_ms=3500,.disable_auto_redirect=true
    };
    esp_http_client_handle_t http=esp_http_client_init(&cfg);
    if(!http)return false;
    bool ok=false;
    uint8_t *row=NULL;
    uint16_t *pixels=NULL;
    do {
        if(esp_http_client_open(http,0)!=ESP_OK)break;
        (void)esp_http_client_fetch_headers(http);
        if(esp_http_client_get_status_code(http)!=200)break;
        uint8_t header[54];
        if(!read_exact(http,header,sizeof(header)))break;
        if(header[0]!='B'||header[1]!='M'||le32(header+14)<40||
           le16(header+26)!=1||le16(header+28)!=24||le32(header+30)!=0)break;
        const uint32_t offset=le32(header+10);
        const int32_t w=(int32_t)le32(header+18);
        const int32_t signed_h=(int32_t)le32(header+22);
        const uint32_t h=(uint32_t)(signed_h<0?-(int64_t)signed_h:(int64_t)signed_h);
        if(offset<54||offset>1024||w<1||w>CAM_WIDTH||
           h<1||h>CAM_HEIGHT)break;
        const size_t stride=(((size_t)w*3u)+3u)&~(size_t)3u;
        pixels=heap_caps_malloc((size_t)w*h*2u,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        row=malloc(stride);
        if(!pixels||!row)break;
        uint8_t discard[64];
        uint32_t skip=offset-54;
        while(skip){
            const size_t bytes=skip>sizeof(discard)?sizeof(discard):skip;
            if(!read_exact(http,discard,bytes))break;
            skip-=(uint32_t)bytes;
        }
        if(skip)break;
        uint32_t y=0;
        for(;y<h;y++){
            if(!read_exact(http,row,stride))break;
            const uint32_t target=signed_h>0 ? h-y-1 : y;
            uint16_t *dst=pixels+(size_t)target*(size_t)w;
            for(int32_t x=0;x<w;x++){
                uint8_t blue=row[3*x],green=row[3*x+1],red=row[3*x+2];
                dst[x]=(uint16_t)(((red&0xf8u)<<8)|((green&0xfcu)<<3)|(blue>>3));
            }
        }
        if(y!=h)break;
        out->pixels=pixels;
        out->width=(uint16_t)w;
        out->height=(uint16_t)h;
        pixels=NULL;
        ok=true;
    }while(0);
    free(row);
    if(pixels)heap_caps_free(pixels);
    esp_http_client_close(http);
    esp_http_client_cleanup(http);
    return ok;
}
static void camera_worker(void *unused) {
    (void)unused;
    char url[CAM_MAX_URL+1];
    uint32_t seen_generation=UINT32_MAX;
    uint32_t failures=0;
    for(;;) {
        bool should_run;
        uint32_t current;
        xSemaphoreTake(lock,portMAX_DELAY);
        should_run=enabled && active_url[0]!=0;
        current=generation;
        snprintf(url,sizeof(url),"%s",active_url);
        xSemaphoreGive(lock);
        if(!should_run){vTaskDelay(pdMS_TO_TICKS(150));continue;}
        if(current!=seen_generation){seen_generation=current;failures=0;}
        mira_camera_frame_t frame={0};
        bool ok=fetch_bmp(url,&frame);
        xSemaphoreTake(lock,portMAX_DELAY);
        bool still_current=enabled && generation==current;
        if(still_current && ok){
            if(latest.pixels)heap_caps_free(latest.pixels);
            latest=frame;
            frame.pixels=NULL;
            failures=0;
        }else if(still_current){
            failures++;
            if(failures==1||failures%25==0)
                ESP_LOGW(TAG,"BMP snapshot not received/decoded (%" PRIu32 " consecutive failures)",failures);
        }
        xSemaphoreGive(lock);
        if(frame.pixels)heap_caps_free(frame.pixels);
        vTaskDelay(pdMS_TO_TICKS((ok&&still_current)?500:1200));
    }
}
esp_err_t mira_camera_init(void){
    if(lock)return ESP_OK;
    lock=xSemaphoreCreateMutex();
    if(!lock)return ESP_ERR_NO_MEM;
    if(xTaskCreate(camera_worker,"mira_camera",6656,NULL,3,NULL)!=pdPASS){
        vSemaphoreDelete(lock);
        lock=NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
bool mira_camera_start(const char *widget_name) {
    if(!lock)return false;
    char url[CAM_MAX_URL+1]={0};
    const bool available=load_url(widget_name,url,sizeof(url));
    xSemaphoreTake(lock,portMAX_DELAY);
    generation++;
    enabled=available;
    snprintf(active_url,sizeof(active_url),"%s",available?url:"");
    if(latest.pixels)heap_caps_free(latest.pixels);
    memset(&latest,0,sizeof(latest));
    xSemaphoreGive(lock);
    return available;
}
void mira_camera_stop(void){
    if(!lock)return;
    xSemaphoreTake(lock,portMAX_DELAY);
    enabled=false;
    generation++;
    active_url[0]=0;
    if(latest.pixels)heap_caps_free(latest.pixels);
    memset(&latest,0,sizeof(latest));
    xSemaphoreGive(lock);
}
bool mira_camera_take(mira_camera_frame_t *out){
    if(!lock||!out)return false;
    xSemaphoreTake(lock,portMAX_DELAY);
    bool have=latest.pixels!=NULL && enabled;
    if(have){*out=latest;memset(&latest,0,sizeof(latest));}
    xSemaphoreGive(lock);
    return have;
}
