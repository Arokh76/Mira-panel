// Mira Panel LVGL 9.6 native ESP-IDF screen-port prototype for Waveshare 4.3C.
// Renders the actual /miralvgl.json programming from the 0.7.37 SPIFFS partition.
// NOT a drop-in replacement for the old Jeedom HTTP/MQTT backend yet.
// All LVGL access is kept on the LVGL task. HTTP may only set reload_pending.
#include "mira_full.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include "esp_log.h"
#include "esp_spiffs.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "cJSON.h"
#include "nvs.h"
#include "lvgl.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "mira_display.h"
#include "mira_mqtt.h"
#include "mira_jeedom.h"
#include "mira_camera.h"
#include "mira_setup_access.h"

#define MIRA_WIDTH 800
#define MIRA_HEIGHT 480
#define NAV_H 52
#define CONTENT_H (MIRA_HEIGHT - NAV_H)
#define MAX_WIDGETS 64
#define JSON_MAX 49152
static const char *TAG="mira-full-43";
static const char *JSON_PATH="/spiffs/miralvgl.json";
static volatile bool reload_pending=false;
void mira_full_request_reload(void){ reload_pending=true; }
// Strict one-way UI state ingestion. Only the LVGL task can touch the widget tree.
// No state update ever generates a Jeedom/MQTT command.
typedef struct { char name[96]; char value[160]; } mira_state_update_t;
static QueueHandle_t state_queue;
static bool suppress_events;
static uint8_t display_brightness_raw=220;
static uint16_t sleep_timeout_seconds=120;
static uint8_t sleep_brightness_percent=8;
static bool sleep_clock_enabled=true,display_sleeping;
static lv_obj_t *sleep_overlay,*sleep_clock_text;
static bool spiffs_ready=false;
static lv_obj_t *screen, *pages[6], *nav, *page_picker;
static lv_obj_t *favorite_btn[4];
static uint8_t current_page=0;
static const char *page_names[6]={"Accueil","Lumières","Températures","Paramètres","Énergie","Multimédia"};
static const int page_order[6]={0,1,2,4,5,3};
static lv_obj_t *clock_text,*date_text;
static uint32_t widget_count;
static uint32_t last_clock_ms;
static uint32_t last_user_touch_ms,last_user_touch_count;
static lv_obj_t *feedback;
static lv_obj_t *touch_diag_label;
static lv_obj_t *touch_diag_button;
static lv_obj_t *camera_overlay,*camera_image,*camera_status;
static lv_image_dsc_t camera_dsc;
static uint16_t *camera_image_pixels;
static uint32_t camera_last_update;

typedef struct {
    char name[96], unit[24], type[24], value[160];
    lv_obj_t *card,*control,*value_label,*info_status;
    lv_obj_t *fields[9]; // energy/media passive Jeedom composite infos
    lv_obj_t *media_slider,*media_device_dropdown,*media_play_caption;
    bool media_is_playing;
    char media_active_device[96];
    int minv,maxv;
} mira_widget_t;
static mira_widget_t widgets[MAX_WIDGETS];
static uint8_t widget_n=0;
static lv_color_t text_c(void){return lv_color_hex(0xF3F6F7);}
static lv_color_t muted_c(void){return lv_color_hex(0xA8B2B8);}
static lv_color_t cyan_c(void){return lv_color_hex(0x22BEEA);}
static lv_color_t panel_c(void){return lv_color_hex(0x10283D);}
static lv_color_t panel2_c(void){return lv_color_hex(0x163449);}
static lv_color_t border_c(void){return lv_color_hex(0x155A75);}
static lv_color_t bg_c(void){return lv_color_hex(0x061725);}

static const char *str(const cJSON *obj, const char *key, const char *def) {
    const cJSON *v=cJSON_GetObjectItemCaseSensitive(obj,key);
    if(cJSON_IsString(v)&&v->valuestring)return v->valuestring;
    return def;
}
static int integer(const cJSON *obj,const char *key,int def){
    const cJSON *v=cJSON_GetObjectItemCaseSensitive(obj,key);
    if(cJSON_IsNumber(v))return v->valueint;
    if(cJSON_IsString(v)&&v->valuestring&&*v->valuestring)return atoi(v->valuestring);
    return def;
}
static void text_style(lv_obj_t *obj,uint32_t color,const lv_font_t *font){
    lv_obj_set_style_text_color(obj,lv_color_hex(color),0);
    if(font)lv_obj_set_style_text_font(obj,font,0);
}
static lv_obj_t *label(lv_obj_t *parent,const char *s,uint32_t color,const lv_font_t *font){
    lv_obj_t *l=lv_label_create(parent);
    lv_label_set_text(l,s?s:"");
    text_style(l,color,font);
    return l;
}
static void no_scroll(lv_obj_t *obj){lv_obj_remove_flag(obj,LV_OBJ_FLAG_SCROLLABLE);}
static void panel_style(lv_obj_t *obj,bool light){
    lv_obj_set_style_bg_color(obj,light?panel2_c():panel_c(),0);
    lv_obj_set_style_bg_opa(obj,LV_OPA_COVER,0);
    lv_obj_set_style_border_color(obj,border_c(),0);
    lv_obj_set_style_border_width(obj,1,0);
    lv_obj_set_style_radius(obj,14,0);
    lv_obj_set_style_pad_all(obj,9,0);
    lv_obj_set_style_shadow_width(obj,0,0); // no shadows during first real UI comparison
}
static lv_obj_t *card(int page,int width,int height){
    lv_obj_t *c=lv_obj_create(pages[page]);
    lv_obj_set_size(c,width,height);
    panel_style(c,false);
    // Match the benchmark's CARD_RELIEF_OUTLINE (mode 3): a cheap 2px
    // outline, not a blurred LVGL shadow. Keep the 9.7-FPS relief strategy.
    lv_obj_set_style_shadow_width(c,0,0);
    lv_obj_set_style_outline_width(c,2,0);
    lv_obj_set_style_outline_opa(c,LV_OPA_50,0);
    lv_obj_set_style_outline_color(c,lv_color_hex(0x000000),0);
    lv_obj_set_style_outline_pad(c,0,0);
    no_scroll(c);
    return c;
}
// 0.7.37 navigation pictograms, rebuilt with LVGL 9 objects: no icon font.
static lv_obj_t *mira_nav_primitive(lv_obj_t *parent,int w,int h) {
    lv_obj_t *o=lv_obj_create(parent);
    lv_obj_set_size(o,w,h);
    lv_obj_remove_flag(o,LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(o,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(o,LV_OPA_TRANSP,0);
    lv_obj_set_style_border_width(o,0,0);
    lv_obj_set_style_pad_all(o,0,0);
    lv_obj_set_style_shadow_width(o,0,0);
    return o;
}
static void mira_nav_bulb(lv_obj_t *parent) {
    lv_obj_t *icon=mira_nav_primitive(parent,25,28);
    lv_obj_center(icon);
    lv_obj_t *head=mira_nav_primitive(icon,16,16);
    lv_obj_set_pos(head,5,0);
    lv_obj_set_style_radius(head,LV_RADIUS_CIRCLE,0);
    lv_obj_set_style_bg_color(head,lv_color_hex(0xDCEAF0),0);
    lv_obj_set_style_bg_opa(head,LV_OPA_COVER,0);
    lv_obj_t *neck=mira_nav_primitive(icon,10,4);
    lv_obj_set_pos(neck,8,17);
    lv_obj_set_style_radius(neck,2,0);
    lv_obj_set_style_bg_color(neck,lv_color_hex(0xDCEAF0),0);
    lv_obj_set_style_bg_opa(neck,LV_OPA_COVER,0);
    lv_obj_t *base=mira_nav_primitive(icon,9,4);
    lv_obj_set_pos(base,8,22);
    lv_obj_set_style_radius(base,2,0);
    lv_obj_set_style_bg_color(base,lv_color_hex(0xDCEAF0),0);
    lv_obj_set_style_bg_opa(base,LV_OPA_COVER,0);
}
static void mira_nav_thermometer(lv_obj_t *parent) {
    lv_obj_t *icon=mira_nav_primitive(parent,23,29);
    lv_obj_center(icon);
    lv_obj_t *stem=mira_nav_primitive(icon,7,20);
    lv_obj_set_pos(stem,8,2);
    lv_obj_set_style_radius(stem,4,0);
    lv_obj_set_style_border_color(stem,lv_color_hex(0xDCEAF0),0);
    lv_obj_set_style_border_width(stem,2,0);
    lv_obj_t *bulb=mira_nav_primitive(icon,13,13);
    lv_obj_set_pos(bulb,5,14);
    lv_obj_set_style_radius(bulb,LV_RADIUS_CIRCLE,0);
    lv_obj_set_style_bg_color(bulb,lv_color_hex(0xDCEAF0),0);
    lv_obj_set_style_bg_opa(bulb,LV_OPA_COVER,0);
}
static void mira_nav_pages(lv_obj_t *parent) {
    for(int dot=0;dot<4;dot++) {
        lv_obj_t *sq=mira_nav_primitive(parent,9,9);
        lv_obj_set_style_radius(sq,2,0);
        lv_obj_set_style_bg_color(sq,cyan_c(),0);
        lv_obj_set_style_bg_opa(sq,LV_OPA_COVER,0);
        lv_obj_align(sq,LV_ALIGN_CENTER,(dot%2)*14-7,(dot/2)*14-7);
    }
}
static void make_flex(lv_obj_t *obj,lv_flex_flow_t f,lv_flex_align_t main,lv_flex_align_t cross){
    lv_obj_set_flex_flow(obj,f);
    lv_obj_set_flex_align(obj,main,cross,LV_FLEX_ALIGN_CENTER);
}
static int to_page(const char *loc){
    if(!strcmp(loc,"lumieres"))return 1;
    if(!strcmp(loc,"temperatures"))return 2;
    if(!strcmp(loc,"energie")||!strcmp(loc,"energy"))return 4;
    if(!strcmp(loc,"multimedia")||!strcmp(loc,"media"))return 5;
    return 0;
}
static void show_page(int idx){
    if(idx<0||idx>5)return;
    current_page=(uint8_t)idx;
    for(int i=0;i<6;i++){
        if(i==idx)lv_obj_remove_flag(pages[i],LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(pages[i],LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_add_flag(page_picker,LV_OBJ_FLAG_HIDDEN);
}
static void nav_click(lv_event_t *e){
    if(display_sleeping)return;
    int index=(int)(intptr_t)lv_event_get_user_data(e);
    if(index==6) {
        if(lv_obj_has_flag(page_picker,LV_OBJ_FLAG_HIDDEN))
            lv_obj_remove_flag(page_picker,LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(page_picker,LV_OBJ_FLAG_HIDDEN);
    } else show_page(index);
}
static void event_local(lv_event_t *e){
    if(suppress_events||display_sleeping)return;
    mira_widget_t *b=(mira_widget_t*)lv_event_get_user_data(e);
    if(!b)return;
    lv_obj_t *src=lv_event_get_target_obj(e);
    if(!strcmp(b->type,"toggle")){
        snprintf(b->value,sizeof(b->value),"%s",lv_obj_has_state(src,LV_STATE_CHECKED)?"On":"Off");
    }else if(!strcmp(b->type,"range")||!strcmp(b->type,"roundrange")||!strcmp(b->type,"thermostat")){
        int v=(!strcmp(b->type,"roundrange")||!strcmp(b->type,"thermostat"))?lv_arc_get_value(src):lv_slider_get_value(src);
        snprintf(b->value,sizeof(b->value),"%d",v);
        if(b->value_label){
            char txt[192];snprintf(txt,sizeof(txt),"%s %s",b->value,b->unit);
            lv_label_set_text(b->value_label,txt);
        }
    }else if(!strcmp(b->type,"color")){
        // Match Mira 0.7.37's RGB CSV action payload. LVGL 9 no longer
        // provides the old colour wheel; use a hue arc at full saturation.
        const uint16_t hue=(uint16_t)lv_arc_get_value(src);
        const lv_color_t col=lv_color_hsv_to_rgb(hue,100,100);
        const uint32_t rgb=lv_color_to_u32(col);
        snprintf(b->value,sizeof(b->value),"%u,%u,%u",
                 (unsigned)((rgb>>16)&255u),(unsigned)((rgb>>8)&255u),
                 (unsigned)(rgb&255u));
    }else if(!strcmp(b->type,"list")||!strcmp(b->type,"listbutton")){
        // The selected label is the outward command value, as in the
        // original Mira list controls. No network command on passive state.
        lv_dropdown_get_selected_str(src,b->value,sizeof(b->value));
    }else if(!strcmp(b->type,"button")||!strcmp(b->type,"multiaction")){
        strcpy(b->value,"1");
    }
    // Unlike MQTT States, only a real human LVGL action may publish Actions.
    const char *payload=(!strcmp(b->type,"toggle")) ? (!strcmp(b->value,"On")?"1":"0") : b->value;
    const bool use_jeedom=mira_jeedom_actions_enabled();
    const bool mqtt_sent=!use_jeedom&&mira_mqtt_publish_action(b->name,payload);
    const bool jeedom_queued=use_jeedom&&mira_jeedom_queue_local_action(b->name,b->type,payload);
    if(feedback) {
        char result[180];
        snprintf(result,sizeof(result),"%s : %.60s = %.60s",
                  jeedom_queued?"Action Jeedom en file":(mqtt_sent?"Action MQTT":"Aperçu local"),b->name,b->value);
        lv_label_set_text(feedback,result);
    }
}
static int clamp(int a,int lo,int hi){return a<lo?lo:(a>hi?hi:a);}

// Passive playback-device list updates and currently selected speaker
// are independent. A device Info is never a command.
static void media_select_active_device(mira_widget_t *w){
    if(!w||!w->media_device_dropdown||!w->media_active_device[0])return;
    const char *options=lv_dropdown_get_options(w->media_device_dropdown);
    if(!options)return;
    uint16_t index=0;
    const char *current=options;
    const size_t wanted=strlen(w->media_active_device);
    while(*current){
        const char *next=strchr(current,'\n');
        size_t length=next?(size_t)(next-current):strlen(current);
        if(index>0&&length==wanted&&!strncmp(current,w->media_active_device,length)){
            lv_dropdown_set_selected(w->media_device_dropdown,index);
            return;
        }
        if(!next)break;
        current=next+1;
        index++;
    }
}

// Multimedia LVGL controls use the *same* action names as the existing
// Mira 0.7.37 Jeedom and MQTT plugins.  No HTTP/network work in LVGL callback.
// Encode the immutable static widget index and control ordinal as user_data.
static void media_direct_action(lv_event_t *event) {
    if(suppress_events||display_sleeping)return;
    uintptr_t encoded=(uintptr_t)lv_event_get_user_data(event);
    if(encoded==0)return;
    encoded--;
    const size_t index=encoded>>3,action=encoded&7u;
    if(index>=widget_n||action>4)return;
    mira_widget_t *w=&widgets[index];
    if(strcmp(w->type,"media"))return;
    char command[128],payload[48]="1";
    const char *suffix=NULL;
    if(action==0)suffix="previous";
    if(action==1)suffix=w->media_is_playing?"pause":"play";
    if(action==2)suffix="next";
    if(action==3){
        if(!w->media_slider)return;
        suffix="volume_cmd";
        snprintf(payload,sizeof(payload),"%ld",(long)lv_slider_get_value(w->media_slider));
    }
    if(action==4){
        if(!w->media_device_dropdown)return;
        const uint16_t choice=lv_dropdown_get_selected(w->media_device_dropdown);
        if(choice==0)return; // Placeholder is not a real playback target.
        suffix="device_cmd";
        snprintf(payload,sizeof(payload),"%u",(unsigned)(choice-1));
    }
    if(!suffix)return;
    snprintf(command,sizeof(command),"%.95s__%s",w->name,suffix);
    const bool jeedom=mira_jeedom_actions_enabled();
    const bool queued=jeedom&&mira_jeedom_queue_local_action(
        command,(action>=3)?"range":"button",payload);
    const bool published=!jeedom&&mira_mqtt_publish_action(command,payload);
    if(action==1&&(queued||published)){
        w->media_is_playing=!w->media_is_playing;
        if(w->media_play_caption)lv_label_set_text(w->media_play_caption,w->media_is_playing?"Pause":"Lecture");
    }
    if(feedback){
        char result[180];
        snprintf(result,sizeof(result),"%s : %.112s",
            queued?"Jeedom en file":(published?"MQTT envoyé":"Action inactive"),command);
        lv_label_set_text(feedback,result);
    }
}
static void media_add_button(mira_widget_t *w,int action,int x,const char *text){
    lv_obj_t *button=lv_button_create(w->card);
    lv_obj_set_size(button,142,43);
    lv_obj_set_pos(button,x,203);
    panel_style(button,true);
    lv_obj_t *caption=label(button,text,0xF3F6F7,&lv_font_montserrat_16);
    lv_obj_center(caption);
    if(action==1)w->media_play_caption=caption;
    uintptr_t context=((uintptr_t)(w-widgets)<<3)|(uintptr_t)action;
    lv_obj_add_event_cb(button,media_direct_action,LV_EVENT_CLICKED,(void*)(context+1));
}


// Native camera popup. The worker delivers RGB565 pixels via a mutex, but
// only the LVGL render task ever creates/destroys LVGL objects or image dsc.
static void camera_close(void) {
    mira_camera_stop();
    if(camera_image)lv_image_set_src(camera_image,NULL);
    if(camera_overlay)lv_obj_delete(camera_overlay);
    camera_overlay=NULL;camera_image=NULL;camera_status=NULL;
    if(camera_image_pixels)heap_caps_free(camera_image_pixels);
    camera_image_pixels=NULL;
    memset(&camera_dsc,0,sizeof(camera_dsc));
}
static void camera_close_event(lv_event_t *event) {
    (void)event;
    camera_close();
}
static void camera_open_event(lv_event_t *event) {
    if(display_sleeping)return;
    uintptr_t opaque=(uintptr_t)lv_event_get_user_data(event);
    if(!opaque)return;
    const size_t idx=opaque-1u;
    if(idx>=widget_n||strcmp(widgets[idx].type,"camera"))return;
    camera_close();
    camera_overlay=lv_obj_create(lv_layer_top());
    lv_obj_set_size(camera_overlay,800,480);
    lv_obj_align(camera_overlay,LV_ALIGN_CENTER,0,0);
    lv_obj_set_style_bg_color(camera_overlay,lv_color_hex(0x000000),0);
    lv_obj_set_style_bg_opa(camera_overlay,LV_OPA_COVER,0);
    lv_obj_set_style_border_width(camera_overlay,0,0);
    lv_obj_set_style_radius(camera_overlay,0,0);
    lv_obj_set_style_pad_all(camera_overlay,0,0);
    no_scroll(camera_overlay);
    lv_obj_t *back=lv_button_create(camera_overlay);
    lv_obj_set_size(back,66,40);
    lv_obj_align(back,LV_ALIGN_TOP_LEFT,5,5);
    lv_obj_add_event_cb(back,camera_close_event,LV_EVENT_CLICKED,NULL);
    lv_obj_t *caption=label(back,"Retour",0xffffff,&lv_font_montserrat_16);
    lv_obj_center(caption);
    lv_obj_t *heading=label(camera_overlay,widgets[idx].name,0xffffff,&lv_font_montserrat_16);
    lv_obj_align(heading,LV_ALIGN_TOP_MID,25,12);
    camera_image=lv_image_create(camera_overlay);
    lv_obj_align(camera_image,LV_ALIGN_CENTER,0,22);
    camera_status=label(camera_overlay,"Chargement caméra BMP/JPEG...",0xffffff,&lv_font_montserrat_16);
    lv_obj_set_width(camera_status,770);
    lv_obj_set_style_text_align(camera_status,LV_TEXT_ALIGN_CENTER,0);
    lv_obj_align(camera_status,LV_ALIGN_CENTER,0,15);
    if(!mira_camera_start(widgets[idx].name))
        lv_label_set_text(camera_status,"Configurer une URL HTTP BMP/JPEG dans Jeedom > Mira > Commandes");
    camera_last_update=lv_tick_get();
}
static void camera_poll_on_lvgl_task(uint32_t tick){
    if(!camera_overlay||tick-camera_last_update<85u)return;
    camera_last_update=tick;
    mira_camera_frame_t frame={0};
    if(!mira_camera_take(&frame))return;
    // A malformed frame must release ownership even if it cannot be shown.
    if(!frame.pixels||!frame.width||!frame.height){
        if(frame.pixels)heap_caps_free(frame.pixels);
        return;
    }
    if(camera_image)lv_image_set_src(camera_image,NULL);
    if(camera_image_pixels)heap_caps_free(camera_image_pixels);
    camera_image_pixels=frame.pixels;
    memset(&camera_dsc,0,sizeof(camera_dsc));
    camera_dsc.header.magic=LV_IMAGE_HEADER_MAGIC;
    camera_dsc.header.cf=LV_COLOR_FORMAT_RGB565;
    camera_dsc.header.w=frame.width;
    camera_dsc.header.h=frame.height;
    camera_dsc.data_size=(uint32_t)frame.width*(uint32_t)frame.height*2u;
    camera_dsc.data=(const uint8_t*)camera_image_pixels;
    lv_image_set_src(camera_image,&camera_dsc);
    lv_obj_align(camera_image,LV_ALIGN_CENTER,0,22);
    if(camera_status)lv_obj_add_flag(camera_status,LV_OBJ_FLAG_HIDDEN);
}

static void make_widget(const cJSON *o,int info_total,int *info_seen){
    if(widget_n>=MAX_WIDGETS){ESP_LOGW(TAG,"Widget capacity reached (%d)",MAX_WIDGETS);return;}
    const char *type=str(o,"type",""),*loc=str(o,"emplacement","accueil");
    const char *style=str(o,"style","standard"),*layout=str(o,"layout","auto");
    int pg=to_page(loc),is_auto=!strcmp(layout,"auto");
    const char *name=str(o,"name","");
    const char *display=str(o,"displayName","");
    if(!*display)display=name;
    if(!*type){ESP_LOGW(TAG,"Skipping widget without type");return;}
    if(!*name && strcmp(type,"datetime")){
        ESP_LOGW(TAG,"Skipping unnamed widget type '%s' to prevent ambiguous commands",type);
        return;
    }
    // An unlabelled placeholder can be confused with a working command.
    // Keep the original type visible for accurate migration diagnostics.
    // Do not silently turn unsupported widgets into passive info cards or
    // consume one of the 64 available action slots. Unknown commands would
    // otherwise appear to work while having no functional control.
    static const char *const supported[]={
        "datetime","info","button","multiaction","toggle","range",
        "roundrange","thermostat","list","listbutton","color",
        "energy","media","camera"
    };
    bool recognized=false;
    for(size_t n=0;n<sizeof(supported)/sizeof(supported[0]);n++){
        if(!strcmp(type,supported[n])){recognized=true;break;}
    }
    if(!recognized){
        ESP_LOGW(TAG,"Unsupported widget type '%s' (name='%s')",type,name);
        // Leave a visible, explicitly unsupported placeholder instead of
        // silently losing a programmed Mira widget during migration.
        lv_obj_t *notice=card(pg,390,64);
        lv_obj_t *caption=label(notice,display,0xF3F6F7,&lv_font_montserrat_16);
        lv_obj_set_width(caption,350);
        char missing[96];
        snprintf(missing,sizeof(missing),"Type non porté : %.65s",type);
        lv_obj_t *warning=label(notice,missing,0xFFB35C,&lv_font_montserrat_12);
        lv_obj_align(warning,LV_ALIGN_BOTTOM_LEFT,0,0);
        return;
    }
    mira_widget_t *w=&widgets[widget_n++];
    memset(w,0,sizeof(*w));
    snprintf(w->name,sizeof(w->name),"%s",name);
    snprintf(w->unit,sizeof(w->unit),"%s",str(o,"unit",""));
    snprintf(w->type,sizeof(w->type),"%s",type);
    snprintf(w->value,sizeof(w->value),"%s",str(o,"value",""));
    w->minv=integer(o,"min",0);w->maxv=integer(o,"max",100);
    if(w->maxv<=w->minv)w->maxv=w->minv+100;
    int cw=390;
    if(!strcmp(layout,"tile"))cw=192;
    if(!strcmp(layout,"wide")||!strcmp(layout,"hero"))cw=786;
    if(!strcmp(type,"datetime")){
        lv_obj_t *c=card(pg,786,158);
        lv_obj_set_style_bg_opa(c,LV_OPA_TRANSP,0);
        lv_obj_set_style_border_width(c,0,0);
        lv_obj_set_style_pad_all(c,0,0);
        clock_text=label(c,"--:--",0xffffff,&lv_font_montserrat_48);
        lv_obj_set_size(clock_text,370,92);
        lv_obj_set_style_text_align(clock_text,LV_TEXT_ALIGN_CENTER,0);
        lv_obj_set_pos(clock_text,14,12);
        date_text=label(c,"-- -- ----",0xA8B2B8,&lv_font_montserrat_16);
        lv_obj_set_size(date_text,370,26);
        lv_obj_set_style_text_align(date_text,LV_TEXT_ALIGN_CENTER,0);
        lv_obj_set_pos(date_text,14,125);
        lv_obj_t *right=label(c,"Mira Panel",0xA8B2B8,&lv_font_montserrat_16);
        lv_obj_set_pos(right,422,36);
        w->card=c;return;
    }
    if(!strcmp(type,"info")){
        bool home_tile=(pg==0&&!strcmp(style,"standard")&&is_auto);
        if(home_tile||(!strcmp(layout,"tile"))){
            if(home_tile)(*info_seen)++;
            int cols=4;
            if(home_tile && info_total>0){
                int remain=info_total%4;
                if(remain==0)remain=4;
                if(*info_seen>info_total-remain)cols=remain;
                if(cols<2)cols=2;
            }
            int width=home_tile?(786-(cols-1)*6)/cols:192;
            int height=home_tile?142:78;
            w->card=card(pg,width,height);
            lv_obj_set_style_pad_all(w->card,4,0);
            lv_obj_t *nm=label(w->card,display,0xA8B2B8,&lv_font_montserrat_16);
            lv_obj_set_size(nm,width-16,34);
            lv_obj_set_style_text_align(nm,LV_TEXT_ALIGN_CENTER,0);
            lv_obj_align(nm,LV_ALIGN_TOP_MID,0,home_tile?12:1);
            char value[198];snprintf(value,sizeof(value),"%s%s%s",*w->value?w->value:"--",*w->unit?" ":"",w->unit);
            w->value_label=label(w->card,value,0x22BEEA,home_tile?&lv_font_montserrat_28:&lv_font_montserrat_20);
            lv_obj_set_size(w->value_label,width-14,35);
            lv_obj_set_style_text_align(w->value_label,LV_TEXT_ALIGN_CENTER,0);
            lv_obj_align(w->value_label,LV_ALIGN_TOP_MID,0,home_tile?48:26);
            if(home_tile){
                lv_obj_t *since=label(w->card,*w->value?"Valeur configurée":"En attente des données",0xA8B2B8,&lv_font_montserrat_12);
                w->info_status=since;
                lv_obj_set_width(since,width-18);
                lv_obj_set_style_text_align(since,LV_TEXT_ALIGN_CENTER,0);
                lv_obj_align(since,LV_ALIGN_BOTTOM_MID,0,-10);
            }
            return;
        }
        bool wide=!strcmp(layout,"wide");
        w->card=card(pg,wide?786:cw,wide?56:36);
        make_flex(w->card,LV_FLEX_FLOW_ROW,wide?LV_FLEX_ALIGN_SPACE_BETWEEN:LV_FLEX_ALIGN_CENTER,LV_FLEX_ALIGN_CENTER);
        label(w->card,display,0xA8B2B8,&lv_font_montserrat_16);
        char value[198];snprintf(value,sizeof(value),"%s %s",*w->value?w->value:"--",w->unit);
        w->value_label=label(w->card,value,0x22BEEA,&lv_font_montserrat_20);
        return;
    }
    if(!strcmp(type,"button")||!strcmp(type,"multiaction")){
        w->card=card(pg,cw,54);
        lv_obj_set_style_bg_opa(w->card,LV_OPA_TRANSP,0);
        lv_obj_set_style_border_width(w->card,0,0);
        lv_obj_t *bt=lv_button_create(w->card);
        w->control=bt;
        lv_obj_set_size(bt,360,44);lv_obj_center(bt);
        panel_style(bt,true);lv_obj_set_style_radius(bt,13,0);
        lv_obj_t *t=label(bt,display,0xF3F6F7,&lv_font_montserrat_16);lv_obj_center(t);
        lv_obj_add_event_cb(bt,event_local,LV_EVENT_CLICKED,w);
        return;
    }
    if(!strcmp(type,"toggle")){
        w->card=card(pg,cw,54);
        lv_obj_set_style_pad_left(w->card,16,0);
        lv_obj_set_style_pad_right(w->card,16,0);
        make_flex(w->card,LV_FLEX_FLOW_ROW,LV_FLEX_ALIGN_SPACE_BETWEEN,LV_FLEX_ALIGN_CENTER);
        label(w->card,display,0xF3F6F7,&lv_font_montserrat_16);
        w->control=lv_switch_create(w->card);
        lv_obj_set_size(w->control,54,28);
        lv_obj_set_style_bg_color(w->control,cyan_c(),LV_PART_INDICATOR|LV_STATE_CHECKED);
        lv_obj_add_event_cb(w->control,event_local,LV_EVENT_VALUE_CHANGED,w);
        return;
    }
    if(!strcmp(type,"range")||!strcmp(type,"roundrange")||!strcmp(type,"thermostat")){
        bool round=!strcmp(type,"roundrange")||!strcmp(type,"thermostat");
        w->card=card(pg,round?(!strcmp(type,"thermostat")?786:390):cw,round?225:80);
        if(round){
            label(w->card,display,0xF3F6F7,&lv_font_montserrat_16);
            w->control=lv_arc_create(w->card);
            lv_obj_set_size(w->control,165,165);
            lv_obj_align(w->control,LV_ALIGN_CENTER,0,14);
            lv_arc_set_range(w->control,w->minv,w->maxv);
            lv_arc_set_value(w->control,clamp(atoi(w->value),w->minv,w->maxv));
            lv_arc_set_rotation(w->control,135);
            lv_arc_set_bg_angles(w->control,0,270);
            lv_obj_set_style_arc_color(w->control,cyan_c(),LV_PART_INDICATOR);
            w->value_label=label(w->card,"--",0xF3F6F7,&lv_font_montserrat_28);
            lv_obj_align(w->value_label,LV_ALIGN_CENTER,0,14);
        }else{
            make_flex(w->card,LV_FLEX_FLOW_COLUMN,LV_FLEX_ALIGN_START,LV_FLEX_ALIGN_CENTER);
            lv_obj_t *r=lv_obj_create(w->card);
            lv_obj_set_size(r,365,26);
            no_scroll(r);
            lv_obj_set_style_bg_opa(r,LV_OPA_TRANSP,0);
            lv_obj_set_style_border_width(r,0,0);
            lv_obj_set_style_pad_all(r,0,0);
            make_flex(r,LV_FLEX_FLOW_ROW,LV_FLEX_ALIGN_SPACE_BETWEEN,LV_FLEX_ALIGN_CENTER);
            label(r,display,0xF3F6F7,&lv_font_montserrat_16);
            w->value_label=label(r,*w->value?w->value:"--",0x22BEEA,&lv_font_montserrat_16);
            w->control=lv_slider_create(w->card);
            lv_obj_set_size(w->control,350,14);
            lv_slider_set_range(w->control,w->minv,w->maxv);
            lv_slider_set_value(w->control,clamp(atoi(w->value),w->minv,w->maxv),LV_ANIM_OFF);
            lv_obj_set_style_bg_color(w->control,cyan_c(),LV_PART_INDICATOR);
            lv_obj_set_style_bg_color(w->control,lv_color_hex(0xEAF7FF),LV_PART_KNOB);
        }
        // Only issue a Jeedom/MQTT command after the user finishes moving
        // the slider/arc. VALUE_CHANGED can generate dozens of requests.
        lv_obj_add_event_cb(w->control,event_local,LV_EVENT_RELEASED,w);
        return;
    }
    if(!strcmp(type,"list")||!strcmp(type,"listbutton")){
        w->card=card(pg,cw,96);
        label(w->card,display,0xF3F6F7,&lv_font_montserrat_16);
        w->control=lv_dropdown_create(w->card);
        lv_obj_set_size(w->control,340,44);
        char options[512];snprintf(options,sizeof(options),"%s",w->value);
        for(char *p=options;*p;p++)if(*p=='|')*p='\n';
        lv_dropdown_set_options(w->control,*options?options:"--");
        lv_obj_align(w->control,LV_ALIGN_BOTTOM_MID,0,-4);
        lv_obj_add_event_cb(w->control,event_local,LV_EVENT_VALUE_CHANGED,w);
        return;
    }
    if(!strcmp(type,"color")){
        w->card=card(pg,cw,206);
        label(w->card,display,0xF3F6F7,&lv_font_montserrat_16);
        // LVGL 9.6 dropped the LVGL8 colorwheel. A hue arc provides
        // functional RGB output, sent once on finger release.
        w->control=lv_arc_create(w->card);
        lv_obj_set_size(w->control,150,150);
        lv_arc_set_range(w->control,0,360);
        lv_arc_set_value(w->control,200);
        lv_arc_set_rotation(w->control,135);
        lv_arc_set_bg_angles(w->control,0,270);
        lv_obj_set_style_arc_color(w->control,cyan_c(),LV_PART_INDICATOR);
        lv_obj_align(w->control,LV_ALIGN_CENTER,0,12);
        lv_obj_add_event_cb(w->control,event_local,LV_EVENT_RELEASED,w);
        return;
    }
    // Energy and media are native passive Jeedom data cards. Every subfield
    // keeps its original <widget>__<field> name; no fake actions on receipt.
    if(!strcmp(type,"energy")||!strcmp(type,"media")){
        static const char *const energy_fields[]={"solar","home","grid","battery","soc","soc2","soc3"};
        static const char *const energy_titles[]={"Solaire","Maison","Réseau","Batterie","SOC","SOC 2","SOC 3"};
        static const char *const media_fields[]={"title","artist","album","state","progress","volume","cover","device","devices"};
        static const char *const media_titles[]={"Titre","Artiste","Album","État","Progression","Volume","Pochette","Appareil","Appareils"};
        bool media=!strcmp(type,"media");
        int total=media?9:7;
        w->card=card(pg,786,media?355:205);
        lv_obj_t *heading=label(w->card,display,0xF3F6F7,&lv_font_montserrat_20);
        lv_obj_set_pos(heading,10,4);
        for(int n=0;n<total;n++){
            int col=n/5,row=n%5;
            int x=col?400:12,y=35+row*31;
            const char *title=media?media_titles[n]:energy_titles[n];
            lv_obj_t *name_label=label(w->card,title,0xA8B2B8,&lv_font_montserrat_16);
            lv_obj_set_pos(name_label,x,y);
            lv_obj_set_width(name_label,105);
            w->fields[n]=label(w->card,"--",0x22BEEA,&lv_font_montserrat_16);
            lv_obj_set_pos(w->fields[n],x+112,y);
            lv_obj_set_width(w->fields[n],245);
        }
        if(media){
            media_add_button(w,0,125,"Précédent");
            media_add_button(w,1,307,"Lecture");
            media_add_button(w,2,489,"Suivant");
            lv_obj_t *volume_text=label(w->card,"Volume",0xA8B2B8,&lv_font_montserrat_16);
            lv_obj_set_pos(volume_text,15,254);
            w->media_slider=lv_slider_create(w->card);
            lv_obj_set_size(w->media_slider,625,15);
            lv_obj_set_pos(w->media_slider,118,262);
            lv_slider_set_range(w->media_slider,0,100);
            lv_slider_set_value(w->media_slider,40,LV_ANIM_OFF);
            lv_obj_set_style_bg_color(w->media_slider,cyan_c(),LV_PART_INDICATOR);
            uintptr_t vol=((uintptr_t)(w-widgets)<<3)|3u;
            lv_obj_add_event_cb(w->media_slider,media_direct_action,LV_EVENT_RELEASED,(void*)(vol+1));
            lv_obj_t *device_text=label(w->card,"Sortie",0xA8B2B8,&lv_font_montserrat_16);
            lv_obj_set_pos(device_text,15,293);
            w->media_device_dropdown=lv_dropdown_create(w->card);
            lv_obj_set_size(w->media_device_dropdown,625,39);
            lv_obj_set_pos(w->media_device_dropdown,118,290);
            lv_dropdown_set_options(w->media_device_dropdown,"Sélectionner un appareil");
            uintptr_t device=((uintptr_t)(w-widgets)<<3)|4u;
            lv_obj_add_event_cb(w->media_device_dropdown,media_direct_action,LV_EVENT_VALUE_CHANGED,(void*)(device+1));
        }
        return;
    }
    if(!strcmp(type,"camera")){
        w->card=card(pg,cw,86);
        w->control=lv_button_create(w->card);
        lv_obj_set_size(w->control,cw-24,57);
        lv_obj_center(w->control);
        lv_obj_set_style_bg_color(w->control,panel2_c(),0);
        lv_obj_set_style_border_color(w->control,cyan_c(),0);
        lv_obj_set_style_border_width(w->control,1,0);
        lv_obj_t *caption=label(w->control,display,0xF3F6F7,&lv_font_montserrat_16);
        lv_obj_center(caption);
        uintptr_t idx=(uintptr_t)(w-widgets)+1u;
        lv_obj_add_event_cb(w->control,camera_open_event,LV_EVENT_CLICKED,(void*)idx);
        return;
    }
    w->card=card(pg,cw,50);
    label(w->card,display,0xA8B2B8,&lv_font_montserrat_16);
}

// Match the old Waveshare43CDisplay brightness behavior, but save only on
// release (never write flash on every pixel of a drag).  This callback runs
// on LVGL task; the CH32 gate/PWM write is a short I2C transaction.
static void brightness_release(lv_event_t *event) {
    if(suppress_events||display_sleeping)return;
    lv_obj_t *slider=lv_event_get_target_obj(event);
    int value=lv_slider_get_value(slider);
    if(value<30)value=30;
    if(value>255)value=255;
    // Slider release can occur without a value change; do not wear NVS.
    if(display_brightness_raw==(uint8_t)value)return;
    const esp_err_t err=mira_display_set_brightness((uint8_t)value);
    if(err!=ESP_OK){
        ESP_LOGE(TAG,"Waveshare brightness update failed: %s",esp_err_to_name(err));
        return;
    }
    display_brightness_raw=(uint8_t)value;
    nvs_handle_t nvs;
    if(nvs_open("mira_idf_ui",NVS_READWRITE,&nvs)==ESP_OK){
        if(nvs_set_u8(nvs,"brightness",display_brightness_raw)==ESP_OK)
            (void)nvs_commit(nvs);
        nvs_close(nvs);
    }
}


static void save_sleep_setting_u16(const char *key,uint16_t value){
    nvs_handle_t nv;
    if(nvs_open("mira_idf_ui",NVS_READWRITE,&nv)!=ESP_OK)return;
    if(nvs_set_u16(nv,key,value)==ESP_OK)(void)nvs_commit(nv);
    nvs_close(nv);
}
static void save_sleep_setting_u8(const char *key,uint8_t value){
    nvs_handle_t nv;
    if(nvs_open("mira_idf_ui",NVS_READWRITE,&nv)!=ESP_OK)return;
    if(nvs_set_u8(nv,key,value)==ESP_OK)(void)nvs_commit(nv);
    nvs_close(nv);
}
static void sleep_delay_changed(lv_event_t *event){
    if(display_sleeping||suppress_events)return;
    int value=lv_slider_get_value(lv_event_get_target_obj(event));
    if(value<0)value=0;
    if(value>3600)value=3600;
    if(sleep_timeout_seconds!=(uint16_t)value){
        sleep_timeout_seconds=(uint16_t)value;
        save_sleep_setting_u16("sleep_delay",sleep_timeout_seconds);
    }
}
static void sleep_light_changed(lv_event_t *event){
    if(display_sleeping||suppress_events)return;
    int value=lv_slider_get_value(lv_event_get_target_obj(event));
    if(value<0)value=0;
    if(value>100)value=100;
    if(sleep_brightness_percent!=(uint8_t)value){
        sleep_brightness_percent=(uint8_t)value;
        save_sleep_setting_u8("sleep_light",sleep_brightness_percent);
    }
}
static void sleep_clock_changed(lv_event_t *event){
    if(display_sleeping||suppress_events)return;
    bool enabled=lv_obj_has_state(lv_event_get_target_obj(event),LV_STATE_CHECKED);
    if(sleep_clock_enabled!=enabled){
        sleep_clock_enabled=enabled;
        save_sleep_setting_u8("sleep_clock",enabled?1:0);
    }
}
static uint8_t raw_from_percent(uint8_t percentage){
    if(percentage==0)return 0;
    if(percentage>100)percentage=100;
    return (uint8_t)(30u+(((uint32_t)percentage-1u)*225u+49u)/99u);
}
static void display_enter_sleep(void){
    if(display_sleeping)return;
    const uint8_t raw=sleep_clock_enabled?raw_from_percent(sleep_brightness_percent):0;
    esp_err_t err=mira_display_set_brightness(raw);
    if(err!=ESP_OK){
        ESP_LOGW(TAG,"Unable to lower backlight for sleep: %s",esp_err_to_name(err));
        return;
    }
    display_sleeping=true;
    camera_close();
    if(sleep_overlay && sleep_clock_enabled && raw>0)
        lv_obj_remove_flag(sleep_overlay,LV_OBJ_FLAG_HIDDEN);
    ESP_LOGI(TAG,"Mira sleep entered, clock=%d, brightness=%u%%",
             sleep_clock_enabled,(unsigned)sleep_brightness_percent);
}
static void display_wake(void){
    if(!display_sleeping)return;
    display_sleeping=false;
    if(sleep_overlay)lv_obj_add_flag(sleep_overlay,LV_OBJ_FLAG_HIDDEN);
    ESP_ERROR_CHECK_WITHOUT_ABORT(mira_display_set_brightness(display_brightness_raw));
    ESP_LOGI(TAG,"Mira display awakened by physical GT911 touch");
}

static void settings(void){
    lv_obj_t *head=card(3,640,40);
    lv_obj_t *h=label(head,"Paramètres",0xF3F6F7,&lv_font_montserrat_16);
    lv_obj_center(h);
    lv_obj_t *display=card(3,640,130);
    make_flex(display,LV_FLEX_FLOW_COLUMN,LV_FLEX_ALIGN_START,LV_FLEX_ALIGN_START);
    label(display,"Affichage",0x22BEEA,&lv_font_montserrat_16);
    label(display,"Luminosité écran",0xF3F6F7,&lv_font_montserrat_16);
    lv_obj_t *sl=lv_slider_create(display);
    lv_obj_set_size(sl,600,14);
    lv_slider_set_range(sl,30,255);
    lv_slider_set_value(sl,display_brightness_raw,LV_ANIM_OFF);
    lv_obj_add_event_cb(sl,brightness_release,LV_EVENT_RELEASED,NULL);
    label(display,"Thème : Mira",0xA8B2B8,&lv_font_montserrat_16);
    lv_obj_t *n=card(3,640,72);
    label(n,"Navigation",0x22BEEA,&lv_font_montserrat_16);
    label(n,"Retour accueil : 60 s",0xF3F6F7,&lv_font_montserrat_16);
    lv_obj_t *sleep=card(3,640,214);
    lv_obj_t *shead=label(sleep,"Veille",0x22BEEA,&lv_font_montserrat_16);
    lv_obj_set_pos(shead,12,6);
    lv_obj_t *delay_title=label(sleep,"Délai de veille (0 = désactivée)",0xF3F6F7,&lv_font_montserrat_16);
    lv_obj_set_pos(delay_title,12,38);
    lv_obj_t *delay=lv_slider_create(sleep);
    lv_obj_set_size(delay,575,14);
    lv_obj_set_pos(delay,18,75);
    lv_slider_set_range(delay,0,3600);
    lv_slider_set_value(delay,sleep_timeout_seconds,LV_ANIM_OFF);
    lv_obj_add_event_cb(delay,sleep_delay_changed,LV_EVENT_RELEASED,NULL);
    lv_obj_t *light_title=label(sleep,"Luminosité en veille (%)",0xF3F6F7,&lv_font_montserrat_16);
    lv_obj_set_pos(light_title,12,104);
    lv_obj_t *dim=lv_slider_create(sleep);
    lv_obj_set_size(dim,575,14);
    lv_obj_set_pos(dim,18,138);
    lv_slider_set_range(dim,0,100);
    lv_slider_set_value(dim,sleep_brightness_percent,LV_ANIM_OFF);
    lv_obj_add_event_cb(dim,sleep_light_changed,LV_EVENT_RELEASED,NULL);
    lv_obj_t *clock_label=label(sleep,"Horloge en veille",0xF3F6F7,&lv_font_montserrat_16);
    lv_obj_set_pos(clock_label,12,169);
    lv_obj_t *clock_switch=lv_switch_create(sleep);
    lv_obj_set_pos(clock_switch,545,163);
    if(sleep_clock_enabled)lv_obj_add_state(clock_switch,LV_STATE_CHECKED);
    lv_obj_add_event_cb(clock_switch,sleep_clock_changed,LV_EVENT_VALUE_CHANGED,NULL);
}
static void empty_notice(const char *message){
    lv_obj_t *c=card(0,786,120);
    lv_obj_t *l=label(c,message,0xF3F6F7,&lv_font_montserrat_20);
    lv_obj_set_width(l,740);
    lv_label_set_long_mode(l,LV_LABEL_LONG_WRAP);
    lv_obj_center(l);
    lv_obj_set_style_text_align(l,LV_TEXT_ALIGN_CENTER,0);
}
static void build_config(void){
    // Validate the new programming before deleting live LVGL objects.
    // A damaged/incomplete SPIFFS read must not blank a running panel.
    FILE *f=spiffs_ready?fopen(JSON_PATH,"rb"):NULL;
    if(!f){
        if(widget_n==0)empty_notice("Mira LVGL 9.6 : configuration non trouvée.\n"
                     "Importer le vrai miralvgl.json via 192.168.4.1/mira");
        ESP_LOGW(TAG,"Mira 0.7.37 JSON absent, import via /mira");
        if(widget_n==0)show_page(0);
        return;
    }
    if(fseek(f,0,SEEK_END)!=0){fclose(f);if(!widget_n)empty_notice("Erreur de lecture JSON Mira");return;}
    long len=ftell(f);rewind(f);
    if(len<=0||len>JSON_MAX){fclose(f);if(!widget_n)empty_notice("JSON Mira trop grand ou vide");return;}
    char *buf=malloc((size_t)len+1);
    if(!buf){fclose(f);if(!widget_n)empty_notice("Mémoire insuffisante pour JSON Mira");return;}
    size_t n=fread(buf,1,(size_t)len,f);fclose(f);
    if(n!=(size_t)len){
        free(buf);
        if(!widget_n)empty_notice("Lecture incomplète de la configuration Mira");
        ESP_LOGE(TAG,"Incomplete Mira JSON read: %u of %ld bytes",(unsigned)n,len);
        return;
    }
    buf[n]=0;
    cJSON *root=cJSON_ParseWithLength(buf,n);free(buf);
    cJSON *array=root?cJSON_GetObjectItemCaseSensitive(root,"lvgl"):NULL;
    if(!cJSON_IsArray(array)){
        if(!widget_n)empty_notice("Le JSON Mira ne contient pas de tableau lvgl");
        if(root)cJSON_Delete(root);
        ESP_LOGE(TAG,"Invalid Mira JSON: expected lvgl array; existing SPIFFS left unchanged");
        return;
    }
    // Reject structurally malformed programming before any screen changes.
    // A valid export can legitimately have an empty lvgl array.
    const int count=cJSON_GetArraySize(array);
    for(int i=0;i<count;i++){
        if(!cJSON_IsObject(cJSON_GetArrayItem(array,i))){
            ESP_LOGE(TAG,"Invalid Mira widget at index %d; preserving live UI",i);
            cJSON_Delete(root);
            return;
        }
    }
    widget_n=0;clock_text=NULL;date_text=NULL;widget_count=0;
    for(int i=0;i<6;i++)if(i!=3)lv_obj_clean(pages[i]);
    int home_info=0,seen=0;
    for(int i=0;i<count;i++){
        cJSON *it=cJSON_GetArrayItem(array,i);
        if(!strcmp(str(it,"type",""),"info")&&!strcmp(str(it,"emplacement","accueil"),"accueil")
           &&!strcmp(str(it,"style","standard"),"standard")&&!strcmp(str(it,"layout","auto"),"auto"))home_info++;
    }
    for(int i=0;i<count;i++){
        cJSON *it=cJSON_GetArrayItem(array,i);
        if(cJSON_IsObject(it))make_widget(it,home_info,&seen);
    }
    widget_count=(uint32_t)widget_n;
    cJSON_Delete(root);
    if(!count)empty_notice("Aucun widget programmé : importer la configuration Jeedom");
    ESP_LOGI(TAG,"Mira JSON chargé, widgets=%u, heap PSRAM libre=%u",
             widget_n,(unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    show_page(0);
}
static void create_picker(void){
    page_picker=lv_obj_create(screen);
    lv_obj_set_size(page_picker,800,CONTENT_H);
    lv_obj_set_pos(page_picker,0,0);
    panel_style(page_picker,false);
    lv_obj_set_style_radius(page_picker,0,0);
    lv_obj_set_style_bg_opa(page_picker,LV_OPA_90,0);
    no_scroll(page_picker);
    lv_obj_t *c=lv_obj_create(page_picker);
    lv_obj_set_size(c,460,376);
    panel_style(c,true);lv_obj_center(c);no_scroll(c);
    lv_obj_t *t=label(c,"Toutes les pages",0xF3F6F7,&lv_font_montserrat_16);
    lv_obj_align(t,LV_ALIGN_TOP_MID,0,12);
    for(int j=0;j<6;j++){
        int p=page_order[j];
        lv_obj_t *bt=lv_button_create(c);
        lv_obj_set_size(bt,420,43);
        lv_obj_set_pos(bt,10,38+j*54);
        panel_style(bt,false);
        lv_obj_t *l=label(bt,page_names[p],0xF3F6F7,&lv_font_montserrat_16);
        lv_obj_center(l);
        lv_obj_add_event_cb(bt,nav_click,LV_EVENT_CLICKED,(void *)(intptr_t)p);
    }
    lv_obj_add_flag(page_picker,LV_OBJ_FLAG_HIDDEN);
}
// Visible diagnostic independent from all Jeedom configuration / settings.
// The GT911 counters are sampled by the board driver.  Even when LVGL click
// dispatch is broken, this label identifies whether the hardware saw a press.
static void mira_touch_diagnostic_refresh(void) {
    if(!touch_diag_label)return;
    mira_display_stats_t st;
    mira_display_stats(&st);
    // Do not rerasterize the screen if the hardware counters have not moved.
    char new_line[110];
    static char last_line[110];
    snprintf(new_line,sizeof(new_line),
        "GT911:%s Appuis:%lu X:%lu Y:%lu Err:%lu Stat:%02lX",
        st.touch_ready ? "OK" : "ABSENT",
        (unsigned long)st.touch_presses,
        (unsigned long)st.touch_last_x,
        (unsigned long)st.touch_last_y,
        (unsigned long)st.touch_errors,
        (unsigned long)st.touch_last_status);
    if(strcmp(new_line,last_line)!=0) {
        lv_label_set_text(touch_diag_label,new_line);
        snprintf(last_line,sizeof(last_line),"%s",new_line);
    }
}
static void mira_touch_diagnostic_pressed(lv_event_t *event) {
    (void)event;
    if(touch_diag_button) {
        lv_obj_set_style_bg_color(touch_diag_button,lv_color_hex(0x27A87C),0);
    }
    ESP_LOGI(TAG,"LVGL button PRESS reached Mira UI");
}
void mira_full_ui_create(lv_display_t *disp){
    nvs_handle_t ui_nvs;
    if(nvs_open("mira_idf_ui",NVS_READONLY,&ui_nvs)==ESP_OK){
        uint8_t stored=220;
        if(nvs_get_u8(ui_nvs,"brightness",&stored)==ESP_OK && stored>=30)
            display_brightness_raw=stored;
        uint16_t delay=120;
        if(nvs_get_u16(ui_nvs,"sleep_delay",&delay)==ESP_OK && delay<=3600)
            sleep_timeout_seconds=delay;
        uint8_t light=8;
        if(nvs_get_u8(ui_nvs,"sleep_light",&light)==ESP_OK && light<=100)
            sleep_brightness_percent=light;
        uint8_t clock=1;
        if(nvs_get_u8(ui_nvs,"sleep_clock",&clock)==ESP_OK)
            sleep_clock_enabled=clock!=0;
        nvs_close(ui_nvs);
    }
    ESP_ERROR_CHECK_WITHOUT_ABORT(mira_display_set_brightness(display_brightness_raw));
    screen=lv_obj_create(NULL);
    lv_obj_set_size(screen,800,480);
    lv_obj_set_style_bg_color(screen,bg_c(),0);
    lv_obj_set_style_bg_grad_color(screen,lv_color_hex(0x0A2134),0);
    lv_obj_set_style_bg_grad_dir(screen,LV_GRAD_DIR_VER,0);
    lv_obj_set_style_border_width(screen,0,0);
    lv_obj_set_style_pad_all(screen,0,0);
    no_scroll(screen);
    lv_display_set_default(disp);
    lv_screen_load(screen);
    for(int i=0;i<6;i++){
        pages[i]=lv_obj_create(screen);
        lv_obj_set_size(pages[i],800,CONTENT_H);
        lv_obj_set_pos(pages[i],0,0);
        lv_obj_set_style_bg_color(pages[i],bg_c(),0);
        lv_obj_set_style_bg_grad_color(pages[i],lv_color_hex(0x0A2134),0);
        lv_obj_set_style_bg_grad_dir(pages[i],LV_GRAD_DIR_VER,0);
        lv_obj_set_style_pad_all(pages[i],6,0);
        lv_obj_set_style_pad_top(pages[i],8,0);
        lv_obj_set_style_pad_row(pages[i],6,0);
        lv_obj_set_style_pad_column(pages[i],6,0);
        lv_obj_set_style_border_width(pages[i],0,0);
        if(i==0||i==1||i==2||i==4||i==5){
            make_flex(pages[i],LV_FLEX_FLOW_ROW_WRAP,LV_FLEX_ALIGN_CENTER,LV_FLEX_ALIGN_START);
        } else make_flex(pages[i],LV_FLEX_FLOW_COLUMN,LV_FLEX_ALIGN_START,LV_FLEX_ALIGN_CENTER);
        lv_obj_set_scroll_dir(pages[i],LV_DIR_VER);
        lv_obj_set_scrollbar_mode(pages[i],LV_SCROLLBAR_MODE_OFF);
        if(i)lv_obj_add_flag(pages[i],LV_OBJ_FLAG_HIDDEN);
    }
    nav=lv_obj_create(screen);
    lv_obj_set_pos(nav,0,CONTENT_H);lv_obj_set_size(nav,800,NAV_H);
    lv_obj_set_style_bg_color(nav,lv_color_hex(0x06101B),0);
    lv_obj_set_style_border_color(nav,border_c(),0);
    lv_obj_set_style_border_width(nav,1,0);
    lv_obj_set_style_radius(nav,0,0);
    lv_obj_set_style_pad_all(nav,0,0);
    no_scroll(nav);
    const int fav[4]={0,1,2,6};
    // Original 0.7.37 bar: home, drawn bulb, drawn thermometer, 2x2 pages.
    for(int i=0;i<4;i++){
        lv_obj_t *bt=lv_button_create(nav);
        favorite_btn[i]=bt;
        lv_obj_set_pos(bt,1+i*200,1);
        lv_obj_set_size(bt,200,50);
        lv_obj_set_style_bg_opa(bt,i==0?LV_OPA_70:LV_OPA_TRANSP,0);
        lv_obj_set_style_bg_color(bt,lv_color_hex(0x155A75),0);
        lv_obj_set_style_border_width(bt,0,0);
        lv_obj_set_style_shadow_width(bt,0,0);
        lv_obj_add_event_cb(bt,nav_click,LV_EVENT_CLICKED,(void *)(intptr_t)fav[i]);
        if(i==0) {
            lv_obj_t *l=label(bt,LV_SYMBOL_HOME,0x22BEEA,&lv_font_montserrat_16);
            lv_obj_center(l);
        } else if(i==1) mira_nav_bulb(bt);
        else if(i==2) mira_nav_thermometer(bt);
        else mira_nav_pages(bt);
    }
    settings();
    create_picker();
    build_config();
    sleep_overlay=lv_obj_create(lv_layer_top());
    lv_obj_set_size(sleep_overlay,800,480);
    lv_obj_align(sleep_overlay,LV_ALIGN_CENTER,0,0);
    lv_obj_set_style_bg_color(sleep_overlay,lv_color_hex(0x000000),0);
    lv_obj_set_style_bg_opa(sleep_overlay,LV_OPA_COVER,0);
    lv_obj_set_style_border_width(sleep_overlay,0,0);
    lv_obj_set_style_radius(sleep_overlay,0,0);
    lv_obj_set_style_pad_all(sleep_overlay,0,0);
    no_scroll(sleep_overlay);
    sleep_clock_text=label(sleep_overlay,"--:--",0xffffff,&lv_font_montserrat_48);
    lv_obj_center(sleep_clock_text);
    lv_obj_add_flag(sleep_overlay,LV_OBJ_FLAG_HIDDEN);
    feedback=label(screen,"APERÇU IDF : interactions locales (Jeedom non connecté)",0xA8B2B8,&lv_font_montserrat_12);
    lv_obj_set_pos(feedback,6,CONTENT_H-17);
    lv_obj_add_flag(feedback,LV_OBJ_FLAG_HIDDEN); // avoid covering real UI

    // Diagnostic banner, always visible on the device, including an empty Mira.
    // Does not depend on an imported miralvgl.json or functioning navigation.
    touch_diag_label=label(screen,"GT911: lecture...",0xDCEAF0,&lv_font_montserrat_12);
    lv_obj_set_pos(touch_diag_label,10,4);
    lv_obj_set_width(touch_diag_label,625);
    no_scroll(touch_diag_label);

    // A direct way to test a real LVGL click and reach the Settings preview.
    // This temporary button does not replace Mira's normal 4-icon navigation.
    touch_diag_button=lv_button_create(screen);
    lv_obj_set_size(touch_diag_button,145,38);
    lv_obj_set_pos(touch_diag_button,646,4);
    lv_obj_set_style_bg_color(touch_diag_button,lv_color_hex(0x155A75),0);
    lv_obj_set_style_shadow_width(touch_diag_button,0,0);
    lv_obj_add_event_cb(touch_diag_button,mira_touch_diagnostic_pressed,LV_EVENT_PRESSED,NULL);
    lv_obj_add_event_cb(touch_diag_button,nav_click,LV_EVENT_CLICKED,(void *)(intptr_t)3);
    lv_obj_t *btn_caption=label(touch_diag_button,"Paramètres",0xF3F6F7,&lv_font_montserrat_16);
    lv_obj_center(btn_caption);
    // Keep diagnostics available through /perf and logs without overlaying
    // actual Mira programming. The test button is no longer a UI shortcut.
    lv_obj_add_flag(touch_diag_label,LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(touch_diag_button,LV_OBJ_FLAG_HIDDEN);
    mira_touch_diagnostic_refresh();
    last_clock_ms=lv_tick_get();
    last_user_touch_ms=last_clock_ms;
    mira_display_stats_t startup_touch={0};
    mira_display_stats(&startup_touch);
    last_user_touch_count=startup_touch.touch_presses;
    ESP_LOGI(TAG,"Mira LVGL 9.6 rendering tree ready, source 0.7.37 JSON");
}

bool mira_full_enqueue_state(const char *name,const char *value){
    if(!state_queue||!name||!value||!*name||strlen(name)>=96||strlen(value)>=160)return false;
    mira_state_update_t msg={0};
    snprintf(msg.name,sizeof(msg.name),"%s",name);
    snprintf(msg.value,sizeof(msg.value),"%s",value);
    return xQueueSend(state_queue,&msg,0)==pdTRUE;
}
static void normalize_widget_name(const char *src,char *dst,size_t length){
    size_t j=0;
    for(size_t i=0;src[i]&&j+1<length;i++){
        unsigned char c=(unsigned char)src[i];
        if(c==' '||c=='/'||c=='#'||c=='+')c='_';
        if(c>='A'&&c<='Z')c=(unsigned char)(c+('a'-'A'));
        dst[j++]=(char)c;
    }
    dst[j]=0;
}
static void apply_state_on_lvgl_task(const mira_state_update_t *msg){
    for(uint8_t i=0;i<widget_n;i++){
        mira_widget_t *w=&widgets[i];
        if(!strcmp(w->type,"energy")||!strcmp(w->type,"media")){
            static const char *const energy_fields[]={"solar","home","grid","battery","soc","soc2","soc3"};
            static const char *const media_fields[]={"title","artist","album","state","progress","volume","cover","device","devices"};
            const size_t prefix=strlen(w->name);
            const size_t incoming_len=strlen(msg->name);
            if(incoming_len>prefix+2 && !strncmp(msg->name,w->name,prefix)&&
               msg->name[prefix]=='_'&&msg->name[prefix+1]=='_'){
                const char *field=msg->name+prefix+2;
                const bool media=!strcmp(w->type,"media");
                const int total=media?9:7;
                for(int n=0;n<total;n++){
                    const char *known=media?media_fields[n]:energy_fields[n];
                    if(!strcmp(field,known)&&w->fields[n]){
                        lv_label_set_text(w->fields[n],msg->value);
                        if(media){
                            suppress_events=true;
                            if(n==3){
                                const bool playing=!strcasecmp(msg->value,"playing")||
                                   !strcasecmp(msg->value,"lecture")||
                                   !strcasecmp(msg->value,"play")||
                                   !strcasecmp(msg->value,"en lecture")||
                                   !strcasecmp(msg->value,"on")||
                                   !strcmp(msg->value,"1")||
                                   !strcasecmp(msg->value,"true");
                                const bool paused=!strcasecmp(msg->value,"paused")||
                                   !strcasecmp(msg->value,"pause")||
                                   !strcasecmp(msg->value,"en pause")||
                                   !strcasecmp(msg->value,"stopped")||
                                   !strcasecmp(msg->value,"arrêt")||
                                   !strcasecmp(msg->value,"off")||
                                   !strcmp(msg->value,"0")||
                                   !strcasecmp(msg->value,"false");
                                if(playing||paused){
                                    w->media_is_playing=playing;
                                    if(w->media_play_caption)
                                        lv_label_set_text(w->media_play_caption,playing?"Pause":"Lecture");
                                }
                            }else if(n==7) {
                                snprintf(w->media_active_device,sizeof(w->media_active_device),"%.95s",msg->value);
                                media_select_active_device(w);
                            }else if(n==5&&w->media_slider) {
                                const int value=clamp(atoi(msg->value),0,100);
                                lv_slider_set_value(w->media_slider,value,LV_ANIM_OFF);
                            }else if(n==8&&w->media_device_dropdown) {
                                char options[240];
                                snprintf(options,sizeof(options),"Sélectionner un appareil\n%.159s",msg->value);
                                for(char *p=options;*p;p++)if(*p=='|'||*p==';')*p='\n';
                                lv_dropdown_set_options(w->media_device_dropdown,options);
                                lv_dropdown_set_selected(w->media_device_dropdown,0);
                                media_select_active_device(w);
                                lv_label_set_text(w->fields[n],"Liste actualisée");
                            }
                            suppress_events=false;
                        }
                        return; // Passive infos never invoke UI callbacks.
                    }
                }
            }
        }
        char norm_name[96],norm_recv[96];
        normalize_widget_name(w->name,norm_name,sizeof(norm_name));
        normalize_widget_name(msg->name,norm_recv,sizeof(norm_recv));
        bool matches=strcmp(w->name,msg->name)==0||strcmp(norm_name,norm_recv)==0;
        if(!matches&&!strcmp(w->type,"thermostat")){
            char setpoint_name[128],setpoint_normalized[128];
            snprintf(setpoint_name,sizeof(setpoint_name),"%.95s__consigne",w->name);
            normalize_widget_name(setpoint_name,setpoint_normalized,sizeof(setpoint_normalized));
            matches=!strcmp(setpoint_name,msg->name)||!strcmp(setpoint_normalized,norm_recv);
        }
        if(!matches)continue;
        // Remote information is authoritative, but must never trigger an
        // outgoing command. Color states accept legacy #RRGGBB / r,g,b.
        if(w->control && !strcmp(w->type,"color")){
            unsigned r=0,g=0,b=0;
            bool valid=false;
            if(msg->value[0]=='#' && strlen(msg->value)==7){
                unsigned rgb=0;
                // Require six valid hexadecimal characters, not a partial
                // sscanf conversion of malformed colour state.
                bool hex=true;
                for(int k=1;k<7;k++){
                    char ch=msg->value[k];
                    if(!((ch>='0'&&ch<='9')||(ch>='a'&&ch<='f')||
                         (ch>='A'&&ch<='F'))){hex=false;break;}
                }
                if(hex&&sscanf(msg->value+1,"%6x",&rgb)==1){
                    r=(rgb>>16)&255u;g=(rgb>>8)&255u;b=rgb&255u;
                    valid=true;
                }
            }else{
                char trailing=0;
                if(sscanf(msg->value,"%u,%u,%u %c",&r,&g,&b,&trailing)==3 &&
                   r<=255u && g<=255u && b<=255u)valid=true;
            }
            if(valid){
                const lv_color_hsv_t hsv=lv_color_rgb_to_hsv((uint8_t)r,(uint8_t)g,(uint8_t)b);
                suppress_events=true;
                lv_arc_set_value(w->control,hsv.h);
                suppress_events=false;
            }
        }
        snprintf(w->value,sizeof(w->value),"%s",msg->value);
        suppress_events=true;
        if(w->control && !strcmp(w->type,"toggle")){
            bool on=!strcasecmp(msg->value,"on")||!strcasecmp(msg->value,"true")||
                    !strcmp(msg->value,"1");
            if(on)lv_obj_add_state(w->control,LV_STATE_CHECKED);
            else lv_obj_remove_state(w->control,LV_STATE_CHECKED);
        }else if(w->control && (!strcmp(w->type,"range")||!strcmp(w->type,"roundrange")||
                                  !strcmp(w->type,"thermostat"))){
            const int value=clamp(atoi(msg->value),w->minv,w->maxv);
            if(!strcmp(w->type,"range"))lv_slider_set_value(w->control,value,LV_ANIM_OFF);
            else lv_arc_set_value(w->control,value);
        }else if(w->control && (!strcmp(w->type,"list")||!strcmp(w->type,"listbutton"))){
            const char *options=lv_dropdown_get_options(w->control);
            if(options){
                uint16_t index=0;
                const char *start=options;
                while(*start){
                    const char *end=strchr(start,'\n');
                    size_t length=end?(size_t)(end-start):strlen(start);
                    if(strlen(msg->value)==length&&!strncmp(start,msg->value,length)){
                        lv_dropdown_set_selected(w->control,index);
                        break;
                    }
                    if(!end)break;
                    start=end+1;
                    index++;
                }
            }
        }
        if(w->info_status)
            lv_label_set_text(w->info_status,"Donnée reçue");
        if(w->value_label){
            char display[200];
            if(*w->unit)snprintf(display,sizeof(display),"%s %s",msg->value,w->unit);
            else snprintf(display,sizeof(display),"%s",msg->value);
            lv_label_set_text(w->value_label,display);
        }
        suppress_events=false;
        // One Jeedom state may be represented on several Mira pages.
        // Update all matching widgets; passive updates never send actions.
        continue;
    }
}
static void apply_pending_states(void){
    if(!state_queue)return;
    mira_state_update_t update;
    for(int n=0;n<12&&xQueueReceive(state_queue,&update,0)==pdTRUE;n++)
        apply_state_on_lvgl_task(&update);
}

void mira_full_ui_tick(void){
    if(reload_pending){reload_pending=false;camera_close();build_config();}
    apply_pending_states();
    static uint32_t diag_last_tick;
    uint32_t now_tick=lv_tick_get();
    camera_poll_on_lvgl_task(now_tick);
    // Original Mira's 60-second auto return: use verified GT911 physical
    // press counter, not LVGL redraw/Info traffic (which is not user activity).
    mira_display_stats_t last_touch={0};
    mira_display_stats(&last_touch);
    if(last_touch.touch_presses!=last_user_touch_count){
        last_user_touch_count=last_touch.touch_presses;
        last_user_touch_ms=now_tick;
        if(display_sleeping)display_wake();
    }else if(!display_sleeping && now_tick-last_user_touch_ms>=60000u && current_page!=0){
        show_page(0);
        last_user_touch_ms=now_tick;
    }
    if(!display_sleeping && sleep_timeout_seconds>0 &&
       now_tick-last_user_touch_ms>=(uint32_t)sleep_timeout_seconds*1000u)
        display_enter_sleep();
    if(now_tick-diag_last_tick>=200){
        diag_last_tick=now_tick;
        mira_touch_diagnostic_refresh();
    }
    uint32_t ms=lv_tick_get();
    if(ms-last_clock_ms>2000){
        last_clock_ms=ms;
        time_t now=time(NULL);
        if(now>1700000000){
            struct tm tmv;localtime_r(&now,&tmv);
            char buff[24];snprintf(buff,sizeof(buff),"%02d:%02d",tmv.tm_hour,tmv.tm_min);
            if(clock_text)lv_label_set_text(clock_text,buff);
            if(date_text){
                char datebuf[32];
                snprintf(datebuf,sizeof(datebuf),"%02d/%02d/%04d",
                         tmv.tm_mday,tmv.tm_mon+1,tmv.tm_year+1900);
                lv_label_set_text(date_text,datebuf);
            }
            if(display_sleeping&&sleep_clock_enabled&&sleep_clock_text)
                lv_label_set_text(sleep_clock_text,buff);
        }
    }
}
bool mira_full_storage_init(void){
    if(!state_queue)state_queue=xQueueCreate(24,sizeof(mira_state_update_t));
    if(!state_queue)ESP_LOGE(TAG,"State queue allocation failed");
    const esp_vfs_spiffs_conf_t cfg={
        .base_path="/spiffs",.partition_label="spiffs",
        .max_files=4,.format_if_mount_failed=false
    };
    esp_err_t err=esp_vfs_spiffs_register(&cfg);
    spiffs_ready=(err==ESP_OK);
    if(!spiffs_ready)ESP_LOGW(TAG,"SPIFFS read failed (%s), no format, import disabled",esp_err_to_name(err));
    else ESP_LOGI(TAG,"SPIFFS mounted non-destructively; original 0.7.37 JSON is preserved");
    return spiffs_ready;
}
static esp_err_t page_get(httpd_req_t *req){
    const char *html=
        "<!doctype html><html lang='fr'><meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>Mira LVGL 9.6 - Import JSON</title><h2>Mira LVGL 9.6 / Waveshare 4.3C</h2>"
        "<p>Rendu experimental des widgets reels depuis <code>/miralvgl.json</code> (firmware 0.7.37).</p>"
        "<p>Transport MQTT natif disponible apres configuration. API HTTP Jeedom historique, camera et widgets composites en cours de portage.</p>"
        "<p><b>La partition SPIFFS n'est jamais formatée automatiquement.</b></p>"
        "<p>Charge uniquement un export JSON Mira contenant le tableau <code>lvgl</code>.</p>"
        "<h3>Jeedom - cle de l'ecran deja configuree dans le plugin</h3>"
        "<input id='jkey' type='password' placeholder='Cle Jeedom existante'>"
        "<button onclick='configureJeedom()'>Enregistrer la cle Jeedom</button>"
        "<button onclick='checkJeedom()'>Verifier la liaison Jeedom</button><pre id='jresult'></pre>"
        "<h3>Wi-Fi IDF (point d'acces de secours conserve)</h3>"
        "<input id='ssid' placeholder='SSID Wi-Fi'><input id='wpass' type='password' placeholder='Mot de passe Wi-Fi'>"
        "<button onclick='configureWifi()'>Connecter au Wi-Fi</button><pre id='netresult'></pre>"
        "<h3>MQTT ESP-IDF (optionnel)</h3><p>Topics identiques: mira/panel/States et Actions</p>"
        "<input id='mhost' placeholder='Broker IP'><input id='mport' type='number' value='1883'>"
        "<input id='mname' value='panel' placeholder='Nom de l ecran'>"
        "<input id='muser' placeholder='Utilisateur MQTT'><input id='mpass' type='password' placeholder='Mot de passe MQTT'>"
        "<label><input id='menabled' type='checkbox'>Activer MQTT</label>"
        "<button onclick='configureMqtt()'>Sauver MQTT</button><pre id='mqttresult'></pre>"
        "<input id='file' type='file' accept='.json,application/json'>"
        "<button onclick='send()'>Importer dans SPIFFS</button><pre id='out'></pre>"
        "<p><a href='/perf'>Mesures FPS</a> | <a href='/'>OTA et récupération</a></p>"
        "<script>async function send(){let f=document.getElementById('file').files[0];"
        "if(!f)return;let r=await fetch('/mira/import',{method:'POST',headers:{'Content-Type':'application/json'},body:f});"
        "document.getElementById(\'out\').textContent=await r.text();}"
        "async function configureJeedom(){let r=await fetch('/mira/jeedom/key',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({key:document.getElementById('jkey').value})});document.getElementById('jresult').textContent=await r.text();}"
        "async function checkJeedom(){let k=encodeURIComponent(document.getElementById('jkey').value);let r=await fetch('/mira/jeedom/status?key='+k);document.getElementById('jresult').textContent=await r.text();}"
        "async function configureWifi(){let r=await fetch('/mira/wifi',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({ssid:document.getElementById('ssid').value,password:document.getElementById('wpass').value})});document.getElementById('netresult').textContent=await r.text();}"
        "async function configureMqtt(){let r=await fetch('/mira/mqtt',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({host:document.getElementById('mhost').value,port:Number(document.getElementById('mport').value),screen:document.getElementById('mname').value,username:document.getElementById('muser').value,password:document.getElementById('mpass').value,enabled:document.getElementById('menabled').checked})});document.getElementById('mqttresult').textContent=await r.text();}</script></html>";
    httpd_resp_set_type(req,"text/html; charset=utf-8");
    return httpd_resp_send(req,html,HTTPD_RESP_USE_STRLEN);
}
static esp_err_t import_post(httpd_req_t *req){
    if(!mira_setup_require_ap(req))return ESP_FAIL;
    if(!spiffs_ready)return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"SPIFFS absent; sauvegarder avant correction"),ESP_FAIL;
    size_t len=req->content_len;
    if(len<2||len>JSON_MAX)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"JSON > 48 KiB ou vide"),ESP_FAIL;
    char *buf=malloc(len+1);
    if(!buf)return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"RAM insuffisante"),ESP_FAIL;
    size_t received=0;
    while(received<len){
        int r=httpd_req_recv(req,buf+received,len-received);
        if(r<=0){free(buf);return httpd_resp_send_err(req,HTTPD_408_REQ_TIMEOUT,"Import incomplet"),ESP_FAIL;}
        received+=(size_t)r;
    }
    buf[len]=0;
    cJSON *root=cJSON_ParseWithLength(buf,len);
    bool valid=root&&cJSON_IsArray(cJSON_GetObjectItemCaseSensitive(root,"lvgl"));
    if(root)cJSON_Delete(root);
    if(!valid){free(buf);return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Racine lvgl absente"),ESP_FAIL;}
    const char *tmp="/spiffs/miralvgl.new";
    FILE *out=fopen(tmp,"wb");
    if(!out){free(buf);return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"SPIFFS écriture impossible"),ESP_FAIL;}
    size_t written=fwrite(buf,1,len,out);
    fclose(out);free(buf);
    if(written!=len||rename(tmp,JSON_PATH)!=0){
        remove(tmp);
        return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Erreur écriture JSON"),ESP_FAIL;
    }
    reload_pending=true;
    ESP_LOGW(TAG,"Mira JSON importé (%u octets), rafraîchissement LVGL demandé",(unsigned)len);
    httpd_resp_set_type(req,"text/plain; charset=utf-8");
    return httpd_resp_sendstr(req,"Mira JSON importé. Pages mises à jour sur l'écran.");
}

/* HTTP-safe input, e.g. POST /mira/state {"name":"Salon","value":"22.5"}.
   Never invoke LVGL in the HTTP server task and never send a Jeedom action. */
static esp_err_t state_post(httpd_req_t *req){
    if(!state_queue){httpd_resp_set_status(req,"503 Service Unavailable");httpd_resp_sendstr(req,"State queue unavailable");return ESP_FAIL;}
    if(req->content_len<2||req->content_len>512){
        httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"JSON state payload must be 2..512 bytes");
        return ESP_FAIL;
    }
    char buf[513];size_t got=0,len=req->content_len;
    while(got<len){
        int n=httpd_req_recv(req,buf+got,len-got);
        if(n<=0){httpd_resp_send_err(req,HTTPD_408_REQ_TIMEOUT,"Incomplete state payload");return ESP_FAIL;}
        got+=(size_t)n;
    }
    buf[len]=0;
    cJSON *json=cJSON_ParseWithLength(buf,len);
    const cJSON *name=json?cJSON_GetObjectItemCaseSensitive(json,"name"):NULL;
    const cJSON *value=json?cJSON_GetObjectItemCaseSensitive(json,"value"):NULL;
    const char *val=cJSON_IsString(value)?value->valuestring:NULL;
    char scalar[48];
    if(cJSON_IsNumber(value)){
        snprintf(scalar,sizeof(scalar),"%.9g",value->valuedouble);
        val=scalar;
    }else if(cJSON_IsBool(value)){
        val=cJSON_IsTrue(value)?"1":"0";
    }
    if(!cJSON_IsString(name)||!name->valuestring||!*name->valuestring||
       strlen(name->valuestring)>=96||!val||strlen(val)>=160){
        if(json)cJSON_Delete(json);
        httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Expected name (1..95) and value (max 159)");
        return ESP_FAIL;
    }
    mira_state_update_t update={0};
    snprintf(update.name,sizeof(update.name),"%s",name->valuestring);
    snprintf(update.value,sizeof(update.value),"%s",val);
    cJSON_Delete(json);
    if(xQueueSend(state_queue,&update,0)!=pdTRUE){
        httpd_resp_set_status(req,"503 Service Unavailable");
        return httpd_resp_sendstr(req,"State queue full");
    }
    httpd_resp_set_type(req,"application/json");
    return httpd_resp_sendstr(req,"{\"queued\":true,\"action\":false}");
}

void mira_full_register_http(httpd_handle_t server){
    httpd_uri_t a={.uri="/mira",.method=HTTP_GET,.handler=page_get};
    httpd_uri_t b={.uri="/mira/import",.method=HTTP_POST,.handler=import_post};
    httpd_uri_t c={.uri="/mira/state",.method=HTTP_POST,.handler=state_post};
    ESP_ERROR_CHECK_WITHOUT_ABORT(httpd_register_uri_handler(server,&a));
    ESP_ERROR_CHECK_WITHOUT_ABORT(httpd_register_uri_handler(server,&b));
    ESP_ERROR_CHECK_WITHOUT_ABORT(httpd_register_uri_handler(server,&c));
}
uint32_t mira_full_widget_count(void){return widget_count;}
