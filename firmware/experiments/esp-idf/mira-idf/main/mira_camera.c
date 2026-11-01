// ESP-IDF native Mira camera - asynchronous RGB24 BMP and JPEG snapshots.
// Uses Espressif esp_jpeg for RGB565 software decode; supports multipart MJPEG first frames.
// Never call LVGL from the network task.
#include "mira_camera.h"
#include "esp_http_client.h"
#include "jpeg_decoder.h"
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
#define CAM_MAX_JPEG (900u * 1024u)

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

// Read a complete JPEG (Content-Length or end-of-image marker), then decode
// off the GUI task into a PSRAM RGB565 frame. Refuse oversized compressed
// payloads or images that cannot be scaled to the 800x428 viewport.
static bool decode_jpeg(esp_http_client_handle_t http,const uint8_t *prefix,
                        size_t initial,int64_t expected,mira_camera_frame_t *out){
    if(expected>(int64_t)CAM_MAX_JPEG || initial>CAM_MAX_JPEG)return false;
    const size_t capacity=(expected>0)?(size_t)expected:CAM_MAX_JPEG;
    if(capacity<initial)return false;
    uint8_t *jpeg=heap_caps_malloc(capacity,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!jpeg)return false;
    memcpy(jpeg,prefix,initial);
    size_t used=initial;
    bool complete=false;
    // The first network read can already contain EOI plus multipart headers.
    for(size_t i=1;i<used;i++){
        if(jpeg[i-1]==0xff && jpeg[i]==0xd9){
            used=i+1;
            complete=true;
            break;
        }
    }
    const int64_t deadline=esp_timer_get_time()+7000000LL;
    while(!complete&&used<capacity&&esp_timer_get_time()<deadline){
        if(used>=2 && jpeg[used-2]==0xff && jpeg[used-1]==0xd9){
            complete=true;break;
        }
        size_t pending=capacity-used;
        if(pending>2048)pending=2048;
        int n=esp_http_client_read(http,(char*)jpeg+used,(int)pending);
        if(n<0)break;
        if(n==0){
            if(expected>0&&used>=(size_t)expected)break;
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        const size_t previous=used;
        used+=(size_t)n;
        // Multipart boundaries can arrive in the same read as JPEG EOI.
        for(size_t i=previous?previous-1:0;i+1<used;i++){
            if(jpeg[i]==0xff&&jpeg[i+1]==0xd9){
                used=i+2;
                complete=true;
                break;
            }
        }
        if(complete)break;
        if(expected>0&&used>=(size_t)expected)break;
    }
    if(used>=2&&jpeg[used-2]==0xff&&jpeg[used-1]==0xd9)complete=true;
    if(!complete||used<60||jpeg[0]!=0xff||jpeg[1]!=0xd8){
        heap_caps_free(jpeg);return false;
    }
    esp_jpeg_image_cfg_t config={
        .indata=jpeg,
        .indata_size=(uint32_t)used,
        .out_format=JPEG_IMAGE_FORMAT_RGB565,
        .out_scale=JPEG_IMAGE_SCALE_0,
    };
    esp_jpeg_image_output_t info={0};
    bool fit=false;
    for(int scale=0;scale<=3;scale++){
        config.out_scale=(esp_jpeg_image_scale_t)scale;
        memset(&info,0,sizeof(info));
        if(esp_jpeg_get_image_info(&config,&info)!=ESP_OK)break;
        if(info.width && info.height &&
           info.width<=CAM_WIDTH && info.height<=CAM_HEIGHT &&
           info.output_len>=(size_t)info.width*(size_t)info.height*2u &&
           info.output_len<=(size_t)CAM_WIDTH*CAM_HEIGHT*2u) {
            fit=true;break;
        }
    }
    if(!fit){heap_caps_free(jpeg);return false;}
    uint16_t *pixels=heap_caps_malloc(info.output_len,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!pixels){heap_caps_free(jpeg);return false;}
    config.outbuf=(uint8_t*)pixels;
    config.outbuf_size=(uint32_t)info.output_len;
    esp_jpeg_image_output_t decoded={0};
    bool ok=esp_jpeg_decode(&config,&decoded)==ESP_OK&&
            decoded.width==info.width && decoded.height==info.height &&
            decoded.output_len>=(size_t)decoded.width*(size_t)decoded.height*2u &&
            decoded.output_len<=info.output_len;
    heap_caps_free(jpeg);
    if(!ok){heap_caps_free(pixels);return false;}
    out->pixels=pixels;
    out->width=decoded.width;
    out->height=decoded.height;
    return true;
}

// Read one complete JPEG from a multipart MJPEG stream, bounded to 8 KB of
// headers and 900 KB of JPEG data. Reconnect per frame; no LVGL on this task.
static bool decode_mjpeg_first_frame(esp_http_client_handle_t http,
                                      const uint8_t *prefix,size_t count,
                                      mira_camera_frame_t *out){
    bool last_ff=false;
    for(size_t i=0;i<count;i++){
        if(last_ff && prefix[i]==0xd8)
            return decode_jpeg(http,prefix+i-1,count-i+1,-1,out);
        last_ff=prefix[i]==0xff;
    }
    size_t scanned=count;
    const int64_t deadline=esp_timer_get_time()+5000000LL;
    uint8_t chunk[512];
    while(scanned<8192 && esp_timer_get_time()<deadline){
        int n=esp_http_client_read(http,(char*)chunk,sizeof(chunk));
        if(n<0)return false;
        if(!n){vTaskDelay(pdMS_TO_TICKS(10));continue;}
        for(int i=0;i<n;i++){
            if(last_ff && chunk[i]==0xd8){
                size_t remaining=(size_t)n-(size_t)i;
                uint8_t *head=malloc(remaining+1);
                if(!head)return false;
                head[0]=0xff;
                memcpy(head+1,chunk+i,remaining);
                bool ok=decode_jpeg(http,head,remaining+1,-1,out);
                free(head);
                return ok;
            }
            last_ff=chunk[i]==0xff;
        }
        scanned+=(size_t)n;
    }
    return false;
}
static bool fetch_snapshot(const char *url,mira_camera_frame_t *out){
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
        if(header[0]==0xff&&header[1]==0xd8){
            // Some camera servers report a stale Content-Length or use a
            // continuous JPEG body. Always bound by JPEG EOI and CAM_MAX_JPEG,
            // not by an unreliable HTTP entity length.
            ok=decode_jpeg(http,header,sizeof(header),-1,out);
            break;
        }
        if(header[0]!='B'||header[1]!='M'){
            // Only multipart bodies starting with a MIME boundary may be
            // scanned for JPEG SOI. Do not parse arbitrary HTML/JSON error
            // pages as camera streams.
            if(header[0]=='-' && header[1]=='-')
                ok=decode_mjpeg_first_frame(http,header,sizeof(header),out);
            break;
        }
        if(le32(header+14)<40||
           le16(header+26)!=1||le16(header+28)!=24||le32(header+30)!=0)break;
        const uint32_t offset=le32(header+10);
        const int32_t w=(int32_t)le32(header+18);
        const int32_t signed_h=(int32_t)le32(header+22);
        const uint32_t h=(uint32_t)(signed_h<0?-(int64_t)signed_h:(int64_t)signed_h);
        // Common 640x480 and 800x480 BMP snapshots exceed the 428px
        // camera viewport. Downsample by two rather than rejecting them.
        // Refuse large sources before allocating any network image buffers.
        if(offset<54||offset>1024||w<1||w>CAM_WIDTH*2||
           h<1||h>CAM_HEIGHT*2)break;
        const uint32_t step=(w>CAM_WIDTH||h>CAM_HEIGHT)?2u:1u;
        const uint32_t out_w=(uint32_t)w/step;
        const uint32_t out_h=h/step;
        if(!out_w||!out_h)break;
        const size_t stride=(((size_t)w*3u)+3u)&~(size_t)3u;
        pixels=heap_caps_malloc((size_t)out_w*out_h*2u,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
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
            const uint32_t source_y=signed_h>0 ? h-y-1 : y;
            if(source_y%step || source_y/step>=out_h)continue;
            uint16_t *dst=pixels+(size_t)(source_y/step)*(size_t)out_w;
            for(uint32_t x=0;x<out_w;x++){
                const uint32_t i=3u*x*step;
                uint8_t blue=row[i],green=row[i+1],red=row[i+2];
                dst[x]=(uint16_t)(((red&0xf8u)<<8)|((green&0xfcu)<<3)|(blue>>3));
            }
        }
        if(y!=h)break;
        out->pixels=pixels;
        out->width=(uint16_t)out_w;
        out->height=(uint16_t)out_h;
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
        bool ok=fetch_snapshot(url,&frame);
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
                ESP_LOGW(TAG,"BMP/JPEG snapshot not received or decoded (%" PRIu32 " consecutive failures)",failures);
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
