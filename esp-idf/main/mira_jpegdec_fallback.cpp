// Media JPEG compatibility fallback using the proven WT32 JPEGDEC API.
// Only invoked if the native esp_jpeg decoder cannot handle an image.
// Decode in PSRAM on the existing FreeRTOS worker; do not call LVGL here.
#include "mira_camera.h"
#include "JPEGDEC.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include <new>
#include <cstring>
#include <cstdint>
#include <cstddef>
#include <climits>

static const char *TAG="mira-jpegdec";

static int copy_block(JPEGDRAW *draw){
    if(!draw||!draw->pUser||!draw->pPixels||draw->iBpp!=16)return 0;
    auto *out=static_cast<mira_camera_frame_t*>(draw->pUser);
    if(!out->pixels||!out->width||!out->height)return 0;
    for(int y=0;y<draw->iHeight;y++){
        const int dy=draw->y+y;
        if(dy<0||dy>=out->height)continue;
        const uint16_t *src=draw->pPixels+(size_t)y*draw->iWidth;
        uint16_t *dst=out->pixels+(size_t)dy*out->width;
        for(int x=0;x<draw->iWidthUsed;x++){
            const int dx=draw->x+x;
            if(dx>=0&&dx<out->width)dst[dx]=src[x];
        }
    }
    return 1;
}

extern "C" bool mira_jpegdec_fallback(uint8_t *bytes,size_t len,mira_camera_frame_t *out){
    if(!bytes||!out||len<60||len>900u*1024u||len>(size_t)INT_MAX)return false;
    memset(out,0,sizeof(*out));
    // JPEGDEC's state is substantial: place it in PSRAM, not internal/DMA RAM.
    void *storage=heap_caps_calloc(1,sizeof(JPEGDEC),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!storage){ESP_LOGW(TAG,"No PSRAM for JPEGDEC state");return false;}
    auto *decoder=new(storage) JPEGDEC();
    bool ok=decoder->openRAM(bytes,(int)len,copy_block)==1;
    int divisor=1,options=0;
    if(ok){
        const int width=decoder->getWidth(),height=decoder->getHeight();
        ok=width>0&&height>0;
        while(ok && ((width+divisor-1)/divisor>800 ||
                      (height+divisor-1)/divisor>428)){
            divisor*=2;
            if(divisor>8)ok=false;
        }
        if(ok){
            if(divisor==2)options=JPEG_SCALE_HALF;
            else if(divisor==4)options=JPEG_SCALE_QUARTER;
            else if(divisor==8)options=JPEG_SCALE_EIGHTH;
            out->width=(uint16_t)((width+divisor-1)/divisor);
            out->height=(uint16_t)((height+divisor-1)/divisor);
            out->pixels=(uint16_t*)heap_caps_calloc(
                (size_t)out->width*out->height,sizeof(uint16_t),
                MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
            ok=out->pixels!=nullptr;
        }
    }
    if(ok){
        decoder->setPixelType(RGB565_LITTLE_ENDIAN);
        decoder->setUserPointer(out);
        ok=decoder->decode(0,0,options)==1;
    }
    if(!ok)ESP_LOGW(TAG,"WT32 JPEGDEC fallback failed, code=%d",decoder->getLastError());
    decoder->close();
    decoder->~JPEGDEC();
    heap_caps_free(storage);
    if(!ok){
        if(out->pixels)heap_caps_free(out->pixels);
        memset(out,0,sizeof(*out));
    } else ESP_LOGI(TAG,"WT32 JPEGDEC fallback succeeded, %ux%u",
                    (unsigned)out->width,(unsigned)out->height);
    return ok;
}
