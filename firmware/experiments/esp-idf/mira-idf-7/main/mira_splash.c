// WT32 Mira Panel startup design ported to LVGL 9 for Waveshare 7" 800x480.
// Original author's 150x150 JPEG is embedded in FLASH, decoded once to PSRAM.
// The splash is a TOP LAYER overlay: the real UI can be built underneath it.
// No progress-bar widget, only the changing "Chargement xx %" label.
#include "mira_splash.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "jpeg_decoder.h"
#include "mbedtls/base64.h"
#include <string.h>

static const char *TAG="mira7-splash";
static lv_obj_t *overlay;
static lv_obj_t *progress_label;
static uint16_t *logo_pixels;
static lv_image_dsc_t logo_descriptor;

// Original JPEG source: firmware/source/consolidated/MIRA_PANEL/MiraBootLogo.h
// 150x150 on WT32, on the same #071421 background. No ARDUINO dependencies.
static const char logo_b64[] =
    "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";

static void load_logo(void)
{
    const size_t encoded_len=sizeof(logo_b64)-1u;
    const size_t compressed_max=((encoded_len+3u)/4u)*3u;
    uint8_t *compressed=heap_caps_malloc(compressed_max,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!compressed)return;
    size_t jpeg_len=0;
    if(mbedtls_base64_decode(compressed,compressed_max,&jpeg_len,
        (const unsigned char *)logo_b64,encoded_len)!=0 || jpeg_len<60u) {
        heap_caps_free(compressed);
        return;
    }
    esp_jpeg_image_cfg_t cfg={
        .indata=compressed,
        .indata_size=(uint32_t)jpeg_len,
        .out_format=JPEG_IMAGE_FORMAT_RGB565,
        .out_scale=JPEG_IMAGE_SCALE_0,
    };
    esp_jpeg_image_output_t info={0};
    if(esp_jpeg_get_image_info(&cfg,&info)==ESP_OK &&
       info.width==150u && info.height==150u &&
       info.output_len>=150u*150u*2u && info.output_len<=150u*150u*4u) {
        logo_pixels=heap_caps_malloc(info.output_len,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        if(logo_pixels){
            cfg.outbuf=(uint8_t *)logo_pixels;
            cfg.outbuf_size=(uint32_t)info.output_len;
            esp_jpeg_image_output_t out={0};
            if(esp_jpeg_decode(&cfg,&out)!=ESP_OK || out.width!=150u || out.height!=150u){
                heap_caps_free(logo_pixels);
                logo_pixels=NULL;
            }else{
                memset(&logo_descriptor,0,sizeof(logo_descriptor));
                logo_descriptor.header.magic=LV_IMAGE_HEADER_MAGIC;
                logo_descriptor.header.cf=LV_COLOR_FORMAT_RGB565;
                logo_descriptor.header.w=150;
                logo_descriptor.header.h=150;
                logo_descriptor.data_size=(uint32_t)(150u*150u*2u);
                logo_descriptor.data=(const uint8_t *)logo_pixels;
            }
        }
    }
    heap_caps_free(compressed);
    if(!logo_pixels)ESP_LOGW(TAG,"WT32 splash logo JPEG unavailable; text splash still works");
}

static lv_obj_t *caption(lv_obj_t *parent,const char *value,
                          int offset_y,uint32_t color,const lv_font_t *font)
{
    lv_obj_t *o=lv_label_create(parent);
    lv_label_set_text(o,value);
    lv_obj_set_style_text_font(o,font,0);
    lv_obj_set_style_text_color(o,lv_color_hex(color),0);
    lv_obj_align(o,LV_ALIGN_CENTER,0,offset_y);
    return o;
}

void mira_splash_show(lv_display_t *disp)
{
    lv_display_set_default(disp);
    if(overlay)return;
    overlay=lv_obj_create(lv_layer_top());
    lv_obj_set_size(overlay,800,480);
    lv_obj_set_pos(overlay,0,0);
    lv_obj_remove_flag(overlay,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(overlay,0,0);
    lv_obj_set_style_border_width(overlay,0,0);
    lv_obj_set_style_pad_all(overlay,0,0);
    lv_obj_set_style_bg_color(overlay,lv_color_hex(0x071421),0);
    lv_obj_set_style_bg_opa(overlay,LV_OPA_COVER,0);

    load_logo();
    if(logo_pixels){
        lv_obj_t *img=lv_image_create(overlay);
        lv_image_set_src(img,&logo_descriptor);
        lv_obj_align(img,LV_ALIGN_CENTER,0,-102);
    }
    caption(overlay,"MIRA",28,0xF3F6F7,&lv_font_montserrat_40);
    caption(overlay,"PANEL",63,0x4ACBFA,&lv_font_montserrat_16);
    progress_label=caption(overlay,"Chargement 0 %",145,0xB1C2D4,&lv_font_montserrat_14);
}

void mira_splash_set_progress(uint8_t percent)
{
    if(progress_label)lv_label_set_text_fmt(progress_label,"Chargement %u %%",(unsigned)(percent>100u?100u:percent));
}

void mira_splash_hide(void)
{
    if(overlay)lv_obj_delete(overlay);
    overlay=NULL;
    progress_label=NULL;
    if(logo_pixels)heap_caps_free(logo_pixels);
    logo_pixels=NULL;
    memset(&logo_descriptor,0,sizeof(logo_descriptor));
}
