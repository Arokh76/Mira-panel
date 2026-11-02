// ESP-IDF native Mira camera - asynchronous RGB24 BMP, JPEG and MJPEG snapshots.
// Uses Espressif esp_jpeg for RGB565 software decode; supports multipart MJPEG first frames.
// Never call LVGL from the network task.
#include "mira_camera.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
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
    // HTTPS verifies the server certificate against the embedded CA store.
    // Never silently downgrade HTTPS to plain HTTP or skip verification.
    bool http=!strncmp(url,"http://",7);
    bool https=!strncmp(url,"https://",8);
    if(n>CAM_MAX_URL||(!http&&!https))return false;
    const char *host=url+(https?8:7);
    if(!*host||*host=='/'||*host=='?'||*host=='#')return false;
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

// Decode one bounded JPEG already assembled by either a snapshot or a
// persistent MJPEG TCP session. Never touch LVGL from this worker.
static bool decode_jpeg_buffer(uint8_t *jpeg,size_t used,mira_camera_frame_t *out){
    if(!jpeg||!out||used<60||used>CAM_MAX_JPEG||
       jpeg[0]!=0xff||jpeg[1]!=0xd8||jpeg[used-2]!=0xff||jpeg[used-1]!=0xd9)
        return false;
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
    if(!fit)return false;
    uint16_t *pixels=heap_caps_malloc(info.output_len,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!pixels){return false;}
    config.outbuf=(uint8_t*)pixels;
    config.outbuf_size=(uint32_t)info.output_len;
    esp_jpeg_image_output_t decoded={0};
    bool ok=esp_jpeg_decode(&config,&decoded)==ESP_OK&&
            decoded.width==info.width && decoded.height==info.height &&
            decoded.output_len>=(size_t)decoded.width*(size_t)decoded.height*2u &&
            decoded.output_len<=info.output_len;
    
    if(!ok){heap_caps_free(pixels);return false;}
    out->pixels=pixels;
    out->width=decoded.width;
    out->height=decoded.height;
    return true;
}

// Read a complete JPEG (Content-Length or end-of-image marker), then decode
// off the GUI task into a PSRAM RGB565 frame. Refuse oversized compressed
// payloads or images that cannot be scaled to the 800x428 viewport.
static bool decode_jpeg(esp_http_client_handle_t http,const uint8_t *prefix,
                        size_t initial,int64_t expected,mira_camera_frame_t *out){
    if(!prefix || !out || initial<2 || expected>(int64_t)CAM_MAX_JPEG || initial>CAM_MAX_JPEG)return false;
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
    bool ok=decode_jpeg_buffer(jpeg,used,out);
    heap_caps_free(jpeg);
    return ok;
}

// Fallback: read the first MJPEG JPEG from a camera that doesn't support
// an uninterrupted streaming session. The main path retains its socket.
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
        // Avoid spending beyond the header-scan budget on oversized chunks.
        if((size_t)n>8192u-scanned)n=(int)(8192u-scanned);
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
        .url=url,.timeout_ms=3500,.disable_auto_redirect=true,
        .crt_bundle_attach=esp_crt_bundle_attach
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
// A MJPEG stream stays on one TCP/TLS connection until the user closes the
// camera, the URL changes, or the camera stalls. JPEG bytes are kept in PSRAM
// and scanned for SOI/EOI across arbitrary socket chunk boundaries.
// Preserve all bytes after EOI: they may contain the next multipart frame.
static bool camera_stream_current(uint32_t expected){
    bool current;
    xSemaphoreTake(lock,portMAX_DELAY);
    current=enabled&&generation==expected;
    xSemaphoreGive(lock);
    return current;
}
static bool camera_stream_publish(mira_camera_frame_t *frame,uint32_t expected){
    if(!frame||!frame->pixels)return false;
    xSemaphoreTake(lock,portMAX_DELAY);
    bool current=enabled&&generation==expected;
    if(current){
        if(latest.pixels)heap_caps_free(latest.pixels);
        latest=*frame;
        frame->pixels=NULL;
        frame->width=frame->height=0;
    }
    xSemaphoreGive(lock);
    return current;
}
typedef struct {
    uint8_t *jpeg;
    size_t used;
    bool capturing;
    bool last_ff;
    size_t bytes_before_image;
    uint32_t received;
    uint32_t rejected;
    int64_t last_frame_us;
} mjpeg_decoder_t;
static void mjpeg_append(mjpeg_decoder_t *stream,const uint8_t *src,
                         size_t count,uint32_t expected){
    for(size_t i=0;i<count;i++){
        const uint8_t byte=src[i];
        if(!stream->capturing){
            if(stream->last_ff&&byte==0xd8){
                stream->capturing=true;
                stream->jpeg[0]=0xff;
                stream->jpeg[1]=0xd8;
                stream->used=2;
            }else if(++stream->bytes_before_image>8192u && !stream->received){
                // A bogus multipart or non-JPEG response must be rejected.
                stream->bytes_before_image=8193u;
            }
            stream->last_ff=byte==0xff;
            continue;
        }
        if(stream->used>=CAM_MAX_JPEG){
            stream->rejected++;
            stream->capturing=false;
            stream->used=0;
            stream->last_ff=false;
            continue;
        }
        stream->jpeg[stream->used++]=byte;
        if(stream->last_ff&&byte==0xd9){
            const int64_t now=esp_timer_get_time();
            // Decode no faster than 4fps: screen redraw and PSRAM traffic
            // stay bounded even if the camera serves 25-30 MJPEG frames/s.
            if(now-stream->last_frame_us>=250000LL){
                mira_camera_frame_t frame={0};
                if(decode_jpeg_buffer(stream->jpeg,stream->used,&frame)){
                    if(camera_stream_publish(&frame,expected)){
                        stream->received++;
                        stream->last_frame_us=now;
                    }
                    if(frame.pixels)heap_caps_free(frame.pixels);
                }else stream->rejected++;
            }
            stream->capturing=false;
            stream->used=0;
        }
        stream->last_ff=byte==0xff;
    }
}
// Result: -1=not MJPEG -> use normal BMP/JPEG snapshot path.
// >=0 indicates an actual multipart stream was opened; its frame count is
// returned, and the worker retries on loss with bounded reconnect backoff.
static int camera_stream_mjpeg(const char *url,uint32_t expected){
    esp_http_client_config_t cfg={
        .url=url,.timeout_ms=3500,.disable_auto_redirect=true,
        .crt_bundle_attach=esp_crt_bundle_attach
    };
    esp_http_client_handle_t http=esp_http_client_init(&cfg);
    if(!http)return -1;
    int frames=-1;
    do{
        if(esp_http_client_open(http,0)!=ESP_OK)break;
        (void)esp_http_client_fetch_headers(http);
        if(esp_http_client_get_status_code(http)!=200)break;
        uint8_t first[4]={0};
        if(!read_exact(http,first,sizeof(first)))break;
        if(!((first[0]=='-'&&first[1]=='-')||
             (first[0]=='\r'&&first[1]=='\n'&&first[2]=='-'&&first[3]=='-')))
            break;
        frames=0;
        mjpeg_decoder_t stream={0};
        stream.jpeg=heap_caps_malloc(CAM_MAX_JPEG,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        if(!stream.jpeg){
            ESP_LOGW(TAG,"Not enough PSRAM for persistent MJPEG session");
            break;
        }
        mjpeg_append(&stream,first,sizeof(first),expected);
        int64_t last_data=esp_timer_get_time();
        while(camera_stream_current(expected)){
            uint8_t bytes[2048];
            int n=esp_http_client_read(http,(char*)bytes,sizeof(bytes));
            if(n<0)break;
            if(n==0){
                if(esp_timer_get_time()-last_data>7000000LL||
                   esp_http_client_is_complete_data_received(http))
                    break;
                vTaskDelay(pdMS_TO_TICKS(20));
                continue;
            }
            last_data=esp_timer_get_time();
            mjpeg_append(&stream,bytes,(size_t)n,expected);
            if(stream.bytes_before_image>8192u&&!stream.received)break;
            frames=(int)stream.received;
            // Bound network loop's CPU consumption without opening a new HTTP
            // connection for each JPEG. Cancellation is checked every chunk.
            vTaskDelay(pdMS_TO_TICKS(1));
        }
        ESP_LOGI(TAG,"MJPEG session closed (%" PRIu32 " frames, %" PRIu32 " rejects)",
                 stream.received,stream.rejected);
        heap_caps_free(stream.jpeg);
    }while(0);
    esp_http_client_close(http);
    esp_http_client_cleanup(http);
    return frames;
}
static void camera_worker(void *unused) {
    (void)unused;
    char url[CAM_MAX_URL+1];
    uint32_t seen_generation=UINT32_MAX;
    uint32_t failures=0;
    bool stream_detected=false,stream_tested=false;
    for(;;) {
        bool should_run;
        uint32_t current;
        xSemaphoreTake(lock,portMAX_DELAY);
        should_run=enabled && active_url[0]!=0;
        current=generation;
        snprintf(url,sizeof(url),"%s",active_url);
        xSemaphoreGive(lock);
        if(!should_run){vTaskDelay(pdMS_TO_TICKS(150));continue;}
        if(current!=seen_generation){
            seen_generation=current;
            failures=0;
            stream_detected=false;
            stream_tested=false;
        }
        // Detect a multipart stream once per URL, then stay connected and
        // publish subsequent JPEGs on the same socket until stopped.
        if(!stream_tested||stream_detected){
            int frames=camera_stream_mjpeg(url,current);
            stream_tested=true;
            if(frames>=0){
                stream_detected=true;
                if(frames>0)failures=0;
                else failures++;
                vTaskDelay(pdMS_TO_TICKS(frames>0?200:1200));
                continue;
            }
            stream_detected=false;
        }
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
bool mira_camera_fetch_url(const char *url,mira_camera_frame_t *out){
    if(!out)return false;
    memset(out,0,sizeof(*out));
    if(!mira_camera_url_valid(url))return false;
    return fetch_snapshot(url,out);
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
