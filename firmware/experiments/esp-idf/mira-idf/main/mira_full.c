// Mira Panel LVGL 9.6 native ESP-IDF screen-port prototype for Waveshare 4.3C.
// Renders the actual /miralvgl.json programming from the 0.7.37 SPIFFS partition.
// NOT a drop-in replacement for the old Jeedom HTTP/MQTT backend yet.
// All LVGL access is kept on the LVGL task. HTTP may only set reload_pending.
#include "mira_full.h"
#include "mira_french_font.h"
#include "mira_info_glyphs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <math.h>
#include <errno.h>
#include <ctype.h>
#include "esp_log.h"
#include "esp_system.h"
#include "esp_spiffs.h"
#include "esp_partition.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_wifi.h"
#include "cJSON.h"
#include "nvs.h"
#include "lvgl.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "mira_display.h"
#include "mira_gpio43.h"
#include "mira_mqtt.h"
#include "mira_jeedom.h"
#include "mira_camera.h"
#include "mira_album_art.h"
#include "mira_setup_access.h"
#include "mira_beta_setup.h"
#include "mira_beta_web.h"

#define MIRA_WIDTH 800
#define MIRA_HEIGHT 480
#define NAV_H 50
#define CONTENT_H (MIRA_HEIGHT - NAV_H)
#define MAX_WIDGETS 40 // beta MiraPanelUI::MAX_BINDINGS
#define JSON_MAX 49152
static const char *TAG="mira-full-43";
static const char *JSON_PATH="/spiffs/miralvgl.json";
static volatile bool reload_pending=false;
static volatile bool settings_reload_pending=false;
static volatile bool theme_reload_pending=false;
void mira_full_request_settings_reload(void){settings_reload_pending=true;}
void mira_full_request_reload(void){ reload_pending=true; }
// Strict one-way UI state ingestion. Only the LVGL task can touch the widget tree.
// No state update ever generates a Jeedom/MQTT command.
typedef struct { char name[96]; char value[160]; } mira_state_update_t;
static QueueHandle_t state_queue;
// Album artwork URLs are longer than ordinary Jeedom Infos (159 bytes).
// Keep them OUT of the 24-entry general state queue to protect internal RAM.
typedef struct {char name[96];char url[769];} mira_cover_update_t;
static QueueHandle_t cover_update_queue;
// Incoming values are tracked separately for the beta HTTP /get endpoint.
// An absent entry is unknown, never treated as OFF or zero.
#define MIRA_STATE_CACHE_LIMIT 96
static mira_state_update_t *state_cache;
static size_t state_cache_count;
// Monotonic counters: do not allocate additional heap in diagnostics.
static uint32_t passive_unchanged,passive_evictions,passive_queue_dropped;
static SemaphoreHandle_t state_cache_lock;
typedef struct { char key[32]; char value[48]; } mira_display_command_t;
static QueueHandle_t display_command_queue;
static bool suppress_events;
static uint8_t display_brightness_raw=220;
static uint8_t day_brightness_percent=85,night_brightness_percent=20;
static bool night_auto_enabled;
static bool manual_brightness_override;
static bool last_night_valid,last_night_period;
/* Arduino MiraPanelUI.h defaults to 23:00–07:00 even before
 * DISPLAY_DebutNuit / DISPLAY_FinNuit are created in params.json.
 */
static int night_start_minute=23*60,night_end_minute=7*60;
static uint8_t applied_brightness_raw;

static uint32_t sleep_timeout_seconds=120;
static bool return_home_enabled=true;
static uint8_t sleep_brightness_percent=8;
static bool sleep_clock_enabled=true,display_sleeping;
static lv_obj_t *sleep_overlay,*sleep_clock_text;
static bool spiffs_ready=false;
static lv_obj_t *screen, *pages[6], *nav, *page_picker;
static lv_obj_t *favorite_btn[4],*picker_buttons[6],*picker_card;
static uint8_t favorite_pages[3]={0,1,2};
static uint8_t visible_page_mask=0x3f;
static uint32_t last_favorite_poll_ms;
static uint8_t current_page=0;
// Preserve firmware-beta's enum: 0=Mira, 1=Sombre, 2=Glass.
static uint8_t ui_theme=0;
static const char *page_names[6]={"Accueil","Lumières","Températures","Paramètres","Énergie","Multimédia"};
static const int page_order[6]={0,1,2,4,5,3};
static lv_obj_t *clock_text,*date_text,*clock_status_text;
static uint32_t widget_count;
static uint32_t last_clock_ms;
static uint32_t last_user_touch_ms,last_user_touch_count;
static volatile uint32_t nav_count,nav_last_heap,nav_lowest_heap;
void mira_full_navigation_stats(uint32_t *count,uint32_t *last_heap,uint32_t *min_heap){
    if(count)*count=nav_count;
    if(last_heap)*last_heap=nav_last_heap;
    if(min_heap)*min_heap=nav_lowest_heap;
}

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
    lv_obj_t *info_icon,*info_name;
    lv_obj_t *thermostat_temperature,*thermostat_mode,*thermostat_mode_select;
    lv_obj_t *color_saturation,*color_brightness,*color_preview;
    lv_obj_t *color_knob;
    uint16_t color_hue;
    uint8_t color_sat,color_val;
    lv_obj_t *fields[9]; // energy/media passive Jeedom composite infos
    lv_obj_t *energy_gauge[3],*energy_extra_names[2];
    bool energy_extra_active[2];
    lv_obj_t *media_slider,*media_device_dropdown,*media_play_caption;
    lv_obj_t *media_artwork_image,*media_artwork_placeholder;
    bool media_is_playing;
    // Distinguish Mira beta simple, double, triple and long-click commands.
    uint8_t multi_clicks;
    uint32_t multi_deadline;
    bool multi_long_fired;
    // Original beta "listbutton" is a set of tactile options, not a dropdown.
    char list_text[160];
    uint8_t list_offsets[12],list_count,list_selected;
    lv_obj_t *list_buttons[12];
    char media_active_device[96];
    int minv,maxv;
    /* Live drag: bound network traffic and avoid duplicate release values. */
    int live_last_value;
    uint32_t live_last_tick;
    bool live_sent;
} mira_widget_t;
static mira_widget_t widgets[MAX_WIDGETS];
static uint8_t widget_n=0;
static lv_color_t text_c(void){return lv_color_hex(0xF3F6F7);}
static lv_color_t muted_c(void){return lv_color_hex(0xA8B2B8);}
// Exact palette of MiraPanelUI.cpp 0.7.37, not an invented IDF skin.
static lv_color_t cyan_c(void){return lv_color_hex(ui_theme==2?0x5BDEFF:ui_theme==1?0x31BDF2:0x2ABEFF);}
static lv_color_t panel_c(void){return lv_color_hex(ui_theme==2?0x103047:ui_theme==1?0x141B22:0x0E2032);}
static lv_color_t panel2_c(void){return lv_color_hex(ui_theme==2?0x17445D:ui_theme==1?0x202A33:0x173149);}
static lv_color_t border_c(void){return lv_color_hex(ui_theme==2?0x2B6882:ui_theme==1?0x33414C:0x17354B);}
static lv_color_t bg_c(void){return lv_color_hex(ui_theme==2?0x071A28:ui_theme==1?0x090D12:0x071421);}
static lv_color_t nav_c(void){return lv_color_hex(ui_theme==2?0x06131F:ui_theme==1?0x070B0F:0x06101B);}

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
    lv_obj_set_style_text_color(obj,color==0x22BEEA?cyan_c():lv_color_hex(color),0);
    if(font)lv_obj_set_style_text_font(obj,font,0);
}
static lv_obj_t *label(lv_obj_t *parent,const char *s,uint32_t color,const lv_font_t *font){
    lv_obj_t *l=lv_label_create(parent);
    lv_label_set_text(l,s?s:"");
    text_style(l,color,font);
    return l;
}
// Same principle as firmware-beta miraFitInfoValue(): measure the ACTUAL
// rendered UTF-8 text, including its units, on every change. A fixed font
// chosen from the initial value will overflow when Jeedom sends longer data.
// Dot clipping is a last resort, never an arbitrary constant font cut-off.
static void mira_fit_info_value(lv_obj_t *text,lv_obj_t *card){
    if(!text||!card)return;
    const char *value=lv_label_get_text(text);
    if(!value)return;
    lv_obj_update_layout(card);
    int32_t room=lv_obj_get_content_width(card)-8;
    if(room<=8)return;
    static const lv_font_t *const fonts[]={
        &lv_font_montserrat_28,&lv_font_montserrat_20,
        &mira_font_fr_16,&lv_font_montserrat_14,&mira_font_fr_12
    };
    const int height=lv_obj_get_height(card);
    const size_t start=(height<=40)?3u:((height>=68&&room>=180)?0u:1u);
    const lv_font_t *chosen=fonts[4];
    bool fits=false;
    lv_point_t measured={0};
    for(size_t i=start;i<sizeof(fonts)/sizeof(fonts[0]);i++){
        lv_text_get_size(&measured,value,fonts[i],
                         lv_obj_get_style_text_letter_space(text,0),0,
                         LV_COORD_MAX,LV_TEXT_FLAG_NONE);
        chosen=fonts[i];
        if(measured.x<=room){fits=true;break;}
    }
    // Do not repeatedly invalidate the object if the value did not change.
    if(lv_obj_get_style_text_font(text,0)!=chosen)
        lv_obj_set_style_text_font(text,chosen,0);
    lv_label_set_long_mode(text,fits?LV_LABEL_LONG_CLIP:LV_LABEL_LONG_DOT);
    lv_obj_set_width(text,room);
    lv_obj_set_height(text,lv_font_get_line_height(chosen)+5);
}
// Beta's INFO glyph+value group is centered as a unit, with the font fitted
// against the REAL UTF-8 glyph width after every Jeedom/MQTT state change.
static void mira_fit_icon_info_value(mira_widget_t *w){
    if(!w||!w->card||!w->info_icon||!w->value_label)return;
    lv_obj_update_layout(w->card);
    int inner=lv_obj_get_content_width(w->card);
    int icon_w=lv_obj_get_width(w->info_icon);
    int icon_h=lv_obj_get_height(w->info_icon);
    int gap=inner>=180?10:6,margin=inner>=180?8:4;
    int room=inner-2*margin-icon_w-gap-6;
    if(room<8)return;
    const char *text=lv_label_get_text(w->value_label);
    if(!text)return;
    const lv_font_t *fonts[]={
        &lv_font_montserrat_28,&lv_font_montserrat_20,
        &mira_font_fr_16,&mira_font_fr_12
    };
    const lv_font_t *chosen=fonts[3];
    int text_width=0;
    bool fits=false;
    for(size_t i=(inner>=200&&lv_obj_get_height(w->card)>105)?0u:1u;i<4;i++){
        lv_point_t size={0};
        lv_text_get_size(&size,text,fonts[i],
            lv_obj_get_style_text_letter_space(w->value_label,0),
            0,LV_COORD_MAX,LV_TEXT_FLAG_NONE);
        chosen=fonts[i];text_width=size.x;
        if(text_width<=room){fits=true;break;}
    }
    int draw_width=fits?text_width:room;
    int x=margin+(inner-2*margin-icon_w-gap-draw_width)/2;
    int y=lv_obj_get_y(w->info_icon);
    lv_obj_set_pos(w->info_icon,x,y);
    lv_obj_add_flag(w->value_label,LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_align(w->value_label,LV_ALIGN_TOP_LEFT);
    lv_obj_set_style_text_font(w->value_label,chosen,0);
    lv_obj_set_style_text_align(w->value_label,LV_TEXT_ALIGN_LEFT,0);
    lv_label_set_long_mode(w->value_label,fits?LV_LABEL_LONG_CLIP:LV_LABEL_LONG_DOT);
    lv_obj_set_size(w->value_label,draw_width+6,lv_font_get_line_height(chosen)+4);
    lv_obj_set_pos(w->value_label,x+icon_w+gap,
        y+(icon_h-lv_font_get_line_height(chosen))/2);
}
static void mira_decorate_info(mira_widget_t *w,const char *requested,
                               const char *mode,const char *layout){
    if(!w||!w->card||!w->value_label)return;
    if(mode&&!strcmp(mode,"text"))return;
    const char *kind=mira_info_glyph_kind(requested,w->name,w->unit);
    if(!kind)return; // Unknown icons preserve their original name/value view.
    lv_obj_update_layout(w->card);
    int width=lv_obj_get_width(w->card),height=lv_obj_get_height(w->card);
    if(width<90||height<35)return;
    bool icon_only=mode&&!strcmp(mode,"icon");
    if(icon_only&&(!layout||strcmp(layout,"free"))&&height>=115){
        height=100;
        lv_obj_set_height(w->card,height);
    }
    int pad=width>=220?16:8;
    int side=width/5, max_side=icon_only?height*10/17:height/2;
    if(side>max_side)side=max_side;
    if(side>70)side=70;
    if(side<18)side=18;
    int y=(height-side)/2;
    if(!icon_only&&height>90)y+=8;
    w->info_icon=mira_info_glyph_create(w->card,kind,side,cyan_c());
    if(!w->info_icon)return;
    lv_obj_set_pos(w->info_icon,pad,y);
    if(w->info_name){
        if(icon_only)lv_obj_add_flag(w->info_name,LV_OBJ_FLAG_HIDDEN);
        else{
            lv_obj_add_flag(w->info_name,LV_OBJ_FLAG_IGNORE_LAYOUT);
            lv_obj_set_align(w->info_name,LV_ALIGN_TOP_LEFT);
            lv_obj_set_width(w->info_name,width-2*pad);
            lv_obj_set_style_text_align(w->info_name,LV_TEXT_ALIGN_CENTER,0);
            lv_obj_set_style_text_font(w->info_name,
                  height>105?&mira_font_fr_16:&mira_font_fr_12,0);
            lv_obj_set_pos(w->info_name,pad,height>105?10:3);
        }
    }
    if(w->info_status){
        lv_obj_add_flag(w->info_status,LV_OBJ_FLAG_IGNORE_LAYOUT);
        lv_obj_set_align(w->info_status,LV_ALIGN_TOP_LEFT);
        lv_obj_set_width(w->info_status,width-2*pad);
        lv_obj_set_style_text_align(w->info_status,LV_TEXT_ALIGN_CENTER,0);
        lv_obj_set_pos(w->info_status,pad,height-21);
    }
    mira_fit_icon_info_value(w);
}
static void no_scroll(lv_obj_t *obj){lv_obj_remove_flag(obj,LV_OBJ_FLAG_SCROLLABLE);}
static void panel_style(lv_obj_t *obj,bool light){
    lv_obj_set_style_bg_color(obj,light?panel2_c():panel_c(),0);
    lv_obj_set_style_bg_opa(obj,ui_theme==2?LV_OPA_80:LV_OPA_COVER,0);
    lv_obj_set_style_border_color(obj,border_c(),0);
    lv_obj_set_style_border_width(obj,1,0);
    lv_obj_set_style_border_opa(obj,ui_theme==0?LV_OPA_50:LV_OPA_70,0);
    lv_obj_set_style_radius(obj,ui_theme==2?16:14,0);
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
    if(idx<0||idx>5||!(visible_page_mask&(1u<<idx)))return;
    current_page=(uint8_t)idx;
    nav_count++;
    nav_last_heap=esp_get_free_heap_size();
    if(!nav_lowest_heap||nav_last_heap<nav_lowest_heap)nav_lowest_heap=nav_last_heap;
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
    }else if(index>=7&&index<=9){
        const uint8_t page=favorite_pages[index-7];
        if(page<6)show_page(page);
    }else show_page(index);
}
static void remember_passive_state(const mira_state_update_t *msg);
// LVGL 9 removed the LVGL 8 colorwheel: recreate the ORIGINAL 150px
// hue ring as a cached RGB565 bitmap. Hue uses a real circular touch target;
// saturation/value sliders preserve the complete RGB gamut. One bitmap is
// shared by every card and rebuilt only when the theme palette changes.
#define MIRA_WHEEL_SIZE 150
#define MIRA_WHEEL_CENTER (MIRA_WHEEL_SIZE/2)
static uint16_t *wheel_pixels;
static lv_image_dsc_t wheel_dsc;
static uint8_t wheel_bitmap_theme=255;
static uint16_t wheel_rgb565(uint32_t c){
    return (uint16_t)(((c>>19)&31u)<<11|((c>>10)&63u)<<5|((c>>3)&31u));
}
static bool wheel_prepare_bitmap(void){
    if(wheel_pixels&&wheel_bitmap_theme==ui_theme)return true;
    if(!wheel_pixels){
        wheel_pixels=heap_caps_malloc(MIRA_WHEEL_SIZE*MIRA_WHEEL_SIZE*2u,
                                      MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        if(!wheel_pixels)return false;
    }
    uint32_t bg=lv_color_to_u32(panel_c())&0xffffffu;
    const uint16_t fill=wheel_rgb565(bg);
    for(int y=0;y<MIRA_WHEEL_SIZE;y++){
        for(int x=0;x<MIRA_WHEEL_SIZE;x++){
            float dx=(float)(x-MIRA_WHEEL_CENTER)+0.5f;
            float dy=(float)(y-MIRA_WHEEL_CENTER)+0.5f;
            float distance2=dx*dx+dy*dy;
            uint16_t pixel=fill;
            if(distance2>=58.0f*58.0f&&distance2<=74.0f*74.0f){
                float angle=atan2f(dx,-dy);
                if(angle<0)angle+=6.2831853f;
                int hue=(int)lroundf(angle*(180.0f/3.14159265f))%360;
                const lv_color_t color=lv_color_hsv_to_rgb(hue,100,100);
                pixel=wheel_rgb565(lv_color_to_u32(color)&0xffffffu);
            }
            wheel_pixels[y*MIRA_WHEEL_SIZE+x]=pixel;
        }
    }
    memset(&wheel_dsc,0,sizeof(wheel_dsc));
    wheel_dsc.header.magic=LV_IMAGE_HEADER_MAGIC;
    wheel_dsc.header.cf=LV_COLOR_FORMAT_RGB565;
    wheel_dsc.header.w=MIRA_WHEEL_SIZE;
    wheel_dsc.header.h=MIRA_WHEEL_SIZE;
    wheel_dsc.data_size=MIRA_WHEEL_SIZE*MIRA_WHEEL_SIZE*2u;
    wheel_dsc.data=(const uint8_t *)wheel_pixels;
    wheel_bitmap_theme=ui_theme;
    return true;
}
static void wheel_mark_hue(mira_widget_t *w,int hue){
    if(!w||!w->control)return;
    if(hue<0)hue=0;
    w->color_hue=(uint16_t)(hue%360);
    if(w->color_knob){
        const float angle=(float)w->color_hue*(3.14159265f/180.0f);
        const int x=MIRA_WHEEL_CENTER+(int)lroundf(66.0f*sinf(angle))-7;
        const int y=MIRA_WHEEL_CENTER-(int)lroundf(66.0f*cosf(angle))-7;
        lv_obj_set_pos(w->color_knob,x,y);
        lv_obj_set_style_bg_color(w->color_knob,
            lv_color_hsv_to_rgb(w->color_hue,100,100),0);
    }
}
static void event_local(lv_event_t *e);
static void wheel_touch(lv_event_t *event){
    const lv_event_code_t code=lv_event_get_code(event);
    if(code!=LV_EVENT_PRESSED&&code!=LV_EVENT_PRESSING)return;
    if(suppress_events||display_sleeping)return;
    mira_widget_t *w=(mira_widget_t *)lv_event_get_user_data(event);
    if(!w||!w->control)return;
    lv_indev_t *touch=lv_indev_get_act();
    if(!touch)return;
    lv_point_t p={0};
    lv_indev_get_point(touch,&p);
    lv_area_t area;
    lv_obj_get_coords(w->control,&area);
    const float dx=(float)p.x-(float)(area.x1+MIRA_WHEEL_CENTER);
    const float dy=(float)p.y-(float)(area.y1+MIRA_WHEEL_CENTER);
    const float r2=dx*dx+dy*dy;
    if(r2<44.0f*44.0f||r2>84.0f*84.0f)return;
    float angle=atan2f(dx,-dy);
    if(angle<0)angle+=6.2831853f;
    int hue=(int)lroundf(angle*57.2957795f)%360;
    if(w->color_hue!=hue){
        wheel_mark_hue(w,hue);
        const int sat=w->color_sat;
        const int brightness=w->color_val;
        if(w->color_preview)
            lv_obj_set_style_bg_color(w->color_preview,
              lv_color_hsv_to_rgb(w->color_hue,sat,brightness),0);
        /* The beta publishes each changed RGB while dragging. */
        event_local(event);
    }
}

/* Legacy beta GenericCmd fallback: use MQTT if connected, otherwise
 * enqueue the configured HTTP URL from Paramètres. Never perform blocking
 * HTTP in an LVGL event and never duplicate a successful MQTT action.
 */
static bool mira_other_transport_action(const char *name,const char *type,
                                        const char *value){
    if(mira_mqtt_publish_action(name,value))return true;
    return mira_generic_queue_local_action(name,type,value);
}

static void event_local(lv_event_t *e){
    if(suppress_events||display_sleeping)return;
    mira_widget_t *b=(mira_widget_t*)lv_event_get_user_data(e);
    if(!b)return;
    lv_obj_t *src=lv_event_get_target_obj(e);
    /* Beta local-GPIO binding semantics: a button/toggle linked to a
     * physical output executes locally and NEVER duplicates the event on
     * Jeedom/MQTT. The 4.3C uses DO0/DO1, not WT32 EXT pin numbers.
     */
    if(!strcmp(b->type,"button")&&mira_gpio43_action_mapped(b->name)){
        const bool success=mira_gpio43_execute_action(b->name);
        if(feedback)lv_label_set_text(feedback,success?
            "Commande sur sortie locale Waveshare":"Sortie GPIO indisponible");
        return;
    }
    if(!strcmp(b->type,"toggle")&&mira_gpio43_toggle_mapped(b->name)){
        const bool desired=lv_obj_has_state(src,LV_STATE_CHECKED);
        if(!mira_gpio43_execute_toggle(b->name,desired)){
            bool actual=false;
            if(mira_gpio43_toggle_state(b->name,&actual)){
                suppress_events=true;
                if(actual)lv_obj_add_state(src,LV_STATE_CHECKED);
                else lv_obj_remove_state(src,LV_STATE_CHECKED);
                suppress_events=false;
            }
        }
        snprintf(b->value,sizeof(b->value),"%s",
                 lv_obj_has_state(src,LV_STATE_CHECKED)?"On":"Off");
        if(feedback)lv_label_set_text(feedback,"Bascule locale Waveshare");
        return;
    }
    if(!strcmp(b->type,"toggle")){
        snprintf(b->value,sizeof(b->value),"%s",lv_obj_has_state(src,LV_STATE_CHECKED)?"On":"Off");
    }else if(!strcmp(b->type,"range")||!strcmp(b->type,"roundrange")||!strcmp(b->type,"thermostat")){
        int v=(!strcmp(b->type,"roundrange")||!strcmp(b->type,"thermostat"))?lv_arc_get_value(src):lv_slider_get_value(src);
        if(!strcmp(b->type,"thermostat")||!strcmp(b->type,"roundrange"))
            snprintf(b->value,sizeof(b->value),"%.1f",v*0.5f);
        else
            snprintf(b->value,sizeof(b->value),"%d",v);
        if(b->value_label){
            char txt[192];
            if(!strcmp(b->type,"thermostat"))
                snprintf(txt,sizeof(txt),"%s°",b->value);
            else snprintf(txt,sizeof(txt),"%s %s",b->value,b->unit);
            lv_label_set_text(b->value_label,txt);
        }
    }else if(!strcmp(b->type,"color")){
        // Preserve full HSV (and therefore full RGB) rather than forcing
        // every outgoing command to maximum saturation and brightness.
        const uint16_t hue=b->color_hue;
        const uint8_t saturation=b->color_sat;
        const uint8_t brightness=b->color_val;
        const lv_color_t col=lv_color_hsv_to_rgb(hue,saturation,brightness);
        const uint32_t rgb=lv_color_to_u32(col);
        /* MiraPanelUI::onColor publishes #RRGGBB to Actions/, never
         * "r,g,b". Keep the same hex payload for MQTT and the cache;
         * the Jeedom HTTP encoder converts it to the plugin's RGB data.
         */
        snprintf(b->value,sizeof(b->value),"#%02X%02X%02X",
                 (unsigned)((rgb>>16)&255u),(unsigned)((rgb>>8)&255u),
                 (unsigned)(rgb&255u));
        if(b->color_preview)lv_obj_set_style_bg_color(b->color_preview,col,0);
    }else if(!strcmp(b->type,"list")||!strcmp(b->type,"listbutton")){
        // The selected label is the outward command value, as in the
        // original Mira list controls. No network command on passive state.
        lv_dropdown_get_selected_str(src,b->value,sizeof(b->value));
    }else if(!strcmp(b->type,"button")){
        strcpy(b->value,"1");
    }
    // Unlike MQTT States, only a real human LVGL action may publish Actions.
    /* Arduino onThermostatRange() publishes "22" at full degrees and
     * "22.5" on half degrees. Keep the display label at one decimal,
     * but do not send the unrelated "22.0" MQTT/Jeedom action value.
     */
    char thermostat_payload[24]={0};
    const char *payload=(!strcmp(b->type,"toggle")) ?
                        (!strcmp(b->value,"On")?"1":"0") : b->value;
    if((!strcmp(b->type,"thermostat")||!strcmp(b->type,"roundrange"))&&b->control){
        const int half=lv_arc_get_value(b->control);
        if(half&1)snprintf(thermostat_payload,sizeof(thermostat_payload),
                           "%.1f",half*0.5f);
        else snprintf(thermostat_payload,sizeof(thermostat_payload),
                      "%d",half/2);
        payload=thermostat_payload;
    }
    // The old beta exposes the displayed switch state through GET /get.
    // Update the read-only HTTP cache for a real local touch, without
    // feeding it through the passive queue or generating another action.
    if(strcmp(b->type,"button")!=0){
        mira_state_update_t observation={0};
        snprintf(observation.name,sizeof(observation.name),"%s",b->name);
        snprintf(observation.value,sizeof(observation.value),"%s",b->value);
        remember_passive_state(&observation);
    }
    const bool use_jeedom=mira_jeedom_actions_enabled();
    const bool mqtt_sent=!use_jeedom&&mira_other_transport_action(b->name,b->type,payload);
    const bool jeedom_queued=use_jeedom&&mira_jeedom_queue_local_action(b->name,b->type,payload);
    if(feedback) {
        char result[180];
        snprintf(result,sizeof(result),"%s : %.60s = %.60s",
                  jeedom_queued?"Action Jeedom en file":(mqtt_sent?"Action MQTT / HTTP":"Aperçu local"),b->name,b->value);
        lv_label_set_text(feedback,result);
    }
}
/* Arduino beta sends range/thermostat actions during dragging. Keep the
 * UI responsive without flooding the bounded Jeedom/MQTT queues. The final
 * value is always sent at release if the throttle skipped it. */
static void event_live_range(lv_event_t *event){
    if(suppress_events||display_sleeping)return;
    mira_widget_t *w=(mira_widget_t*)lv_event_get_user_data(event);
    if(!w||!w->control)return;
    const int value=(!strcmp(w->type,"range")) ?
        lv_slider_get_value(w->control) : lv_arc_get_value(w->control);
    const lv_event_code_t code=lv_event_get_code(event);
    const uint32_t now=lv_tick_get();
    /* Arduino beta: range and roundrange send every changed value,
     * including the release even if unchanged. Thermostat alone throttles
     * VALUE_CHANGED by 130 ms and always sends on release. */
    if(code==LV_EVENT_VALUE_CHANGED){
        if(w->live_sent&&w->live_last_value==value)return;
        if(!strcmp(w->type,"thermostat")&&w->live_last_tick!=0&&
           (uint32_t)(now-w->live_last_tick)<130u)return;
    }else if(code!=LV_EVENT_RELEASED)return;
    event_local(event);
    w->live_sent=true;
    w->live_last_value=value;
    w->live_last_tick=now;
}
// Multi-action dispatch is deferred until the double/triple-click window
// closes. It is processed on the LVGL task without timers tied to deleted
// objects, so hot reprogramming cannot leave callbacks pointing to old cards.
static void multi_dispatch(mira_widget_t *w,uint8_t gesture){
    if(!w||suppress_events||display_sleeping)return;
    static const char *const prefix[]={"Multi_Clic_","Multi_Double_","Multi_Triple_","Multi_Long_"};
    static const char *const kind[]={"single","double","triple","long"};
    if(gesture<1||gesture>4)return;
    const int selected=(int)gesture-1;
    bool sent=false;
    if(mira_jeedom_actions_enabled())
        sent=mira_jeedom_queue_local_action(w->name,"multiaction",kind[selected]);
    else{
        char action[112];
        int n=snprintf(action,sizeof(action),"%s%s",prefix[selected],w->name);
        if(n>0&&n<(int)sizeof(action))
            sent=mira_other_transport_action(action,"button","1");
    }
    if(feedback){
        char result[180];
        snprintf(result,sizeof(result),"Multi %.90s : %s (%s)",w->name,
                 kind[selected],sent?"envoyé":"non envoyé");
        lv_label_set_text(feedback,result);
    }
}
static void multiaction_event(lv_event_t *event){
    if(suppress_events||display_sleeping)return;
    mira_widget_t *w=(mira_widget_t*)lv_event_get_user_data(event);
    if(!w||strcmp(w->type,"multiaction"))return;
    const lv_event_code_t code=lv_event_get_code(event);
    if(code==LV_EVENT_PRESSED){w->multi_long_fired=false;return;}
    if(code==LV_EVENT_LONG_PRESSED){
        w->multi_long_fired=true;
        w->multi_clicks=0;
        multi_dispatch(w,4);
        return;
    }
    if(code!=LV_EVENT_CLICKED||w->multi_long_fired)return;
    const uint32_t now=lv_tick_get();
    if(!w->multi_clicks||(int32_t)(now-w->multi_deadline)>=0)
        w->multi_clicks=1;
    else if(w->multi_clicks<3)w->multi_clicks++;
    w->multi_deadline=now+380;
    if(w->multi_clicks==3){
        w->multi_clicks=0;
        multi_dispatch(w,3);
    }
}
static const char *listbutton_label(const mira_widget_t *w,uint8_t index){
    if(!w||index>=w->list_count)return "";
    return w->list_text+w->list_offsets[index];
}
static void listbutton_mark_selected(mira_widget_t *w,uint8_t selected){
    if(!w||selected>=w->list_count)return;
    w->list_selected=selected;
    for(uint8_t n=0;n<w->list_count;n++){
        if(!w->list_buttons[n])continue;
        lv_obj_set_style_bg_color(w->list_buttons[n],
                                  n==selected?cyan_c():panel2_c(),0);
        lv_obj_set_style_border_color(w->list_buttons[n],
                                      n==selected?lv_color_hex(0xFFC56B):border_c(),0);
    }
}
static void listbutton_clicked(lv_event_t *event){
    if(suppress_events||display_sleeping)return;
    uintptr_t token=(uintptr_t)lv_event_get_user_data(event);
    if(!token)return;
    token--;
    const uint8_t widget=(uint8_t)(token>>4),index=(uint8_t)(token&15u);
    if(widget>=widget_n)return;
    mira_widget_t *w=&widgets[widget];
    if(strcmp(w->type,"listbutton")||index>=w->list_count)return;
    const char *value=listbutton_label(w,index);
    listbutton_mark_selected(w,index);
    memmove(w->value,value,strlen(value)+1u);
    bool sent=mira_jeedom_actions_enabled()
        ?mira_jeedom_queue_local_action(w->name,"listbutton",value)
        :mira_other_transport_action(w->name,"listbutton",value);
    if(feedback){
        char response[180];
        snprintf(response,sizeof(response),"%s : %.60s = %.60s",
                 sent?"Sélection envoyée":"Sélection locale",w->name,value);
        lv_label_set_text(feedback,response);
    }
}
static int clamp(int a,int lo,int hi){return a<lo?lo:(a>hi?hi:a);}

// Passive playback-device list updates and currently selected speaker
// are independent. A device Info is never a command.
// Mode commands use the legacy <thermostat>__mode list identity.
// Keep the same choice syntax and MAX_LIST_ITEMS=12 behavior as the
// Arduino beta, including literal \\n separators and trimmed labels.
static void beta_dropdown_options(const char *source,char *out,size_t capacity){
    if(!capacity)return;
    size_t used=0,letters=0;
    unsigned count=0;
    char option[192]={0};
    const unsigned char *p=(const unsigned char *)(source?source:"");
    for(;;){
        bool separator=!*p||*p=='|'||*p=='\n';
        if(*p=='\\'&&p[1]=='n'){separator=true;p++;}
        if(!separator){
            if(letters+1<sizeof(option))option[letters++]=(char)*p;
            p++;
            continue;
        }
        option[letters]=0;
        char *first=option;
        while(*first&&isspace((unsigned char)*first))first++;
        char *end=first+strlen(first);
        while(end>first&&isspace((unsigned char)end[-1]))*--end=0;
        const size_t len=strlen(first);
        if(len&&count<12&&used+len+(count?1u:0u)<capacity){
            if(count)out[used++]='\n';
            memcpy(out+used,first,len);
            used+=len;
            count++;
        }
        letters=0;
        if(!*p||count==12)break;
        p++;
    }
    out[used]=0;
}
static void thermostat_mode_selected(lv_event_t *event){
    if(suppress_events||display_sleeping)return;
    mira_widget_t *w=(mira_widget_t*)lv_event_get_user_data(event);
    if(!w||strcmp(w->type,"thermostat")||!w->thermostat_mode_select)return;
    char name[128],value[96];
    snprintf(name,sizeof(name),"%.95s__mode",w->name);
    lv_dropdown_get_selected_str(w->thermostat_mode_select,value,sizeof(value));
    if(strlen(name)>=96||!value[0])return;
    if(w->thermostat_mode)lv_label_set_text(w->thermostat_mode,value);
    if(mira_jeedom_actions_enabled())
        (void)mira_jeedom_queue_local_action(name,"list",value);
    else
        (void)mira_other_transport_action(name,"list",value);
}
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
    const bool published=!jeedom&&mira_other_transport_action(command,(action>=3)?"range":"button",payload);
    if(action==1&&(queued||published)){
        w->media_is_playing=!w->media_is_playing;
        if(w->media_play_caption)lv_label_set_text(w->media_play_caption,w->media_is_playing?LV_SYMBOL_PAUSE:LV_SYMBOL_PLAY);
    }
    if(feedback){
        char result[180];
        snprintf(result,sizeof(result),"%s : %.112s",
            queued?"Jeedom en file":(published?"MQTT / HTTP envoyé":"Action inactive"),command);
        lv_label_set_text(feedback,result);
    }
}
static void media_add_button(mira_widget_t *w,int action,int x,const char *text){
    lv_obj_t *button=lv_button_create(w->card);
    lv_obj_set_size(button,72,57);
    lv_obj_set_pos(button,x,380);
    panel_style(button,true);
    lv_obj_t *caption=label(button,text,0xF3F6F7,&lv_font_montserrat_20);
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
    lv_obj_t *caption=label(back,"Retour",0xffffff,&mira_font_fr_16);
    lv_obj_center(caption);
    lv_obj_t *heading=label(camera_overlay,widgets[idx].name,0xffffff,&mira_font_fr_16);
    lv_obj_align(heading,LV_ALIGN_TOP_MID,25,12);
    camera_image=lv_image_create(camera_overlay);
    lv_obj_align(camera_image,LV_ALIGN_CENTER,0,22);
    camera_status=label(camera_overlay,"Chargement caméra BMP/JPEG...",0xffffff,&mira_font_fr_16);
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

// Firmware-beta uses snapshots ONLY for passive INFO tiles. LVGL 9 draws
// expensive rounded cards and text during scroll; snapshot those tiles to
// opaque RGB565 once during IDLE, retain their geometry via IGNORE_LAYOUT,
// and restore live interactive content when the gesture settles.
// Never snapshot while moving or while new Jeedom values arrive.
#define MIRA_SCROLL_CACHE_BUDGET (384u*1024u)
typedef struct {
    lv_obj_t *card,*image;
    lv_image_dsc_t dsc;
    uint8_t *pixels;
    uint32_t bytes;
    bool ready,dirty,disabled;
} mira_scroll_tile_t;
static mira_scroll_tile_t scroll_tiles[MAX_WIDGETS];
static uint32_t scroll_cache_bytes;
static uint32_t scroll_last_move_ms,scroll_last_build_ms;
static bool scroll_cache_active;

static void scroll_cache_activate(bool active){
    if(scroll_cache_active==active)return;
    scroll_cache_active=active;
    for(uint8_t i=0;i<widget_n;i++){
        mira_scroll_tile_t *tile=&scroll_tiles[i];
        if(!tile->ready||!tile->image||!tile->card)continue;
        if(active&&tile->dirty)continue; // never show old Jeedom values
        if(active){
            int32_t edge=((int32_t)tile->dsc.header.w-
                          lv_obj_get_width(tile->card))/2;
            lv_obj_set_pos(tile->image,lv_obj_get_x(tile->card)-edge,
                                       lv_obj_get_y(tile->card)-edge);
            lv_obj_remove_flag(tile->image,LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_opa(tile->card,LV_OPA_TRANSP,0);
        }else{
            lv_obj_set_style_opa(tile->card,LV_OPA_COVER,0);
            lv_obj_add_flag(tile->image,LV_OBJ_FLAG_HIDDEN);
        }
    }
}
static void scroll_cache_reset(void){
    scroll_cache_activate(false);
    for(uint8_t i=0;i<MAX_WIDGETS;i++){
        mira_scroll_tile_t *t=&scroll_tiles[i];
        if(t->image){
            lv_image_set_src(t->image,NULL);
            lv_obj_delete(t->image);
        }
        if(t->pixels)heap_caps_free(t->pixels);
        memset(t,0,sizeof(*t));
    }
    scroll_cache_bytes=0;
    scroll_last_build_ms=0;
}
static void scroll_cache_invalidate(uint8_t index){
    if(index>=MAX_WIDGETS)return;
    mira_scroll_tile_t *tile=&scroll_tiles[index];
    tile->dirty=true;
    // Jeedom can update values DURING an active swipe. Show the live card
    // immediately rather than exposing an outdated cached measurement.
    if(scroll_cache_active&&tile->ready&&tile->card&&tile->image){
        lv_obj_set_style_opa(tile->card,LV_OPA_COVER,0);
        lv_obj_add_flag(tile->image,LV_OBJ_FLAG_HIDDEN);
    }
}
static void scroll_cache_on_move(lv_event_t *event){
    if(lv_event_get_code(event)!=LV_EVENT_SCROLL||display_sleeping)return;
    scroll_last_move_ms=lv_tick_get();
    scroll_cache_activate(true);
}
static void scroll_cache_prepare(uint8_t idx){
    mira_widget_t *widget=&widgets[idx];
    mira_scroll_tile_t *t=&scroll_tiles[idx];
    if(strcmp(widget->type,"info")||!widget->card||
       lv_obj_get_parent(widget->card)!=pages[current_page]||
       t->disabled||(t->ready&&!t->dirty))return;
    lv_obj_t *card_obj=widget->card;
    lv_obj_update_layout(card_obj);
    // LVGL 9.6 removed lv_snapshot_buf_size_needed(). Allow bounded
    // 16px margins for outlines and align every image row to 32 pixels.
    const int32_t w=lv_obj_get_width(card_obj),h=lv_obj_get_height(card_obj);
    if(w<=0||h<=0||w>800||h>428){t->disabled=true;return;}
    const uint32_t stride=((uint32_t)(w+32)+31u)&~31u;
    const uint32_t full_h=(uint32_t)h+32u;
    const uint32_t need_alpha=stride*full_h*4u+128u;
    const uint32_t need_rgb=stride*full_h*2u+128u;
    if(!need_alpha||!need_rgb||need_alpha>480u*1024u||
       need_rgb>320u*1024u){t->disabled=true;return;}
    if(t->ready){
        if(t->image){lv_image_set_src(t->image,NULL);lv_obj_delete(t->image);}
        if(t->pixels)heap_caps_free(t->pixels);
        scroll_cache_bytes-=t->bytes;
        t->image=NULL;t->pixels=NULL;t->bytes=0;t->ready=false;
    }
    // Pressure can be temporary (camera/cover is using PSRAM). Retry after
    // the idle interval instead of permanently disabling the tile.
    if(scroll_cache_bytes+need_rgb>MIRA_SCROLL_CACHE_BUDGET||
       heap_caps_get_free_size(MALLOC_CAP_SPIRAM)<
           need_alpha+need_rgb+256u*1024u)return;
    uint8_t *opaque=heap_caps_malloc(need_rgb,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    uint8_t *alpha=heap_caps_malloc(need_alpha,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!opaque||!alpha){
        if(opaque)heap_caps_free(opaque);
        if(alpha)heap_caps_free(alpha);
        return;  // transient allocation failure; no stale/partial image
    }
    lv_image_dsc_t snapshot={0};
    lv_result_t result=lv_snapshot_take_to_buf(card_obj,LV_COLOR_FORMAT_ARGB8888,
                                              &snapshot,alpha,need_alpha);
    if(result!=LV_RESULT_OK||snapshot.header.cf!=LV_COLOR_FORMAT_ARGB8888||
       !snapshot.header.w||!snapshot.header.h||
       (uint64_t)snapshot.header.w*snapshot.header.h*4u>need_alpha||
       (uint64_t)snapshot.header.w*snapshot.header.h*2u>need_rgb){
        heap_caps_free(alpha);heap_caps_free(opaque);
        t->disabled=true;return;
    }
    // Snapshot ARGB8888 rows may be aligned with padding. Never assume
    // 4*width stride, and NEVER keep that stride in an RGB565 descriptor.
    const uint32_t width=snapshot.header.w,height=snapshot.header.h;
    const uint32_t src_stride=snapshot.header.stride?
                              snapshot.header.stride:width*4u;
    const uint32_t dst_stride=width*2u;
    if(src_stride<width*4u||
       (uint64_t)src_stride*height>need_alpha||
       (uint64_t)dst_stride*height>need_rgb){
        heap_caps_free(alpha);heap_caps_free(opaque);
        t->disabled=true;return;
    }
    const uint32_t background=lv_color_to_u32(bg_c())&0xffffffu;
    for(uint32_t y=0;y<height;y++){
        const uint8_t *src_row=alpha+y*src_stride;
        uint16_t *dst_row=(uint16_t *)(opaque+y*dst_stride);
        for(uint32_t x=0;x<width;x++){
            const uint8_t *pixel=src_row+x*4u;
            // ESP32-S3 uses little-endian BGRA for LVGL ARGB8888.
            uint32_t opacity=pixel[3];
            uint32_t red=((uint32_t)pixel[2]*opacity+
                        ((background>>16)&255u)*(255u-opacity)+127u)/255u;
            uint32_t green=((uint32_t)pixel[1]*opacity+
                          ((background>>8)&255u)*(255u-opacity)+127u)/255u;
            uint32_t blue=((uint32_t)pixel[0]*opacity+
                         (background&255u)*(255u-opacity)+127u)/255u;
            dst_row[x]=(uint16_t)(((red>>3)<<11)|((green>>2)<<5)|(blue>>3));
        }
    }
    heap_caps_free(alpha);
    t->pixels=opaque;
    t->bytes=need_rgb;
    t->card=card_obj;
    t->dsc=snapshot;
    t->dsc.header.cf=LV_COLOR_FORMAT_RGB565;
    t->dsc.header.stride=dst_stride;
    t->dsc.data=opaque;
    t->dsc.data_size=dst_stride*height;
    t->image=lv_image_create(lv_obj_get_parent(card_obj));
    if(!t->image){
        heap_caps_free(opaque);
        memset(t,0,sizeof(*t));
        return;
    }
    lv_obj_add_flag(t->image,LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_remove_flag(t->image,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(t->image,LV_OBJ_FLAG_HIDDEN);
    lv_image_set_src(t->image,&t->dsc);
    const int32_t edge=((int32_t)snapshot.header.w-lv_obj_get_width(card_obj))/2;
    lv_obj_set_pos(t->image,lv_obj_get_x(card_obj)-edge,
                            lv_obj_get_y(card_obj)-edge);
    t->ready=true;
    t->dirty=false;
    scroll_cache_bytes+=need_rgb;
}
static void scroll_cache_service(uint32_t now){
    if(scroll_cache_active&&now-scroll_last_move_ms>=380u)
        scroll_cache_activate(false);
    if(scroll_cache_active||display_sleeping||!widget_n||
       now-last_user_touch_ms<1200u||
       (scroll_last_move_ms&&now-scroll_last_move_ms<850u)||
       now-scroll_last_build_ms<150u)return;
    scroll_last_build_ms=now;
    // Build ONE tile per call; never block the normal 10-15fps UI loop by
    // rasterizing all cards in one pass.
    for(uint8_t i=0;i<widget_n;i++){
        mira_scroll_tile_t *t=&scroll_tiles[i];
        if(!strcmp(widgets[i].type,"info")&&widgets[i].card&&
           lv_obj_get_parent(widgets[i].card)==pages[current_page]&&
           !t->disabled&&(!t->ready||t->dirty)){
            scroll_cache_prepare(i);
            break;
        }
    }
}
// Safe LVGL ownership transfer for a decoded Spotify cover. The HTTP/JPEG
// worker never changes an LVGL object and never frees pixels in use by LVGL.
static uint16_t *artwork_pixels;
static lv_image_dsc_t artwork_desc;
static uint8_t artwork_owner=255;
static void artwork_reset(void){
    mira_album_art_stop();
    if(artwork_owner<widget_n){
        mira_widget_t *w=&widgets[artwork_owner];
        if(w->media_artwork_image)lv_image_set_src(w->media_artwork_image,NULL);
    }
    if(artwork_pixels)heap_caps_free(artwork_pixels);
    artwork_pixels=NULL;
    artwork_owner=255;
    memset(&artwork_desc,0,sizeof(artwork_desc));
}
static void artwork_poll(void){
    uint8_t index=0;
    mira_camera_frame_t frame={0};
    if(!mira_album_art_take(&index,&frame))return;
    if(index>=widget_n||strcmp(widgets[index].type,"media")||
       !widgets[index].media_artwork_image||!frame.pixels||
       !frame.width||!frame.height){
        if(frame.pixels)heap_caps_free(frame.pixels);
        return;
    }
    if(artwork_owner<widget_n){
        mira_widget_t *old=&widgets[artwork_owner];
        if(old->media_artwork_image){
            lv_image_set_src(old->media_artwork_image,NULL);
            lv_obj_add_flag(old->media_artwork_image,LV_OBJ_FLAG_HIDDEN);
            if(old->media_artwork_placeholder)
                lv_obj_remove_flag(old->media_artwork_placeholder,LV_OBJ_FLAG_HIDDEN);
        }
    }
    if(artwork_pixels)heap_caps_free(artwork_pixels);
    artwork_pixels=frame.pixels;
    artwork_owner=index;
    memset(&artwork_desc,0,sizeof(artwork_desc));
    artwork_desc.header.magic=LV_IMAGE_HEADER_MAGIC;
    artwork_desc.header.cf=LV_COLOR_FORMAT_RGB565;
    artwork_desc.header.w=frame.width;
    artwork_desc.header.h=frame.height;
    artwork_desc.data=(const uint8_t *)artwork_pixels;
    artwork_desc.data_size=(uint32_t)frame.width*frame.height*2u;
    mira_widget_t *w=&widgets[index];
    lv_image_set_src(w->media_artwork_image,&artwork_desc);
    const uint32_t biggest=frame.width>frame.height?frame.width:frame.height;
    uint32_t scale=biggest?(230u*256u/biggest):256u;
    if(scale<16)scale=16;
    if(scale>512)scale=512;
    lv_image_set_scale(w->media_artwork_image,scale);
    lv_obj_align(w->media_artwork_image,LV_ALIGN_CENTER,0,0);
    lv_obj_remove_flag(w->media_artwork_image,LV_OBJ_FLAG_HIDDEN);
    if(w->media_artwork_placeholder)
        lv_obj_add_flag(w->media_artwork_placeholder,LV_OBJ_FLAG_HIDDEN);
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
        lv_obj_t *caption=label(notice,display,0xF3F6F7,&mira_font_fr_16);
        lv_obj_set_width(caption,350);
        char missing[96];
        snprintf(missing,sizeof(missing),"Type non porté : %.65s",type);
        lv_obj_t *warning=label(notice,missing,0xFFB35C,&mira_font_fr_12);
        lv_obj_align(warning,LV_ALIGN_BOTTOM_LEFT,0,0);
        return;
    }
    /* firmware-beta MiraPanelUI.cpp registers the four Multi_* IDs for
     * every multiaction, but on MIRA_TARGET_WAVESHARE_43C only draws its
     * tile when WT32MultiVisible(name) returns true. That method always
     * returns false on Waveshare: physical WT32 GPIO multi-buttons do not
     * exist on the 4.3C. Keep the IDs via mira_jeedom index_from_config(),
     * but DO NOT invent a visible touchscreen multiaction button.
     */
    /* On Waveshare the only optional physical multiaction inputs are DI0
     * and DI1. Render a touchscreen tile only when the user explicitly
     * asks for it in the GPIO /multi/config mapping.
     */
    if(!strcmp(type,"multiaction")&&!mira_gpio43_multi_visible(name))return;
    mira_widget_t *w=&widgets[widget_n++];
    memset(w,0,sizeof(*w));
    snprintf(w->name,sizeof(w->name),"%s",name);
    snprintf(w->unit,sizeof(w->unit),"%s",str(o,"unit",""));
    snprintf(w->type,sizeof(w->type),"%s",type);
    snprintf(w->value,sizeof(w->value),"%s",str(o,"value",""));
    // The beta thermostat uses 5..30 C and half-degree setpoints,
    // unlike ordinary integer range widgets.
    const bool thermostat=!strcmp(type,"thermostat");
    w->minv=integer(o,"min",thermostat?5:0);
    w->maxv=integer(o,"max",thermostat?30:100);
    if(thermostat){
        w->minv=clamp(w->minv,-20,60);
        w->maxv=clamp(w->maxv,w->minv+1,80);
    }else if(w->maxv<=w->minv)w->maxv=w->minv+100;
    int cw=390;
    if(!strcmp(layout,"tile"))cw=192;
    if(!strcmp(layout,"wide")||!strcmp(layout,"hero"))cw=786;
    if(!strcmp(type,"datetime")){
        lv_obj_t *c=card(pg,786,158);
        lv_obj_set_style_bg_opa(c,LV_OPA_TRANSP,0);
        lv_obj_set_style_border_width(c,0,0);
        lv_obj_set_style_pad_all(c,0,0);
        clock_text=label(c,"--:--",0xffffff,&mira_home_montserrat_96);
        lv_obj_set_size(clock_text,370,118);
        lv_obj_set_style_text_align(clock_text,LV_TEXT_ALIGN_CENTER,0);
        lv_obj_set_pos(clock_text,14,0);
        date_text=label(c,"-- -- ----",0xA8B2B8,&mira_font_fr_16);
        lv_obj_set_size(date_text,370,26);
        lv_obj_set_style_text_align(date_text,LV_TEXT_ALIGN_CENTER,0);
        lv_obj_set_pos(date_text,14,125);
        clock_status_text=label(c,"MIRA PANEL\nConnexion...\nMode jour",0xA8B2B8,&mira_font_fr_16);
        lv_obj_set_pos(clock_status_text,422,36);
        lv_obj_set_width(clock_status_text,350);
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
            lv_obj_t *nm=label(w->card,display,0xA8B2B8,&mira_font_fr_16);
            w->info_name=nm;
            lv_obj_set_size(nm,width-16,34);
            lv_obj_set_style_text_align(nm,LV_TEXT_ALIGN_CENTER,0);
            lv_obj_align(nm,LV_ALIGN_TOP_MID,0,home_tile?12:1);
            char value[198];snprintf(value,sizeof(value),"%s%s%s",*w->value?w->value:"--",*w->unit?" ":"",w->unit);
            w->value_label=label(w->card,value,0x22BEEA,home_tile?&lv_font_montserrat_28:&lv_font_montserrat_20);
            lv_obj_set_size(w->value_label,width-14,35);
            lv_obj_set_style_text_align(w->value_label,LV_TEXT_ALIGN_CENTER,0);
            lv_obj_align(w->value_label,LV_ALIGN_TOP_MID,0,home_tile?48:26);
            mira_fit_info_value(w->value_label,w->card);
            if(home_tile){
                lv_obj_t *since=label(w->card,*w->value?"Valeur configurée":"En attente des données",0xA8B2B8,&mira_font_fr_12);
                w->info_status=since;
                lv_obj_set_width(since,width-18);
                lv_obj_set_style_text_align(since,LV_TEXT_ALIGN_CENTER,0);
                lv_obj_align(since,LV_ALIGN_BOTTOM_MID,0,-10);
            }
            return;
        }
        const bool hero=!strcmp(layout,"hero");
        const bool value_style=!strcmp(style,"value");
        const bool compact=!strcmp(style,"compact");
        char value[198];
        snprintf(value,sizeof(value),"%s%s%s",*w->value?w->value:"--",
                 *w->unit?" ":"",w->unit);
        if(hero||value_style){
            w->card=card(pg,hero?786:cw,hero?84:72);
            lv_obj_set_style_pad_all(w->card,4,0);
            w->value_label=label(w->card,value,0x22BEEA,&lv_font_montserrat_28);
            lv_obj_set_width(w->value_label,lv_obj_get_width(w->card)-12);
            lv_obj_set_style_text_align(w->value_label,LV_TEXT_ALIGN_CENTER,0);
            lv_obj_align(w->value_label,LV_ALIGN_TOP_MID,0,hero?4:2);
            w->info_name=label(w->card,display,0xA8B2B8,&mira_font_fr_12);
            lv_obj_set_width(w->info_name,lv_obj_get_width(w->card)-12);
            lv_obj_set_style_text_align(w->info_name,LV_TEXT_ALIGN_CENTER,0);
            lv_obj_align(w->info_name,LV_ALIGN_BOTTOM_MID,0,-4);
            mira_fit_info_value(w->value_label,w->card);
            return;
        }
        const bool wide=!strcmp(layout,"wide");
        w->card=card(pg,wide?786:cw,wide?56:(compact?30:36));
        lv_obj_set_style_pad_left(w->card,wide?16:4,0);
        lv_obj_set_style_pad_right(w->card,wide?16:4,0);
        lv_obj_set_style_pad_top(w->card,0,0);
        lv_obj_set_style_pad_bottom(w->card,0,0);
        make_flex(w->card,LV_FLEX_FLOW_ROW,wide?LV_FLEX_ALIGN_SPACE_BETWEEN:LV_FLEX_ALIGN_CENTER,LV_FLEX_ALIGN_CENTER);
        char name_with_colon[110];
        snprintf(name_with_colon,sizeof(name_with_colon),"%s%s",display,
                 !wide&&*display&&display[strlen(display)-1]!=':'?":":"");
        w->info_name=label(w->card,name_with_colon,wide?0xA8B2B8:0xF3F6F7,
                           wide?&mira_font_fr_14:compact?&mira_font_fr_12:&mira_font_fr_14);
        if(wide){
            lv_obj_set_width(w->info_name,440);
            lv_label_set_long_mode(w->info_name,LV_LABEL_LONG_CLIP);
        }
        w->value_label=label(w->card,value,0x22BEEA,
                              wide?&lv_font_montserrat_20:
                              compact?&lv_font_montserrat_14:&mira_font_fr_16);
        if(wide){
            lv_obj_set_width(w->value_label,292);
            lv_obj_set_style_text_align(w->value_label,LV_TEXT_ALIGN_RIGHT,0);
        }else lv_obj_set_style_pad_column(w->card,compact?4:6,0);
        // Fixed-height compact cards must keep their original 14px value.
        if(!compact)mira_fit_info_value(w->value_label,w->card);
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
        lv_obj_t *t=label(bt,display,0xF3F6F7,&mira_font_fr_16);lv_obj_center(t);
        if(!strcmp(type,"multiaction")){
            lv_obj_add_event_cb(bt,multiaction_event,LV_EVENT_PRESSED,w);
            lv_obj_add_event_cb(bt,multiaction_event,LV_EVENT_LONG_PRESSED,w);
            lv_obj_add_event_cb(bt,multiaction_event,LV_EVENT_CLICKED,w);
        }else lv_obj_add_event_cb(bt,event_local,LV_EVENT_CLICKED,w);
        return;
    }
    if(!strcmp(type,"toggle")){
        w->card=card(pg,cw,54);
        lv_obj_set_style_pad_left(w->card,16,0);
        lv_obj_set_style_pad_right(w->card,16,0);
        make_flex(w->card,LV_FLEX_FLOW_ROW,LV_FLEX_ALIGN_SPACE_BETWEEN,LV_FLEX_ALIGN_CENTER);
        label(w->card,display,0xF3F6F7,&mira_font_fr_16);
        w->control=lv_switch_create(w->card);
        lv_obj_set_size(w->control,54,28);
        lv_obj_set_style_bg_color(w->control,cyan_c(),LV_PART_INDICATOR|LV_STATE_CHECKED);
        lv_obj_add_event_cb(w->control,event_local,LV_EVENT_VALUE_CHANGED,w);
        return;
    }
    if(!strcmp(type,"range")||!strcmp(type,"roundrange")||!strcmp(type,"thermostat")){
        bool round=!strcmp(type,"roundrange")||!strcmp(type,"thermostat");
        w->card=card(pg,thermostat?786:(round?390:cw),
                     thermostat?CONTENT_H-16:(round?225:80));
        if(round){
            if(thermostat){
                lv_obj_set_layout(w->card,0);
                lv_obj_set_style_pad_all(w->card,0,0);
                no_scroll(w->card);
                lv_obj_t *title=label(w->card,display,0xF3F6F7,&mira_font_fr_16);
                lv_obj_set_pos(title,14,12);
                lv_obj_set_width(title,310);
            }else label(w->card,display,0xF3F6F7,&mira_font_fr_16);
            if(thermostat){
                lv_obj_t *plate=lv_obj_create(w->card);
                lv_obj_set_size(plate,319,319);
                lv_obj_set_pos(plate,16,51);
                lv_obj_set_style_radius(plate,LV_RADIUS_CIRCLE,0);
                lv_obj_set_style_bg_color(plate,panel2_c(),0);
                lv_obj_set_style_border_color(plate,border_c(),0);
                lv_obj_set_style_pad_all(plate,0,0);
                no_scroll(plate);
                lv_obj_remove_flag(plate,LV_OBJ_FLAG_CLICKABLE);
            }
            w->control=lv_arc_create(w->card);
            lv_obj_set_size(w->control,thermostat?296:165,thermostat?296:165);
            if(thermostat)lv_obj_set_pos(w->control,27,63);
            else lv_obj_align(w->control,LV_ALIGN_CENTER,0,14);
            if(thermostat){
                char *end=NULL;
                const float incoming=strtof(w->value,&end);
                const float initial=(*w->value&&end&&!*end&&isfinite(incoming))
                                     ?incoming:20.0f;
                const int half=clamp((int)lroundf(initial*2.0f),
                                     w->minv*2,w->maxv*2);
                lv_arc_set_range(w->control,w->minv*2,w->maxv*2);
                lv_arc_set_value(w->control,half);
                snprintf(w->value,sizeof(w->value),"%.1f",half*0.5f);
            }else{
                /* Arduino createRoundRange() exposes half-unit resolution.
                 * Keep the full native range doubled; a 20.5 setpoint is
                 * represented by raw LVGL arc value 41.
                 */
                lv_arc_set_range(w->control,w->minv*2,w->maxv*2);
                lv_arc_set_value(w->control,
                                 clamp(atoi(w->value),w->minv,w->maxv)*2);
            }
            if(thermostat){
                lv_arc_set_bg_angles(w->control,135,45);
                lv_obj_set_style_arc_width(w->control,24,LV_PART_MAIN);
                lv_obj_set_style_arc_width(w->control,24,LV_PART_INDICATOR);
                lv_obj_set_style_arc_color(w->control,panel2_c(),LV_PART_MAIN);
            }else{
                lv_arc_set_rotation(w->control,135);
                lv_arc_set_bg_angles(w->control,0,270);
            }
            lv_obj_set_style_arc_color(w->control,cyan_c(),LV_PART_INDICATOR);
            char initial_label[96];
            if(thermostat)snprintf(initial_label,sizeof(initial_label),"%s°",w->value);
            else snprintf(initial_label,sizeof(initial_label),"%s%s%s",
                          w->value,
                          (w->unit[0]&&strncmp(w->unit,"°",2))?" ":"",
                          w->unit);
            if(thermostat){
                lv_obj_t *inner=lv_obj_create(w->card);
                lv_obj_set_size(inner,169,169);
                lv_obj_set_pos(inner,90,126);
                lv_obj_set_style_radius(inner,LV_RADIUS_CIRCLE,0);
                lv_obj_set_style_bg_color(inner,bg_c(),0);
                lv_obj_set_style_border_color(inner,border_c(),0);
                lv_obj_set_style_border_width(inner,2,0);
                lv_obj_set_style_pad_all(inner,0,0);
                no_scroll(inner);
                lv_obj_remove_flag(inner,LV_OBJ_FLAG_CLICKABLE);
                w->value_label=label(inner,initial_label,0xF3F6F7,&lv_font_montserrat_40);
                lv_obj_set_width(w->value_label,157);
                lv_obj_set_style_text_align(w->value_label,LV_TEXT_ALIGN_CENTER,0);
                lv_obj_align(w->value_label,LV_ALIGN_CENTER,0,-16);
                lv_obj_t *hint=label(inner,"CONSIGNE",0xA8B2B8,&mira_font_fr_14);
                lv_obj_set_width(hint,157);
                lv_obj_set_style_text_align(hint,LV_TEXT_ALIGN_CENTER,0);
                lv_obj_align(hint,LV_ALIGN_CENTER,0,26);
            }else{
                w->value_label=label(w->card,initial_label,0xF3F6F7,&lv_font_montserrat_28);
                lv_obj_align(w->value_label,LV_ALIGN_CENTER,0,14);
            }
            if(thermostat){
                lv_obj_t *heading=label(w->card,"Température actuelle",0xA8B2B8,&mira_font_fr_14);
                lv_obj_set_pos(heading,389,122);
                w->thermostat_temperature=label(w->card,"-- °C",0xF3F6F7,&lv_font_montserrat_40);
                lv_obj_set_pos(w->thermostat_temperature,389,156);
                lv_obj_set_width(w->thermostat_temperature,340);
                lv_obj_t *separator=lv_obj_create(w->card);
                lv_obj_set_size(separator,360,1);
                lv_obj_set_pos(separator,386,237);
                lv_obj_set_style_bg_color(separator,border_c(),0);
                lv_obj_set_style_border_width(separator,0,0);
                lv_obj_remove_flag(separator,LV_OBJ_FLAG_CLICKABLE);
                lv_obj_t *mode_heading=label(w->card,"Mode",0xA8B2B8,&mira_font_fr_14);
                lv_obj_set_pos(mode_heading,389,265);
                w->thermostat_mode=label(w->card,"Confort",0x22BEEA,&mira_font_fr_14);
                lv_obj_set_width(w->thermostat_mode,170);
                lv_obj_set_pos(w->thermostat_mode,600,13);
                lv_obj_set_style_text_align(w->thermostat_mode,LV_TEXT_ALIGN_RIGHT,0);
                w->thermostat_mode_select=lv_dropdown_create(w->card);
                lv_obj_set_size(w->thermostat_mode_select,365,43);
                lv_obj_set_pos(w->thermostat_mode_select,387,294);
                lv_dropdown_set_dir(w->thermostat_mode_select,LV_DIR_TOP);
                lv_obj_set_style_text_font(w->thermostat_mode_select,&mira_font_fr_16,0);
                char modes[256];
                beta_dropdown_options(str(o,"modes","Confort|Éco|Hors gel|Arrêt"),
                                      modes,sizeof(modes));
                lv_dropdown_set_options(w->thermostat_mode_select,
                                        *modes?modes:"Confort");
                lv_obj_add_event_cb(w->thermostat_mode_select,thermostat_mode_selected,
                                    LV_EVENT_VALUE_CHANGED,w);
            }
        }else{
            make_flex(w->card,LV_FLEX_FLOW_COLUMN,LV_FLEX_ALIGN_START,LV_FLEX_ALIGN_CENTER);
            lv_obj_t *r=lv_obj_create(w->card);
            lv_obj_set_size(r,365,26);
            no_scroll(r);
            lv_obj_set_style_bg_opa(r,LV_OPA_TRANSP,0);
            lv_obj_set_style_border_width(r,0,0);
            lv_obj_set_style_pad_all(r,0,0);
            make_flex(r,LV_FLEX_FLOW_ROW,LV_FLEX_ALIGN_SPACE_BETWEEN,LV_FLEX_ALIGN_CENTER);
            label(r,display,0xF3F6F7,&mira_font_fr_16);
            w->value_label=label(r,*w->value?w->value:"--",0x22BEEA,&mira_font_fr_16);
            w->control=lv_slider_create(w->card);
            lv_obj_set_size(w->control,350,14);
            lv_slider_set_range(w->control,w->minv,w->maxv);
            lv_slider_set_value(w->control,clamp(atoi(w->value),w->minv,w->maxv),LV_ANIM_OFF);
            lv_obj_set_style_bg_color(w->control,cyan_c(),LV_PART_INDICATOR);
            lv_obj_set_style_bg_color(w->control,lv_color_hex(0xEAF7FF),LV_PART_KNOB);
        }
        // Beta source: live changes (130 ms only for thermostat) and
        // unconditional final release, including the final matching value.
        lv_obj_add_event_cb(w->control,event_live_range,LV_EVENT_VALUE_CHANGED,w);
        lv_obj_add_event_cb(w->control,event_live_range,LV_EVENT_RELEASED,w);
        return;
    }
    if(!strcmp(type,"listbutton")){
        memmove(w->list_text,w->value,strlen(w->value)+1u);
        char *cursor=w->list_text;
        while(*cursor&&w->list_count<12){
            char *next=strchr(cursor,'|');
            if(next)*next=0;
            while(*cursor==' '||*cursor=='\t')cursor++;
            char *end=cursor+strlen(cursor);
            while(end>cursor&&(end[-1]==' '||end[-1]=='\t'))*--end=0;
            if(*cursor){
                w->list_offsets[w->list_count]=(uint8_t)(cursor-w->list_text);
                w->list_count++;
            }
            if(!next)break;
            cursor=next+1;
        }
        if(!w->list_count){
            w->card=card(pg,cw,62);
            label(w->card,"Liste sans choix",0xFFB35C,&mira_font_fr_16);
            return;
        }
        const bool segmented=!strcmp(style,"segmented");
        const bool compact=!strcmp(style,"compact");
        const int per_row=segmented?3:2;
        const int height=compact||segmented?34:38;
        const int rows=(w->list_count+per_row-1)/per_row;
        w->card=card(pg,cw,48+rows*(height+7));
        make_flex(w->card,LV_FLEX_FLOW_COLUMN,LV_FLEX_ALIGN_START,LV_FLEX_ALIGN_CENTER);
        lv_obj_t *name_label=label(w->card,display,0xF3F6F7,&mira_font_fr_16);
        lv_obj_set_width(name_label,cw-28);
        lv_obj_set_style_text_align(name_label,LV_TEXT_ALIGN_CENTER,0);
        lv_obj_t *wrap=lv_obj_create(w->card);
        lv_obj_set_size(wrap,cw-22,rows*(height+7));
        lv_obj_set_style_bg_opa(wrap,segmented?LV_OPA_COVER:LV_OPA_TRANSP,0);
        lv_obj_set_style_bg_color(wrap,panel2_c(),0);
        lv_obj_set_style_border_width(wrap,segmented?1:0,0);
        lv_obj_set_style_pad_all(wrap,3,0);
        lv_obj_set_style_pad_row(wrap,5,0);
        lv_obj_set_style_pad_column(wrap,5,0);
        make_flex(wrap,LV_FLEX_FLOW_ROW_WRAP,LV_FLEX_ALIGN_CENTER,LV_FLEX_ALIGN_CENTER);
        for(uint8_t idx=0;idx<w->list_count;idx++){
            lv_obj_t *button=lv_button_create(wrap);
            w->list_buttons[idx]=button;
            lv_obj_set_size(button,segmented?110:170,height);
            lv_obj_set_style_radius(button,9,0);
            lv_obj_set_style_shadow_width(button,0,0);
            lv_obj_set_style_border_width(button,1,0);
            lv_obj_t *choice=label(button,listbutton_label(w,idx),0xF3F6F7,
                                   compact||segmented?&mira_font_fr_14:&mira_font_fr_16);
            lv_obj_center(choice);
            const uintptr_t token=(((uintptr_t)(w-widgets))<<4)|(uintptr_t)idx;
            lv_obj_add_event_cb(button,listbutton_clicked,LV_EVENT_CLICKED,(void*)(token+1u));
        }
        listbutton_mark_selected(w,0);
        return;
    }
    if(!strcmp(type,"list")){
        w->card=card(pg,cw,96);
        label(w->card,display,0xF3F6F7,&mira_font_fr_16);
        w->control=lv_dropdown_create(w->card);
        lv_obj_set_size(w->control,340,44);
        char options[512];
        beta_dropdown_options(w->value,options,sizeof(options));
        lv_dropdown_set_options(w->control,*options?options:"--");
        lv_obj_align(w->control,LV_ALIGN_BOTTOM_MID,0,-4);
        lv_obj_add_event_cb(w->control,event_local,LV_EVENT_VALUE_CHANGED,w);
        return;
    }
    if(!strcmp(type,"color")){
        w->card=card(pg,786,196);
        lv_obj_t *heading=label(w->card,display,0xF3F6F7,&mira_font_fr_16);
        lv_obj_set_pos(heading,8,3);
        // Restore the beta hue WHEEL (not a linear arc). The shared image
        // costs only 45KB PSRAM and does not recompute on scroll/redraw.
        const lv_color_hsv_t initial=lv_color_rgb_to_hsv(50,120,180);
        w->color_sat=initial.s;
        w->color_val=initial.v;
        w->control=lv_obj_create(w->card);
        lv_obj_set_size(w->control,MIRA_WHEEL_SIZE,MIRA_WHEEL_SIZE);
        lv_obj_set_pos(w->control,318,36);
        lv_obj_set_style_pad_all(w->control,0,0);
        lv_obj_set_style_bg_color(w->control,panel_c(),0);
        lv_obj_set_style_border_width(w->control,0,0);
        lv_obj_set_style_radius(w->control,LV_RADIUS_CIRCLE,0);
        lv_obj_set_style_shadow_width(w->control,0,0);
        no_scroll(w->control);
        if(wheel_prepare_bitmap()){
            lv_obj_t *ring=lv_image_create(w->control);
            lv_image_set_src(ring,&wheel_dsc);
            lv_obj_set_pos(ring,0,0);
            lv_obj_remove_flag(ring,LV_OBJ_FLAG_CLICKABLE);
        }else{
            ESP_LOGW(TAG,"No PSRAM for the beta hue wheel");
        }
        w->color_knob=lv_obj_create(w->control);
        lv_obj_set_size(w->color_knob,14,14);
        lv_obj_set_style_pad_all(w->color_knob,0,0);
        lv_obj_set_style_radius(w->color_knob,LV_RADIUS_CIRCLE,0);
        lv_obj_set_style_border_color(w->color_knob,cyan_c(),0);
        lv_obj_set_style_border_width(w->color_knob,2,0);
        lv_obj_set_style_shadow_width(w->color_knob,0,0);
        no_scroll(w->color_knob);
        lv_obj_remove_flag(w->color_knob,LV_OBJ_FLAG_CLICKABLE);
        wheel_mark_hue(w,initial.h);
        lv_obj_add_event_cb(w->control,wheel_touch,LV_EVENT_PRESSED,w);
        lv_obj_add_event_cb(w->control,wheel_touch,LV_EVENT_PRESSING,w);
        lv_obj_add_event_cb(w->control,event_local,LV_EVENT_RELEASED,w);
        return;
    }
    if(!strcmp(type,"energy")){
        // Restore the beta's 786x344 Jeedom energy dashboard. The generic
        // 7-row table wasn't visually or functionally equivalent.
        const int pad=13,gap=8,box_w=(786-2*pad-gap)/2,box_h=91;
        const int y1=50,y2=y1+box_h+gap;
        static const char *const titles[]={
            "SOLAIRE","MAISON","RÉSEAU","BATTERIE"
        };
        static const uint32_t colors[]={
            0xF6BF49,0xFF9F61,0x42C7EE,0x55DAAB
        };
        w->card=card(pg,786,344);
        lv_obj_set_layout(w->card,LV_LAYOUT_NONE);
        lv_obj_set_style_pad_all(w->card,0,0);
        no_scroll(w->card);
        lv_obj_t *heading=label(w->card,display,0xF3F6F7,&mira_font_fr_16);
        lv_obj_set_pos(heading,pad,9);
        lv_obj_set_width(heading,786-2*pad);
        for(int n=0;n<4;n++){
            const int x=pad+(n%2)*(box_w+gap);
            const int y=y1+(n/2)*(box_h+gap);
            lv_obj_t *cell=lv_obj_create(w->card);
            lv_obj_set_pos(cell,x,y);
            lv_obj_set_size(cell,box_w,box_h);
            lv_obj_set_style_pad_all(cell,0,0);
            lv_obj_set_style_radius(cell,11,0);
            lv_obj_set_style_bg_color(cell,panel2_c(),0);
            lv_obj_set_style_bg_opa(cell,LV_OPA_COVER,0);
            lv_obj_set_style_border_color(cell,lv_color_hex(colors[n]),0);
            lv_obj_set_style_border_width(cell,1,0);
            lv_obj_set_style_shadow_width(cell,0,0);
            no_scroll(cell);
            lv_obj_remove_flag(cell,LV_OBJ_FLAG_CLICKABLE);
            lv_obj_t *name_label=label(cell,titles[n],0xA8B2B8,&mira_font_fr_14);
            lv_obj_set_pos(name_label,10,9);
            lv_obj_set_width(name_label,box_w-20);
            w->fields[n]=label(cell,"--",colors[n],&lv_font_montserrat_20);
            lv_obj_set_pos(w->fields[n],10,38);
            lv_obj_set_width(w->fields[n],box_w-18);
            lv_label_set_long_mode(w->fields[n],LV_LABEL_LONG_DOT);
        }
        const int section_y=y2+box_h+17;
        lv_obj_t *storage=label(w->card,"STOCKAGE",0xA8B2B8,&mira_font_fr_14);
        lv_obj_set_pos(storage,pad+3,section_y);
        lv_obj_set_width(storage,200);
        for(int i=0;i<3;i++){
            int y=section_y+i*47;
            if(i){
                w->energy_extra_names[i-1]=label(w->card,
                    i==1?"BATTERIE 2":"BATTERIE 3",0xA8B2B8,&mira_font_fr_14);
                lv_obj_set_pos(w->energy_extra_names[i-1],pad+3,y);
                lv_obj_set_width(w->energy_extra_names[i-1],210);
                lv_obj_add_flag(w->energy_extra_names[i-1],LV_OBJ_FLAG_HIDDEN);
            }
            w->fields[4+i]=label(w->card,"--",0x55DAAB,&lv_font_montserrat_20);
            lv_obj_set_width(w->fields[4+i],70);
            lv_obj_set_style_text_align(w->fields[4+i],LV_TEXT_ALIGN_RIGHT,0);
            lv_obj_set_pos(w->fields[4+i],786-pad-72,y-1);
            w->energy_gauge[i]=lv_bar_create(w->card);
            lv_obj_set_pos(w->energy_gauge[i],pad+3,y+32);
            lv_obj_set_size(w->energy_gauge[i],786-2*pad-6,11);
            lv_bar_set_range(w->energy_gauge[i],0,100);
            lv_bar_set_value(w->energy_gauge[i],0,LV_ANIM_OFF);
            lv_obj_set_style_bg_color(w->energy_gauge[i],panel2_c(),LV_PART_MAIN);
            lv_obj_set_style_bg_color(w->energy_gauge[i],
                                      lv_color_hex(0x55DAAB),LV_PART_INDICATOR);
            lv_obj_set_style_radius(w->energy_gauge[i],LV_RADIUS_CIRCLE,LV_PART_MAIN);
            lv_obj_set_style_radius(w->energy_gauge[i],LV_RADIUS_CIRCLE,LV_PART_INDICATOR);
            lv_obj_remove_flag(w->energy_gauge[i],LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_flag(w->energy_gauge[i],LV_OBJ_FLAG_HIDDEN);
            if(i)lv_obj_add_flag(w->fields[4+i],LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }
    if(!strcmp(type,"media")){
        // Restore beta's 786x558 Spotify-style card. The former generic
        // key/value table was not Mira 0.7.37's multimedia widget.
        w->card=card(pg,786,558);
        lv_obj_set_layout(w->card,LV_LAYOUT_NONE);
        lv_obj_set_style_pad_all(w->card,0,0);
        no_scroll(w->card);
        lv_obj_t *heading=label(w->card,display,0xF3F6F7,&mira_font_fr_16);
        lv_obj_set_width(heading,742);
        lv_obj_set_style_text_align(heading,LV_TEXT_ALIGN_CENTER,0);
        lv_obj_set_pos(heading,22,7);
        const int artX=(786-230)/2,artY=38;
        for(int side=-1;side<=1;side+=2){
            lv_obj_t *sleeve=lv_obj_create(w->card);
            lv_obj_set_pos(sleeve,artX+side*24,artY+7);
            lv_obj_set_size(sleeve,225,225);
            lv_obj_set_style_radius(sleeve,15,0);
            lv_obj_set_style_bg_color(sleeve,lv_color_hex(0x244463),0);
            lv_obj_set_style_bg_grad_color(sleeve,lv_color_hex(0x101C34),0);
            lv_obj_set_style_bg_grad_dir(sleeve,LV_GRAD_DIR_VER,0);
            lv_obj_set_style_border_width(sleeve,0,0);
            lv_obj_set_style_shadow_width(sleeve,0,0); // preserves fast scroll
            lv_obj_set_style_pad_all(sleeve,0,0);
            lv_obj_remove_flag(sleeve,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
        }
        lv_obj_t *art=lv_obj_create(w->card);
        lv_obj_set_size(art,230,230);
        lv_obj_set_pos(art,artX,artY);
        lv_obj_set_style_radius(art,15,0);
        lv_obj_set_style_clip_corner(art,true,0);
        lv_obj_set_style_bg_color(art,lv_color_hex(0x132A47),0);
        lv_obj_set_style_bg_grad_color(art,lv_color_hex(0x315C87),0);
        lv_obj_set_style_bg_grad_dir(art,LV_GRAD_DIR_VER,0);
        lv_obj_set_style_pad_all(art,0,0);
        lv_obj_set_style_border_width(art,0,0);
        lv_obj_set_style_shadow_width(art,0,0);
        no_scroll(art);
        lv_obj_remove_flag(art,LV_OBJ_FLAG_CLICKABLE);
        w->media_artwork_placeholder=label(art,LV_SYMBOL_AUDIO,0x22BEEA,
                                           &lv_font_montserrat_28);
        lv_obj_center(w->media_artwork_placeholder);
        w->media_artwork_image=lv_image_create(art);
        lv_obj_align(w->media_artwork_image,LV_ALIGN_CENTER,0,0);
        lv_obj_add_flag(w->media_artwork_image,LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(w->media_artwork_image,LV_OBJ_FLAG_CLICKABLE);
        // Keep the original passive field names; hidden metadata still
        // receives Jeedom Infos without creating duplicate visual labels.
        for(int n=0;n<9;n++){
            const char *initial=n==0?"Aucune lecture":n==3?"À l'arrêt":
                                n==5?"-- %":"";
            const lv_font_t *font=n==0?&mira_font_fr_16:
                                   n==1?&mira_font_fr_14:&mira_font_fr_12;
            uint32_t color=n==0?0xF3F6F7:0xA8B2B8;
            w->fields[n]=label(w->card,initial,color,font);
            if(n==0||n==1||n==3){
                lv_obj_set_width(w->fields[n],742);
                lv_obj_set_style_text_align(w->fields[n],LV_TEXT_ALIGN_CENTER,0);
                lv_obj_set_pos(w->fields[n],22,n==0?279:n==1?314:340);
                lv_label_set_long_mode(w->fields[n],LV_LABEL_LONG_DOT);
            }else if(n==5){
                lv_obj_set_width(w->fields[n],49);
                lv_obj_set_style_text_align(w->fields[n],LV_TEXT_ALIGN_RIGHT,0);
                lv_obj_set_pos(w->fields[n],710,472);
            }else lv_obj_add_flag(w->fields[n],LV_OBJ_FLAG_HIDDEN);
        }
        media_add_button(w,0,261,LV_SYMBOL_PREV);
        media_add_button(w,1,357,LV_SYMBOL_PLAY);
        media_add_button(w,2,453,LV_SYMBOL_NEXT);
        lv_obj_t *volume_icon=label(w->card,LV_SYMBOL_VOLUME_MAX,
                                     0x22BEEA,&lv_font_montserrat_20);
        lv_obj_set_pos(volume_icon,22,469);
        w->media_slider=lv_slider_create(w->card);
        lv_obj_set_size(w->media_slider,646,18);
        lv_obj_set_pos(w->media_slider,60,474);
        lv_slider_set_range(w->media_slider,0,100);
        lv_slider_set_value(w->media_slider,40,LV_ANIM_OFF);
        lv_obj_set_style_bg_color(w->media_slider,cyan_c(),LV_PART_INDICATOR);
        uintptr_t vol=((uintptr_t)(w-widgets)<<3)|3u;
        lv_obj_add_event_cb(w->media_slider,media_direct_action,LV_EVENT_RELEASED,(void*)(vol+1));
        w->media_device_dropdown=lv_dropdown_create(w->card);
        lv_obj_set_size(w->media_device_dropdown,660,36);
        lv_obj_set_pos(w->media_device_dropdown,106,509);
        lv_dropdown_set_options(w->media_device_dropdown,"Sélectionner un appareil");
        lv_obj_t *device_title=label(w->card,"Sortie",0xA8B2B8,&mira_font_fr_14);
        lv_obj_set_pos(device_title,22,520);
        uintptr_t device=((uintptr_t)(w-widgets)<<3)|4u;
        lv_obj_add_event_cb(w->media_device_dropdown,media_direct_action,
                            LV_EVENT_VALUE_CHANGED,(void*)(device+1));
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
        lv_obj_t *caption=label(w->control,display,0xF3F6F7,&mira_font_fr_16);
        lv_obj_center(caption);
        uintptr_t idx=(uintptr_t)(w-widgets)+1u;
        lv_obj_add_event_cb(w->control,camera_open_event,LV_EVENT_CLICKED,(void*)idx);
        return;
    }
    w->card=card(pg,cw,50);
    label(w->card,display,0xA8B2B8,&mira_font_fr_16);
}

static bool current_night_period(void); // implementation below
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
    applied_brightness_raw=(uint8_t)value;
    // In automatic mode the same slider adjusts the active day/night level,
    // so the scheduler will not immediately undo the user's change.
    const uint8_t pct=(uint8_t)(1+(((uint32_t)value-30u)*99u+112u)/225u);
    const bool night=night_auto_enabled&&current_night_period();
    if(night_auto_enabled){
        if(night)night_brightness_percent=pct;
        else day_brightness_percent=pct;
    }
    nvs_handle_t nvs;
    if(nvs_open("mira_idf_ui",NVS_READWRITE,&nvs)==ESP_OK){
        if(nvs_set_u8(nvs,"brightness",display_brightness_raw)==ESP_OK){
            if(night_auto_enabled)
                (void)nvs_set_u8(nvs,night?"night_pct":"day_pct",pct);
            (void)nvs_commit(nvs);
        }
        nvs_close(nvs);
    }
}


static void save_sleep_setting_u32(const char *key,uint32_t value){
    nvs_handle_t nv;
    if(nvs_open("mira_idf_ui",NVS_READWRITE,&nv)!=ESP_OK)return;
    if(nvs_set_u32(nv,key,value)==ESP_OK)(void)nvs_commit(nv);
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
    if(value>86400)value=86400;
    if(sleep_timeout_seconds!=(uint32_t)value){
        sleep_timeout_seconds=(uint32_t)value;
        save_sleep_setting_u32("sleep_delay",sleep_timeout_seconds);
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
static int parse_beta_clock(const char *value){
    /* Match Arduino normalizeClock(): trim, decode %3A, and accept the
     * compact HHMM form as well as HH:MM. Invalid input is an instruction
     * to keep the beta default, not disable the schedule entirely.
     */
    if(!value)return -1;
    while(*value&&isspace((unsigned char)*value))value++;
    size_t length=strlen(value);
    while(length&&isspace((unsigned char)value[length-1]))length--;
    const unsigned char *p=(const unsigned char*)value;
    int digits[4]={0};
    unsigned indices[4]={0};
    if(length==4){
        indices[0]=0;indices[1]=1;indices[2]=2;indices[3]=3;
    }else if(length==5&&value[2]==':'){
        indices[0]=0;indices[1]=1;indices[2]=3;indices[3]=4;
    }else if(length==7&&value[2]=='%'&&value[3]=='3'&&
             (value[4]=='A'||value[4]=='a')){
        indices[0]=0;indices[1]=1;indices[2]=5;indices[3]=6;
    }else return -1;
    for(unsigned i=0;i<4;i++){
        if(p[indices[i]]<'0'||p[indices[i]]>'9')return -1;
        digits[i]=(int)(p[indices[i]]-'0');
    }
    const int hour=digits[0]*10+digits[1];
    const int minute=digits[2]*10+digits[3];
    return hour<24&&minute<60?hour*60+minute:-1;
}
static bool current_night_period(void){
    if(!night_auto_enabled||night_start_minute<0||night_end_minute<0||
       night_start_minute==night_end_minute)return false;
    time_t now=time(NULL);
    if(now<1700000000)return false; // No reliable clock before SNTP.
    struct tm tmv;
    if(!localtime_r(&now,&tmv))return false;
    const int minute=tmv.tm_hour*60+tmv.tm_min;
    if(night_start_minute>night_end_minute)
        return minute>=night_start_minute||minute<night_end_minute;
    return minute>=night_start_minute&&minute<night_end_minute;
}
static uint8_t desired_awake_brightness(void){
    if(manual_brightness_override)return display_brightness_raw;
    if(!night_auto_enabled||night_start_minute<0||night_end_minute<0||
       night_start_minute==night_end_minute)return display_brightness_raw;
    time_t now=time(NULL);
    if(now<1700000000)return display_brightness_raw;
    return raw_from_percent(current_night_period()?night_brightness_percent:
                                                day_brightness_percent);
}
static void apply_auto_brightness(void){
    // Original beta releases temporary manual brightness at day/night switch.
    if(night_auto_enabled&&night_start_minute>=0&&night_end_minute>=0&&
       night_start_minute!=night_end_minute&&time(NULL)>=1700000000){
        const bool night=current_night_period();
        if(last_night_valid&&night!=last_night_period)
            manual_brightness_override=false;
        last_night_period=night;
        last_night_valid=true;
    }else last_night_valid=false;
    if(display_sleeping)return;
    const uint8_t wanted=desired_awake_brightness();
    if(wanted==applied_brightness_raw)return;
    if(mira_display_set_brightness(wanted)==ESP_OK)applied_brightness_raw=wanted;
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
    applied_brightness_raw=raw;
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
    applied_brightness_raw=desired_awake_brightness();
    ESP_ERROR_CHECK_WITHOUT_ABORT(mira_display_set_brightness(applied_brightness_raw));
    ESP_LOGI(TAG,"Mira display awakened");
}
static void apply_sleep_settings_after_reload(void){
    if(!display_sleeping)return;
    const uint8_t raw=sleep_clock_enabled?raw_from_percent(sleep_brightness_percent):0;
    if(raw!=applied_brightness_raw&&mira_display_set_brightness(raw)==ESP_OK)
        applied_brightness_raw=raw;
    if(sleep_overlay){
        if(sleep_clock_enabled&&raw>0)
            lv_obj_remove_flag(sleep_overlay,LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(sleep_overlay,LV_OBJ_FLAG_HIDDEN);
    }
}

// Beta MiraPanelUI::handleMqttDisplayCommand, native ESP-IDF equivalent.
// This function runs ONLY from the LVGL task; the MQTT callback queues data.
static bool display_true(const char *value){
    return !strcasecmp(value,"1")||!strcasecmp(value,"true")||
           !strcasecmp(value,"on")||!strcasecmp(value,"yes");
}
static bool display_false(const char *value){
    return !strcasecmp(value,"0")||!strcasecmp(value,"false")||
           !strcasecmp(value,"off")||!strcasecmp(value,"no");
}
static bool display_number(const char *value,int max,int *result){
    if(!value||!*value)return false;
    int n=0;
    for(const unsigned char *p=(const unsigned char*)value;*p;p++){
        if(*p<'0'||*p>'9'||n>(max-(*p-'0'))/10)return false;
        n=n*10+(*p-'0');
    }
    *result=n;return true;
}
static void display_command_apply(const mira_display_command_t *cmd){
    char key[32],value[48];
    snprintf(key,sizeof(key),"%s",cmd->key);
    snprintf(value,sizeof(value),"%s",cmd->value);
    for(char *p=key;*p;p++)*p=(char)tolower((unsigned char)*p);
    size_t used=strlen(value);
    while(used&&isspace((unsigned char)value[used-1]))value[--used]=0;
    char *trim=value;
    while(*trim&&isspace((unsigned char)*trim))trim++;
    if(!strcmp(key,"wake")||!strcmp(key,"sleep")){
        if(!display_true(trim))return;
        if(!strcmp(key,"wake")){
            display_wake();
            // A remote wake is user activity: do not immediately re-enter sleep.
            last_user_touch_ms=lv_tick_get();
        }else display_enter_sleep();
        return;
    }
    if(!strcmp(key,"brightness")){
        if(!strcasecmp(trim,"auto")){
            manual_brightness_override=false;
        }else{
            int brightness=0;
            if(!display_number(trim,100,&brightness))return;
            display_brightness_raw=raw_from_percent((uint8_t)brightness);
            manual_brightness_override=true;
        }
        apply_auto_brightness();
        return;
    }
    const char *nvs_key=NULL;
    enum {VALUE_U8,VALUE_U32,VALUE_CLOCK} kind=VALUE_U8;
    int value_number=0,minimum=0,maximum=100;
    if(!strcmp(key,"day")){nvs_key="day_pct";minimum=1;}
    else if(!strcmp(key,"night")){nvs_key="night_pct";minimum=1;}
    else if(!strcmp(key,"sleep_brightness"))nvs_key="sleep_light";
    else if(!strcmp(key,"sleep_timeout")){nvs_key="sleep_delay";maximum=86400;kind=VALUE_U32;}
    else if(!strcmp(key,"night_auto")){nvs_key="night_auto";maximum=1;}
    else if(!strcmp(key,"clock_sleep")){nvs_key="sleep_clock";maximum=1;}
    else if(!strcmp(key,"night_start")){nvs_key="night_start";kind=VALUE_CLOCK;}
    else if(!strcmp(key,"night_end")){nvs_key="night_end";kind=VALUE_CLOCK;}
    else return;
    char clock[6]={0};
    if(kind==VALUE_CLOCK){
        const int parsed=parse_beta_clock(trim);
        if(parsed<0)return;
        /* Firmware-beta normalizes HHMM / HH:MM / HH%3AMM to HH:MM.
         * Format exactly five characters without a truncation-prone
         * snprintf into a six-character buffer.
         */
        const int hh=parsed/60,mm=parsed%60;
        clock[0]=(char)('0'+hh/10);
        clock[1]=(char)('0'+hh%10);
        clock[2]=':';
        clock[3]=(char)('0'+mm/10);
        clock[4]=(char)('0'+mm%10);
        clock[5]=0;
    }else if(!strcmp(key,"night_auto")||!strcmp(key,"clock_sleep")){
        if(display_true(trim))value_number=1;
        else if(display_false(trim))value_number=0;
        else return;
    }else if(!display_number(trim,maximum,&value_number)||value_number<minimum)return;
    nvs_handle_t nv;
    if(nvs_open("mira_idf_ui",NVS_READWRITE,&nv)!=ESP_OK)return;
    esp_err_t err;
    if(kind==VALUE_CLOCK)err=nvs_set_str(nv,nvs_key,clock);
    else if(kind==VALUE_U32)err=nvs_set_u32(nv,nvs_key,(uint32_t)value_number);
    else err=nvs_set_u8(nv,nvs_key,(uint8_t)value_number);
    if(err==ESP_OK)err=nvs_commit(nv);
    nvs_close(nv);
    if(err!=ESP_OK){
        ESP_LOGW(TAG,"MQTT display parameter %s could not be saved",key);
        return;
    }
    // The existing reload path owns settings and LVGL tree refresh.
    // No command is sent to an actuator and no MQTT echo is published.
    settings_reload_pending=true;
}
static void apply_pending_display_commands(void){
    if(!display_command_queue)return;
    mira_display_command_t cmd;
    for(int i=0;i<8&&xQueueReceive(display_command_queue,&cmd,0)==pdTRUE;i++)
        display_command_apply(&cmd);
}
static void return_home_changed(lv_event_t *event){
    if(suppress_events)return;
    lv_obj_t *src=lv_event_get_target_obj(event);
    return_home_enabled=lv_obj_has_state(src,LV_STATE_CHECKED);
    save_sleep_setting_u8("home_return",return_home_enabled?1:0);
}
static void night_auto_changed(lv_event_t *event){
    if(suppress_events)return;
    lv_obj_t *src=lv_event_get_target_obj(event);
    const bool requested=lv_obj_has_state(src,LV_STATE_CHECKED);
    // Refuse to enable an unspecified or degenerate beta schedule.
    if(requested&&(night_start_minute<0||night_end_minute<0||
                   night_start_minute==night_end_minute)){
        lv_obj_remove_state(src,LV_STATE_CHECKED);
        ESP_LOGW(TAG,"Day/night schedule not configured; retaining manual brightness");
        return;
    }
    night_auto_enabled=requested;
    save_sleep_setting_u8("night_auto",requested?1:0);
    apply_auto_brightness();
}
// Theme transitions occur only on the LVGL task. Rebuild widgets with their
// original JSON programming; never modify Jeedom values or SPIFFS on theme.
static void build_config(void);
static void settings(void);
static void theme_changed(lv_event_t *event){
    if(suppress_events||display_sleeping)return;
    uint16_t selected=lv_dropdown_get_selected(lv_event_get_target_obj(event));
    if(selected>2||selected==ui_theme)return;
    ui_theme=(uint8_t)selected;
    save_sleep_setting_u8("theme",ui_theme);
    // Never delete a dropdown or its parent while LVGL is dispatching its
    // VALUE_CHANGED event. Apply/rebuild in the next LVGL tick instead.
    theme_reload_pending=true;
}
static void theme_apply_on_lvgl_task(void){
    lv_obj_set_style_bg_color(screen,bg_c(),0);
    lv_obj_set_style_bg_grad_color(screen,bg_c(),0);
    for(size_t i=0;i<6;i++){
        lv_obj_set_style_bg_color(pages[i],bg_c(),0);
        lv_obj_set_style_bg_grad_color(pages[i],bg_c(),0);
    }
    lv_obj_set_style_bg_color(nav,nav_c(),0);
    lv_obj_set_style_border_color(nav,border_c(),0);
    if(page_picker){
        lv_obj_set_style_bg_color(page_picker,panel_c(),0);
        if(picker_card)lv_obj_set_style_bg_color(picker_card,panel2_c(),0);
    }
    const uint8_t current=current_page;
    build_config();
    if(pages[3]){lv_obj_clean(pages[3]);settings();}
    show_page(current);
}

static void settings(void){
    lv_obj_t *head=card(3,640,40);
    lv_obj_t *h=label(head,"Paramètres",0xF3F6F7,&mira_font_fr_16);
    lv_obj_center(h);
    lv_obj_t *display=card(3,640,178);
    make_flex(display,LV_FLEX_FLOW_COLUMN,LV_FLEX_ALIGN_START,LV_FLEX_ALIGN_START);
    label(display,"Affichage",0x22BEEA,&mira_font_fr_16);
    label(display,"Luminosité écran",0xF3F6F7,&mira_font_fr_16);
    lv_obj_t *sl=lv_slider_create(display);
    lv_obj_set_size(sl,600,14);
    lv_slider_set_range(sl,30,255);
    lv_slider_set_value(sl,desired_awake_brightness(),LV_ANIM_OFF);
    lv_obj_add_event_cb(sl,brightness_release,LV_EVENT_RELEASED,NULL);
    label(display,"Thème de Mira Panel",0xA8B2B8,&mira_font_fr_16);
    lv_obj_t *theme_choice=lv_dropdown_create(display);
    lv_dropdown_set_options(theme_choice,"Mira\nSombre\nGlass");
    lv_dropdown_set_selected(theme_choice,ui_theme);
    lv_obj_set_size(theme_choice,260,38);
    lv_obj_add_event_cb(theme_choice,theme_changed,LV_EVENT_VALUE_CHANGED,NULL);
    lv_obj_t *schedule=card(3,640,106);
    label(schedule,"Luminosité automatique jour / nuit",0x22BEEA,&mira_font_fr_16);
    lv_obj_t *auto_switch=lv_switch_create(schedule);
    lv_obj_set_pos(auto_switch,550,12);
    if(night_auto_enabled)lv_obj_add_state(auto_switch,LV_STATE_CHECKED);
    lv_obj_add_event_cb(auto_switch,night_auto_changed,LV_EVENT_VALUE_CHANGED,NULL);
    char period[140];
    if(night_start_minute>=0&&night_end_minute>=0){
        snprintf(period,sizeof(period),"Nuit %02d:%02d - %02d:%02d | Jour %u%% / Nuit %u%%",
                 night_start_minute/60,night_start_minute%60,
                 night_end_minute/60,night_end_minute%60,
                 (unsigned)day_brightness_percent,(unsigned)night_brightness_percent);
    }else snprintf(period,sizeof(period),"Horaires à configurer dans les paramètres Mira");
    lv_obj_t *period_label=label(schedule,period,0xA8B2B8,&mira_font_fr_14);
    lv_obj_set_pos(period_label,12,49);
    lv_obj_t *n=card(3,640,72);
    label(n,"Navigation",0x22BEEA,&mira_font_fr_16);
    label(n,"Retour accueil après 60 s",0xF3F6F7,&mira_font_fr_16);
    lv_obj_t *home_switch=lv_switch_create(n);
    lv_obj_set_pos(home_switch,550,28);
    if(return_home_enabled)lv_obj_add_state(home_switch,LV_STATE_CHECKED);
    lv_obj_add_event_cb(home_switch,return_home_changed,LV_EVENT_VALUE_CHANGED,NULL);
    lv_obj_t *sleep=card(3,640,214);
    lv_obj_t *shead=label(sleep,"Veille",0x22BEEA,&mira_font_fr_16);
    lv_obj_set_pos(shead,12,6);
    lv_obj_t *delay_title=label(sleep,"Délai de veille (0 = désactivée)",0xF3F6F7,&mira_font_fr_16);
    lv_obj_set_pos(delay_title,12,38);
    lv_obj_t *delay=lv_slider_create(sleep);
    lv_obj_set_size(delay,575,14);
    lv_obj_set_pos(delay,18,75);
    lv_slider_set_range(delay,0,86400);
    lv_slider_set_value(delay,sleep_timeout_seconds,LV_ANIM_OFF);
    lv_obj_add_event_cb(delay,sleep_delay_changed,LV_EVENT_RELEASED,NULL);
    lv_obj_t *light_title=label(sleep,"Luminosité en veille (%)",0xF3F6F7,&mira_font_fr_16);
    lv_obj_set_pos(light_title,12,104);
    lv_obj_t *dim=lv_slider_create(sleep);
    lv_obj_set_size(dim,575,14);
    lv_obj_set_pos(dim,18,138);
    lv_slider_set_range(dim,0,100);
    lv_slider_set_value(dim,sleep_brightness_percent,LV_ANIM_OFF);
    lv_obj_add_event_cb(dim,sleep_light_changed,LV_EVENT_RELEASED,NULL);
    lv_obj_t *clock_label=label(sleep,"Horloge en veille",0xF3F6F7,&mira_font_fr_16);
    lv_obj_set_pos(clock_label,12,169);
    lv_obj_t *clock_switch=lv_switch_create(sleep);
    lv_obj_set_pos(clock_switch,545,163);
    if(sleep_clock_enabled)lv_obj_add_state(clock_switch,LV_STATE_CHECKED);
    lv_obj_add_event_cb(clock_switch,sleep_clock_changed,LV_EVENT_VALUE_CHANGED,NULL);
}
static void empty_notice(const char *message){
    lv_obj_t *c=card(0,786,120);
    // Onboarding text includes accented French characters (É, é, è, etc.).
    // Plain Montserrat 20 has no such glyphs in this IDF build.
    lv_obj_t *l=label(c,message,0xF3F6F7,&mira_font_fr_16);
    lv_obj_set_width(l,740);
    lv_label_set_long_mode(l,LV_LABEL_LONG_WRAP);
    lv_obj_center(l);
    lv_obj_set_style_text_align(l,LV_TEXT_ALIGN_CENTER,0);
}
// Mira beta's free layout uses a logical 1000x1000 workspace. Apply
// the coordinates only after constructing a widget so every widget type
// can use its existing builder. Ignore layout while positioning the card.
static void apply_free_layout(const cJSON *config,mira_widget_t *widget){
    if(!widget||!widget->card||strcmp(str(config,"layout","auto"),"free"))return;
    const int32_t lx=integer(config,"lx",-1),ly=integer(config,"ly",-1);
    const int32_t lw=integer(config,"lw",-1),lh=integer(config,"lh",-1);
    if(lx<0||ly<0||lw<=0||lh<=0)return;
    int64_t x=(int64_t)lx*MIRA_WIDTH/1000;
    int64_t y=(int64_t)ly*CONTENT_H/1000;
    int64_t width=(int64_t)lw*MIRA_WIDTH/1000;
    int64_t height=(int64_t)lh*CONTENT_H/1000;
    if(width<40)width=40;
    if(height<32)height=32;
    if(width>MIRA_WIDTH)width=MIRA_WIDTH;
    if(x<0)x=0;
    if(x+width>MIRA_WIDTH)x=MIRA_WIDTH-width;
    if(y<0)y=0;
    // Same behavior as the beta: allow overflow at the bottom so the
    // parent page can scroll, rather than silently moving the widget.
    if(y>INT32_MAX-height)return;
    lv_obj_add_flag(widget->card,LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_pos(widget->card,(lv_coord_t)x,(lv_coord_t)y);
    lv_obj_set_size(widget->card,(lv_coord_t)width,(lv_coord_t)height);
    no_scroll(widget->card);
}
static void build_config(void){
    // Validate the new programming before deleting live LVGL objects.
    // A damaged/incomplete SPIFFS read must not blank a running panel.
    FILE *f=spiffs_ready?fopen(JSON_PATH,"rb"):NULL;
    if(!f){
        if(widget_n==0){
            if(mira_beta_setup_unconfigured())
                empty_notice("Première configuration de Mira Panel\n"
                             "Wi-Fi : Mira Panel  |  http://192.168.4.1\n"
                             "Étape 1 : clé locale, puis étape 2 : Wi-Fi");
            else
                empty_notice("Mira Panel : programmation non trouvée.\n"
                             "Configurer les widgets avec le plugin Jeedom");
        }
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
    if(count>MAX_WIDGETS){
        ESP_LOGE(TAG,"Programming has %d widgets (max %d); keeping live UI",count,MAX_WIDGETS);
        cJSON_Delete(root);
        return;
    }
    for(int i=0;i<count;i++){
        if(!cJSON_IsObject(cJSON_GetArrayItem(array,i))){
            ESP_LOGE(TAG,"Invalid Mira widget at index %d; preserving live UI",i);
            cJSON_Delete(root);
            return;
        }
    }
    artwork_reset(); // detach any old cover before deleting media cards
    scroll_cache_reset(); // release every LVGL image reference BEFORE card deletion
    widget_n=0;clock_text=NULL;date_text=NULL;clock_status_text=NULL;widget_count=0;
    for(int i=0;i<6;i++)if(i!=3)lv_obj_clean(pages[i]);
    int home_info=0,seen=0;
    for(int i=0;i<count;i++){
        cJSON *it=cJSON_GetArrayItem(array,i);
        if(!strcmp(str(it,"type",""),"info")&&!strcmp(str(it,"emplacement","accueil"),"accueil")
           &&!strcmp(str(it,"style","standard"),"standard")&&!strcmp(str(it,"layout","auto"),"auto"))home_info++;
    }
    for(int i=0;i<count;i++){
        cJSON *it=cJSON_GetArrayItem(array,i);
        if(cJSON_IsObject(it)){
            const uint8_t before=widget_n;
            make_widget(it,home_info,&seen);
            if(widget_n>before){
                mira_widget_t *w=&widgets[widget_n-1];
                apply_free_layout(it,w);
                if(!strcmp(w->type,"info"))
                    mira_decorate_info(w,str(it,"icon","auto"),
                        str(it,"labelMode","auto"),str(it,"layout","auto"));
            }
        }
    }
    widget_count=(uint32_t)widget_n;
    cJSON_Delete(root);
    if(!count)empty_notice("Aucun widget programmé : importer la configuration Jeedom");
    ESP_LOGI(TAG,"Mira JSON chargé, widgets=%u, heap PSRAM libre=%u",
             widget_n,(unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    show_page(0);
}
// Match Mira beta: favorites (three independently saved slots) and a
// configurable page visibility mask are polled from legacy NVS on LVGL task.
static void refresh_nav_favorites(void){
    for(uint8_t slot=0;slot<3;slot++){
        lv_obj_t *bt=favorite_btn[slot];
        if(!bt)continue;
        lv_obj_clean(bt);
        const uint8_t page=favorite_pages[slot];
        if(page<6 && !(visible_page_mask&(1u<<page))){
            lv_obj_add_flag(bt,LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_remove_flag(bt,LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_opa(bt,page==0?LV_OPA_70:LV_OPA_TRANSP,0);
        if(page==1)mira_nav_bulb(bt);
        else if(page==2)mira_nav_thermometer(bt);
        else{
            const char *symbol=page==0?LV_SYMBOL_HOME:
                               page==3?LV_SYMBOL_SETTINGS:
                               page==4?LV_SYMBOL_CHARGE:
                               page==5?LV_SYMBOL_AUDIO:LV_SYMBOL_PLUS;
            lv_obj_t *l=label(bt,symbol,page==255?0xA8B2B8:0x22BEEA,
                              &lv_font_montserrat_20);
            lv_obj_center(l);
        }
    }
}
static void refresh_page_picker(void){
    if(!picker_card)return;
    uint8_t row=0;
    for(uint8_t i=0;i<6;i++){
        const uint8_t page=(uint8_t)page_order[i];
        lv_obj_t *bt=picker_buttons[page];
        if(!bt)continue;
        if(!(visible_page_mask&(1u<<page))){
            lv_obj_add_flag(bt,LV_OBJ_FLAG_HIDDEN);
        }else{
            lv_obj_remove_flag(bt,LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_pos(bt,10,38+row*54);
            row++;
        }
    }
    lv_obj_set_height(picker_card,52+row*54);
    lv_obj_center(picker_card);
}
static void poll_navigation_settings(void){
    const uint32_t now=lv_tick_get();
    if(now-last_favorite_poll_ms<1000)return;
    last_favorite_poll_ms=now;
    nvs_handle_t nv;
    if(nvs_open("miraui",NVS_READONLY,&nv)!=ESP_OK)return;
    uint8_t mask=0x3f;
    if(nvs_get_u8(nv,"pageMask",&mask)!=ESP_OK)mask=0x3f;
    mask=(mask&0x3fu)|1u;
    uint8_t favorites[3]={0,1,2};
    bool valid=true;
    for(uint8_t i=0;i<3;i++){
        char key[8];
        snprintf(key,sizeof(key),"fav%u",(unsigned)i);
        (void)nvs_get_u8(nv,key,&favorites[i]);
        if(favorites[i]!=255&&favorites[i]>=6)valid=false;
        for(uint8_t j=0;j<i;j++)
            if(favorites[i]!=255&&favorites[i]==favorites[j])valid=false;
    }
    nvs_close(nv);
    bool changed=visible_page_mask!=mask;
    visible_page_mask=mask;
    if(valid){
        for(uint8_t i=0;i<3;i++)if(favorite_pages[i]!=favorites[i]){
            favorite_pages[i]=favorites[i];changed=true;
        }
    }
    if(changed){
        refresh_page_picker();
        refresh_nav_favorites();
        if(!(visible_page_mask&(1u<<current_page)))show_page(0);
    }
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
    picker_card=c;
    lv_obj_set_size(c,460,376);
    panel_style(c,true);lv_obj_center(c);no_scroll(c);
    lv_obj_t *t=label(c,"Toutes les pages",0xF3F6F7,&mira_font_fr_16);
    lv_obj_align(t,LV_ALIGN_TOP_MID,0,12);
    for(int j=0;j<6;j++){
        int p=page_order[j];
        lv_obj_t *bt=lv_button_create(c);
        picker_buttons[p]=bt;
        lv_obj_set_size(bt,420,43);
        lv_obj_set_pos(bt,10,38+j*54);
        panel_style(bt,false);
        lv_obj_t *l=label(bt,page_names[p],0xF3F6F7,&mira_font_fr_16);
        lv_obj_center(l);
        lv_obj_add_event_cb(bt,nav_click,LV_EVENT_CLICKED,(void *)(intptr_t)p);
    }
    lv_obj_add_flag(page_picker,LV_OBJ_FLAG_HIDDEN);
    refresh_page_picker();
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
static void load_ui_configuration(void){
    // Arduino Preferences stored the navigation option in miraui/home.
    // Read it as a fallback only; never rewrite the beta's original NVS.
    nvs_handle_t legacy_nvs;
    if(nvs_open("miraui",NVS_READONLY,&legacy_nvs)==ESP_OK){
        uint8_t saved_theme=0xff;
        if(nvs_get_u8(legacy_nvs,"theme",&saved_theme)==ESP_OK&&saved_theme<=2)
            ui_theme=saved_theme;
        uint8_t legacy_home=1;
        if(nvs_get_u8(legacy_nvs,"home",&legacy_home)==ESP_OK)
            return_home_enabled=legacy_home!=0;
        nvs_close(legacy_nvs);
    }
    nvs_handle_t ui_nvs;
    if(nvs_open("mira_idf_ui",NVS_READONLY,&ui_nvs)==ESP_OK){
        uint8_t configured_theme=0xff;
        if(nvs_get_u8(ui_nvs,"theme",&configured_theme)==ESP_OK&&configured_theme<=2)
            ui_theme=configured_theme;
        uint8_t stored=220;
        if(nvs_get_u8(ui_nvs,"brightness",&stored)==ESP_OK && stored>=30)
            display_brightness_raw=stored;
        uint32_t delay=120;
        esp_err_t d=nvs_get_u32(ui_nvs,"sleep_delay",&delay);
        if(d!=ESP_OK){
            uint16_t legacy_delay=0;
            if(nvs_get_u16(ui_nvs,"sleep_delay",&legacy_delay)==ESP_OK){
                delay=legacy_delay;d=ESP_OK;
            }
        }
        if(d==ESP_OK&&delay<=86400)
            sleep_timeout_seconds=delay;
        uint8_t light=8;
        if(nvs_get_u8(ui_nvs,"sleep_light",&light)==ESP_OK && light<=100)
            sleep_brightness_percent=light;
        uint8_t clock=1;
        if(nvs_get_u8(ui_nvs,"sleep_clock",&clock)==ESP_OK)
            sleep_clock_enabled=clock!=0;
        uint8_t home=1;
        if(nvs_get_u8(ui_nvs,"home_return",&home)==ESP_OK)
            return_home_enabled=home!=0;
        uint8_t day=85,night=20,auto_mode=0;
        if(nvs_get_u8(ui_nvs,"day_pct",&day)==ESP_OK&&day>=1&&day<=100)
            day_brightness_percent=day;
        if(nvs_get_u8(ui_nvs,"night_pct",&night)==ESP_OK&&night>=1&&night<=100)
            night_brightness_percent=night;
        if(nvs_get_u8(ui_nvs,"night_auto",&auto_mode)==ESP_OK)
            night_auto_enabled=auto_mode!=0;
        char clock_time[8]={0};
        size_t clock_size=sizeof(clock_time);
        if(nvs_get_str(ui_nvs,"night_start",clock_time,&clock_size)==ESP_OK){
            const int parsed=parse_beta_clock(clock_time);
            night_start_minute=parsed>=0?parsed:23*60;
        }
        clock_size=sizeof(clock_time);
        if(nvs_get_str(ui_nvs,"night_end",clock_time,&clock_size)==ESP_OK){
            const int parsed=parse_beta_clock(clock_time);
            night_end_minute=parsed>=0?parsed:7*60;
        }
        nvs_close(ui_nvs);
    }
}
void mira_full_ui_create(lv_display_t *disp){
    load_ui_configuration();
    applied_brightness_raw=display_brightness_raw;
    ESP_ERROR_CHECK_WITHOUT_ABORT(mira_display_set_brightness(applied_brightness_raw));
    screen=lv_obj_create(NULL);
    lv_obj_set_size(screen,800,480);
    lv_obj_set_style_bg_color(screen,bg_c(),0);
    lv_obj_set_style_bg_grad_color(screen,bg_c(),0);
    lv_obj_set_style_bg_grad_dir(screen,LV_GRAD_DIR_NONE,0);
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
        lv_obj_set_style_bg_grad_color(pages[i],bg_c(),0);
        lv_obj_set_style_bg_grad_dir(pages[i],LV_GRAD_DIR_NONE,0);
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
        if(i==0||i==1||i==2)
            lv_obj_add_event_cb(pages[i],scroll_cache_on_move,LV_EVENT_SCROLL,NULL);
        if(i)lv_obj_add_flag(pages[i],LV_OBJ_FLAG_HIDDEN);
    }
    nav=lv_obj_create(screen);
    lv_obj_set_pos(nav,0,CONTENT_H);lv_obj_set_size(nav,800,NAV_H);
    lv_obj_set_style_bg_color(nav,nav_c(),0);
    lv_obj_set_style_border_color(nav,border_c(),0);
    lv_obj_set_style_border_width(nav,1,0);
    lv_obj_set_style_radius(nav,0,0);
    lv_obj_set_style_pad_all(nav,0,0);
    no_scroll(nav);
    // Original beta: 3 configurable favorite slots + one Pages button.
    for(int i=0;i<4;i++){
        lv_obj_t *bt=lv_button_create(nav);
        favorite_btn[i]=bt;
        lv_obj_set_pos(bt,1+i*200,1);
        lv_obj_set_size(bt,199,48);
        lv_obj_set_style_bg_opa(bt,i==0?LV_OPA_70:LV_OPA_TRANSP,0);
        lv_obj_set_style_bg_color(bt,lv_color_hex(0x155A75),0);
        lv_obj_set_style_border_width(bt,0,0);
        lv_obj_set_style_shadow_width(bt,0,0);
        lv_obj_add_event_cb(bt,nav_click,LV_EVENT_CLICKED,
                            (void *)(intptr_t)(i==3?6:7+i));
        if(i==3)mira_nav_pages(bt);
    }
    settings();
    create_picker();
    poll_navigation_settings();
    refresh_nav_favorites();
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
    feedback=label(screen,"APERÇU IDF : interactions locales (Jeedom non connecté)",0xA8B2B8,&mira_font_fr_12);
    lv_obj_set_pos(feedback,6,CONTENT_H-17);
    lv_obj_add_flag(feedback,LV_OBJ_FLAG_HIDDEN); // avoid covering real UI

    // Diagnostic banner, always visible on the device, including an empty Mira.
    // Does not depend on an imported miralvgl.json or functioning navigation.
    touch_diag_label=label(screen,"GT911: lecture...",0xDCEAF0,&mira_font_fr_12);
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
    lv_obj_t *btn_caption=label(touch_diag_button,"Paramètres",0xF3F6F7,&mira_font_fr_16);
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

bool mira_full_enqueue_display_command(const char *key,const char *value){
    if(!display_command_queue||!key||!value||!*key||
       strlen(key)>=32||strlen(value)>=48)return false;
    mira_display_command_t msg={0};
    snprintf(msg.key,sizeof(msg.key),"%s",key);
    snprintf(msg.value,sizeof(msg.value),"%s",value);
    return xQueueSend(display_command_queue,&msg,0)==pdTRUE;
}
bool mira_full_cached_state(const char *name,char *out,size_t cap){
    if(!state_cache||!state_cache_lock||!name||!out||!cap)return false;
    if(xSemaphoreTake(state_cache_lock,pdMS_TO_TICKS(40))!=pdTRUE)return false;
    bool found=false;
    for(size_t i=0;i<state_cache_count;i++){
        if(!strcmp(state_cache[i].name,name)){
            snprintf(out,cap,"%s",state_cache[i].value);
            found=true;break;
        }
    }
    xSemaphoreGive(state_cache_lock);
    return found;
}
static void remember_passive_state(const mira_state_update_t *msg){
    if(!state_cache||!state_cache_lock||
       xSemaphoreTake(state_cache_lock,pdMS_TO_TICKS(30))!=pdTRUE)return;
    size_t i=0;
    for(;i<state_cache_count;i++)
        if(!strcmp(state_cache[i].name,msg->name))break;
    if(i<state_cache_count &&
       !strcmp(state_cache[i].value,msg->value)){
        // Dimitri fix principle: do not repeatedly write unchanged info.
        __atomic_fetch_add(&passive_unchanged,1u,__ATOMIC_RELAXED);
        xSemaphoreGive(state_cache_lock);
        return;
    }
    if(i==state_cache_count){
        if(state_cache_count<MIRA_STATE_CACHE_LIMIT)state_cache_count++;
        else {
            // Bounded memory: evict oldest observed name, never allocate per MQTT event.
            __atomic_fetch_add(&passive_evictions,1u,__ATOMIC_RELAXED);
            memmove(state_cache,state_cache+1,
                    (MIRA_STATE_CACHE_LIMIT-1)*sizeof(*state_cache));
            i=MIRA_STATE_CACHE_LIMIT-1;
        }
        snprintf(state_cache[i].name,sizeof(state_cache[i].name),"%s",msg->name);
    }
    snprintf(state_cache[i].value,sizeof(state_cache[i].value),"%s",msg->value);
    xSemaphoreGive(state_cache_lock);
}
bool mira_full_enqueue_state(const char *name,const char *value){
    if(!name||!value||!*name||strlen(name)>=96)return false;
    const size_t size=strlen(value),name_size=strlen(name);
    if(name_size>=7&&!strcmp(name+name_size-7,"__cover")){
        // Artwork is a special passive field, never a Jeedom actuator.
        if(!cover_update_queue||size>768)return false;
        mira_cover_update_t cover={0};
        snprintf(cover.name,sizeof(cover.name),"%s",name);
        memcpy(cover.url,value,size+1);
        if(xQueueSend(cover_update_queue,&cover,0)!=pdTRUE){
            __atomic_fetch_add(&passive_queue_dropped,1u,__ATOMIC_RELAXED);
            return false;
        }
        return true;
    }
    if(!state_queue||size>=160)return false;
    mira_state_update_t msg={0};
    snprintf(msg.name,sizeof(msg.name),"%s",name);
    snprintf(msg.value,sizeof(msg.value),"%s",value);
    if(xQueueSend(state_queue,&msg,0)!=pdTRUE){
        __atomic_fetch_add(&passive_queue_dropped,1u,__ATOMIC_RELAXED);
        return false;
    }
    remember_passive_state(&msg);
    return true;
}
void mira_full_state_diagnostics(mira_full_state_diagnostics_t *out){
    if(!out)return;
    memset(out,0,sizeof(*out));
    out->state_cache_capacity=MIRA_STATE_CACHE_LIMIT;
    out->state_unchanged=__atomic_load_n(&passive_unchanged,__ATOMIC_RELAXED);
    out->state_cache_evictions=__atomic_load_n(&passive_evictions,__ATOMIC_RELAXED);
    out->state_queue_dropped=__atomic_load_n(&passive_queue_dropped,__ATOMIC_RELAXED);
    if(state_queue)out->state_queue_pending=(uint32_t)uxQueueMessagesWaiting(state_queue);
    if(state_cache_lock&&xSemaphoreTake(state_cache_lock,pdMS_TO_TICKS(30))==pdTRUE){
        out->state_cache_used=(uint32_t)state_cache_count;
        xSemaphoreGive(state_cache_lock);
    }
}
static void normalize_widget_name(const char *src,char *dst,size_t length){
    if(!dst||!length)return;
    if(!src){dst[0]=0;return;}
    /* Match beta normalizeMqttTopicName(): String::trim() happens BEFORE
     * replacing interior spaces and MQTT-special characters with '_'.
     * Previous IDF code turned trailing spaces into underscores, losing
     * topics that the Arduino version resolves successfully.
     */
    while(*src&&isspace((unsigned char)*src))src++;
    size_t end=strlen(src);
    while(end&&isspace((unsigned char)src[end-1]))end--;
    size_t j=0;
    for(size_t i=0;i<end&&j+1<length;i++){
        unsigned char c=(unsigned char)src[i];
        if(c==' '||c=='/'||c=='#'||c=='+')c='_';
        if(c>='A'&&c<='Z')c=(unsigned char)(c+('a'-'A'));
        dst[j++]=(char)c;
    }
    dst[j]=0;
}
// Beta miraMediaDisplayText(): Spotify frequently sends NFD UTF-8 which
// the small French bitmap fonts can't display. Normalize DISPLAY text only,
// never rewrite Jeedom metadata, actions, device names or cover URLs.
static void mira_media_display_text(const char *src,char *out,size_t cap){
    if(!out||!cap)return;
    if(!src){out[0]=0;return;}
    static const struct {const char *from,*to;} replacements[]={
        {"a\xCC\x80","\xC3\xA0"},{"a\xCC\x81","\xC3\xA1"},
        {"a\xCC\x82","\xC3\xA2"},{"a\xCC\x88","\xC3\xA4"},
        {"e\xCC\x80","\xC3\xA8"},{"e\xCC\x81","\xC3\xA9"},
        {"e\xCC\x82","\xC3\xAA"},{"e\xCC\x88","\xC3\xAB"},
        {"i\xCC\x81","\xC3\xAD"},{"i\xCC\x82","\xC3\xAE"},
        {"i\xCC\x88","\xC3\xAF"},{"o\xCC\x81","\xC3\xB3"},
        {"o\xCC\x82","\xC3\xB4"},{"o\xCC\x88","\xC3\xB6"},
        {"u\xCC\x80","\xC3\xB9"},{"u\xCC\x81","\xC3\xBA"},
        {"u\xCC\x82","\xC3\xBB"},{"u\xCC\x88","\xC3\xBC"},
        {"c\xCC\xA7","\xC3\xA7"},{"n\xCC\x83","\xC3\xB1"},
        {"A\xCC\x82","\xC3\x82"},{"E\xCC\x82","\xC3\x8A"},
        {"I\xCC\x82","\xC3\x8E"},{"O\xCC\x82","\xC3\x94"},
        {"U\xCC\x82","\xC3\x9B"},
        {"\xE2\x80\x99","'"},{"\xE2\x80\x98","'"},
        {"\xCA\xBC","'"},{"\xE2\x80\x9C","\""},
        {"\xE2\x80\x9D","\""},{"\xE2\x80\x93","-"},
        {"\xE2\x80\x94","-"},{"\xC2\xA0"," "}
    };
    size_t used=0;
    while(*src&&used+1<cap){
        bool replaced=false;
        for(size_t i=0;i<sizeof(replacements)/sizeof(replacements[0]);i++){
            const size_t n=strlen(replacements[i].from);
            if(strncmp(src,replacements[i].from,n))continue;
            const size_t m=strlen(replacements[i].to);
            if(used+m>=cap)break;
            memcpy(out+used,replacements[i].to,m);
            used+=m;src+=n;replaced=true;break;
        }
        if(!replaced)out[used++]=*src++;
    }
    out[used]=0;
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
                        if(media&&n==5){
                            char *end=NULL;
                            long vol=strtol(msg->value,&end,10);
                            char volume_text[24]="-- %";
                            if(*msg->value&&end&&!*end&&vol>=0&&vol<=100)
                                snprintf(volume_text,sizeof(volume_text),"%ld %%",vol);
                            lv_label_set_text(w->fields[n],volume_text);
                        }else if(media&&n==3){
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
                            lv_label_set_text(w->fields[n],playing?"En lecture":
                                              paused?"En pause":"État non disponible");
                        }else if(media&&n<=2){
                            char shown[256];
                            mira_media_display_text(msg->value,shown,sizeof(shown));
                            lv_label_set_text(w->fields[n],shown);
                        }else if(n!=6||!media)
                            lv_label_set_text(w->fields[n],
                                              !media&&!*msg->value?"--":msg->value);
                        if(!media&&n>=4&&n<=6){
                            // Beta only displays battery gauges for values
                            // that EXPLICITLY contain a percent sign.
                            const bool has_percent=strchr(msg->value,'%')!=NULL;
                            const int row=n-4;
                            bool active=true;
                            if(row){
                                const int extra=row-1;
                                active=*msg->value&&strcmp(msg->value,"--")!=0;
                                w->energy_extra_active[extra]=active;
                                if(w->energy_extra_names[extra]){
                                    if(active)lv_obj_remove_flag(w->energy_extra_names[extra],LV_OBJ_FLAG_HIDDEN);
                                    else lv_obj_add_flag(w->energy_extra_names[extra],LV_OBJ_FLAG_HIDDEN);
                                }
                                if(active)lv_obj_remove_flag(w->fields[n],LV_OBJ_FLAG_HIDDEN);
                                else lv_obj_add_flag(w->fields[n],LV_OBJ_FLAG_HIDDEN);
                                const int extra_rows=w->energy_extra_active[1]?2:
                                                     (w->energy_extra_active[0]?1:0);
                                lv_obj_set_height(w->card,344+47*extra_rows);
                            }
                            if(w->energy_gauge[row]){
                                const bool show=active&&has_percent;
                                if(show)lv_obj_remove_flag(w->energy_gauge[row],LV_OBJ_FLAG_HIDDEN);
                                else lv_obj_add_flag(w->energy_gauge[row],LV_OBJ_FLAG_HIDDEN);
                                int percent=atoi(msg->value);
                                if(percent<0)percent=0;
                                if(percent>100)percent=100;
                                lv_bar_set_value(w->energy_gauge[row],
                                                 show?percent:0,LV_ANIM_OFF);
                            }
                        }
                        if(media){
                            if(n==6){
                                const uint8_t index=(uint8_t)(w-widgets);
                                if(!mira_album_art_request(index,msg->value) &&
                                   artwork_owner==index){
                                    // Invalid or empty cover: do not retain old song's art.
                                    artwork_reset();
                                    if(w->media_artwork_placeholder)
                                        lv_obj_remove_flag(w->media_artwork_placeholder,LV_OBJ_FLAG_HIDDEN);
                                }
                            }
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
                                        lv_label_set_text(w->media_play_caption,playing?LV_SYMBOL_PAUSE:LV_SYMBOL_PLAY);
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
                        // Continue examining other widgets: the same media
                        // or energy block can be reused on multiple pages.
                        // Passive updates never invoke outgoing callbacks.
                        break;
                    }
                }
            }
        }
        if(!strcmp(w->type,"thermostat")){
            char temp_name[128],mode_name[128];
            snprintf(temp_name,sizeof(temp_name),"%.95s__temperature",w->name);
            snprintf(mode_name,sizeof(mode_name),"%.95s__mode",w->name);
            if(!strcmp(temp_name,msg->name)&&w->thermostat_temperature){
                char text[185];
                snprintf(text,sizeof(text),"%.150s °C",msg->value);
                lv_label_set_text(w->thermostat_temperature,text);
                continue; // This is information, never a setpoint command.
            }
            if(!strcmp(mode_name,msg->name)&&w->thermostat_mode){
                const char *selected_text=msg->value;
                char selected_option[96]={0};
                if(w->thermostat_mode_select){
                    char *end=NULL;
                    const long choice=strtol(msg->value,&end,10);
                    if(*msg->value&&end&&!*end&&choice>=0&&
                       (uint32_t)choice<lv_dropdown_get_option_count(w->thermostat_mode_select)){
                        suppress_events=true;
                        lv_dropdown_set_selected(w->thermostat_mode_select,(uint32_t)choice);
                        suppress_events=false;
                        lv_dropdown_get_selected_str(w->thermostat_mode_select,
                                                     selected_option,sizeof(selected_option));
                        selected_text=selected_option;
                    }
                }
                lv_label_set_text(w->thermostat_mode,selected_text);
                if(w->thermostat_mode_select){
                    const char *opts=lv_dropdown_get_options(w->thermostat_mode_select);
                    if(opts){
                        uint16_t index=0;
                        const char *start=opts;
                        while(*start){
                            const char *end=strchr(start,'\n');
                            size_t length=end?(size_t)(end-start):strlen(start);
                            if(length==strlen(selected_text)&&!strncasecmp(start,selected_text,length)){
                                suppress_events=true;
                                lv_dropdown_set_selected(w->thermostat_mode_select,index);
                                suppress_events=false;
                                break;
                            }
                            if(!end)break;
                            start=end+1;
                            index++;
                        }
                    }
                }
                continue;
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
        // Beta lists accept both the displayed choice and its numeric index.
        // A passive Jeedom update changes visual selection without actions.
        if(!strcmp(w->type,"listbutton")&&w->list_count){
            char *end=NULL;
            long numeric=strtol(msg->value,&end,10);
            int selected=(*msg->value && end && !*end && numeric>=0 &&
                          numeric<w->list_count)?(int)numeric:-1;
            if(selected<0){
                for(uint8_t n=0;n<w->list_count;n++){
                    if(!strcasecmp(listbutton_label(w,n),msg->value)){
                        selected=n;break;
                    }
                }
            }
            if(selected>=0)listbutton_mark_selected(w,(uint8_t)selected);
        }
        // Remote information is authoritative, but must never trigger an
        // outgoing command. Color states accept legacy #RRGGBB / r,g,b.
        if(w->control && !strcmp(w->type,"thermostat")){
            // Jeedom's setpoint can be fractional. Only accept a complete,
            // finite numeric value; never move the dial to 0 on invalid data.
            char *end=NULL;
            errno=0;
            const float input=strtof(msg->value,&end);
            if(!*msg->value||!end||*end||errno==ERANGE||!isfinite(input))
                continue;
            const int half=clamp((int)lroundf(input*2.0f),
                                 w->minv*2,w->maxv*2);
            suppress_events=true;
            lv_arc_set_value(w->control,half);
            suppress_events=false;
            snprintf(w->value,sizeof(w->value),"%.1f",half*0.5f);
            if(w->value_label){
                char text[48];
                snprintf(text,sizeof(text),"%.1f°",half*0.5f);
                lv_label_set_text(w->value_label,text);
            }
            continue;
        }
        if(w->control && !strcmp(w->type,"roundrange")){
            /* Beta handleMqttState() and onRoundRange() keep the displayed
             * value in half-units; an incoming 20.5 becomes arc value 41.
             * Reconcile passively, without emitting a Jeedom/MQTT action.
             */
            char *end=NULL;
            errno=0;
            const float incoming=strtof(msg->value,&end);
            if(!*msg->value||!end||*end||errno==ERANGE||!isfinite(incoming))
                continue;
            const int half=clamp((int)lroundf(incoming*2.0f),
                                 w->minv*2,w->maxv*2);
            suppress_events=true;
            lv_arc_set_value(w->control,half);
            suppress_events=false;
            snprintf(w->value,sizeof(w->value),"%.1f",half*0.5f);
            if(w->value_label){
                char caption[144];
                snprintf(caption,sizeof(caption),"%s%s%s",
                         w->value,
                         (w->unit[0]&&strncmp(w->unit,"°",2))?" ":"",
                         w->unit);
                lv_label_set_text(w->value_label,caption);
            }
            continue;
        }
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
                wheel_mark_hue(w,hsv.h);
                w->color_sat=hsv.s;
                w->color_val=hsv.v;
                if(w->color_preview)
                    lv_obj_set_style_bg_color(w->color_preview,
                                              lv_color_make(r,g,b),0);
                suppress_events=false;
            }
        }
        snprintf(w->value,sizeof(w->value),"%s",msg->value);
        suppress_events=true;
        if(w->control && !strcmp(w->type,"toggle")){
            bool on=!strcasecmp(msg->value,"on")||!strcasecmp(msg->value,"true")||
                    !strcasecmp(msg->value,"oui")||!strcasecmp(msg->value,"actif")||
                    !strcmp(msg->value,"1");
            if(on)lv_obj_add_state(w->control,LV_STATE_CHECKED);
            else lv_obj_remove_state(w->control,LV_STATE_CHECKED);
        }else if(w->control && (!strcmp(w->type,"range")||!strcmp(w->type,"roundrange")||
                                  !strcmp(w->type,"thermostat"))){
            const int value=clamp(atoi(msg->value),w->minv,w->maxv);
            if(!strcmp(w->type,"range"))lv_slider_set_value(w->control,value,LV_ANIM_OFF);
            else lv_arc_set_value(w->control,value);
        }else if(w->control && !strcmp(w->type,"list")){
            const char *options=lv_dropdown_get_options(w->control);
            if(options){
                char *end_number=NULL;
                long numeric=strtol(msg->value,&end_number,10);
                if(*msg->value&&end_number&&!*end_number&&numeric>=0){
                    uint16_t total=lv_dropdown_get_option_count(w->control);
                    if(numeric<total){
                        lv_dropdown_set_selected(w->control,(uint16_t)numeric);
                    }
                }
                uint16_t index=0;
                const char *start=options;
                while(*start){
                    const char *end=strchr(start,'\n');
                    size_t length=end?(size_t)(end-start):strlen(start);
                    if(strlen(msg->value)==length&&!strncasecmp(start,msg->value,length)){
                        lv_dropdown_set_selected(w->control,index);
                        break;
                    }
                    if(!end)break;
                    start=end+1;
                    index++;
                }
            }
        }
        if(w->info_status){
            // Same arrival notation as beta MiraPanelUI::miraMarkInfoArrival.
            time_t epoch=time(NULL);
            char seen[24]="Valeur reçue";
            if(epoch>=1700000000){
                struct tm tmv={0};
                if(localtime_r(&epoch,&tmv))
                    snprintf(seen,sizeof(seen),"MAJ %02d:%02d",tmv.tm_hour,tmv.tm_min);
            }
            lv_label_set_text(w->info_status,seen);
        }
        if(w->value_label){
            char display[200];
            if(*w->unit)snprintf(display,sizeof(display),"%s %s",msg->value,w->unit);
            else snprintf(display,sizeof(display),"%s",msg->value);
            const char *shown=lv_label_get_text(w->value_label);
            if(!shown||strcmp(shown,display)){
                lv_label_set_text(w->value_label,display);
                if(!strcmp(w->type,"info")){
                    if(w->info_icon)mira_fit_icon_info_value(w);
                    else mira_fit_info_value(w->value_label,w->card);
                    scroll_cache_invalidate((uint8_t)(w-widgets));
                }
            }
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

/* Fixed-size HH:MM formatting for retained MQTT Display states.
 * Keep compile-time 6-byte buffer safety and beta 24-hour semantics.
 */
static void mira_format_hhmm(char out[6],int minute){
    if(minute<0||minute>=24*60)minute=0;
    const int hour=minute/60,min=minute%60;
    out[0]=(char)('0'+hour/10);
    out[1]=(char)('0'+hour%10);
    out[2]=':';
    out[3]=(char)('0'+min/10);
    out[4]=(char)('0'+min%10);
    out[5]=0;
}

void mira_full_ui_tick(void){
    /* Same 250 ms local-toggle feedback as the Arduino beta. Read only
     * on the LVGL task; physical GPIO worker never manipulates UI objects.
     */
    static uint32_t last_gpio_tick=0;
    uint32_t tick=lv_tick_get();
    if(tick-last_gpio_tick>=250){
        last_gpio_tick=tick;
        for(size_t i=0;i<widget_n;i++){
            mira_widget_t *w=&widgets[i];
            if(strcmp(w->type,"toggle")||!w->control||
               !mira_gpio43_toggle_mapped(w->name))continue;
            bool on=false;
            if(!mira_gpio43_toggle_state(w->name,&on))continue;
            const bool shown=lv_obj_has_state(w->control,LV_STATE_CHECKED);
            if(shown!=on){
                suppress_events=true;
                if(on)lv_obj_add_state(w->control,LV_STATE_CHECKED);
                else lv_obj_remove_state(w->control,LV_STATE_CHECKED);
                snprintf(w->value,sizeof(w->value),"%s",on?"On":"Off");
                suppress_events=false;
            }
        }
    }
    poll_navigation_settings();
    if(theme_reload_pending){
        theme_reload_pending=false;
        camera_close();
        theme_apply_on_lvgl_task();
    }
    if(reload_pending){reload_pending=false;camera_close();build_config();}
    if(settings_reload_pending){
        settings_reload_pending=false;
        load_ui_configuration();
        if(pages[3]){lv_obj_clean(pages[3]);settings();}
        apply_sleep_settings_after_reload();
        apply_auto_brightness();
    }
    apply_pending_states();
    if(cover_update_queue){
        mira_cover_update_t cover;
        // GUI task owns all image lifecycle; passive cover updates just
        // hand off a URL to its bounded worker (no HTTP on the GUI).
        for(int attempt=0;attempt<2&&xQueueReceive(cover_update_queue,&cover,0)==pdTRUE;attempt++){
            for(uint8_t i=0;i<widget_n;i++){
                mira_widget_t *w=&widgets[i];
                if(strcmp(w->type,"media"))continue;
                char expected[104];
                snprintf(expected,sizeof(expected),"%.95s__cover",w->name);
                if(strcmp(cover.name,expected))continue;
                if(!mira_album_art_request(i,cover.url)){
                    if(artwork_owner==i)artwork_reset();
                    if(w->media_artwork_placeholder)
                        lv_obj_remove_flag(w->media_artwork_placeholder,LV_OBJ_FLAG_HIDDEN);
                }
            }
        }
    }
    apply_pending_display_commands();
    // Reconcile pending single/double clicks after the gesture window.
    const uint32_t gesture_now=lv_tick_get();
    for(uint8_t n=0;n<widget_n;n++){
        mira_widget_t *w=&widgets[n];
        if(strcmp(w->type,"multiaction")||!w->multi_clicks||
           (int32_t)(gesture_now-w->multi_deadline)<0)continue;
        const uint8_t count=w->multi_clicks;
        w->multi_clicks=0;
        multi_dispatch(w,count);
    }
    static uint32_t diag_last_tick;
    uint32_t now_tick=lv_tick_get();
    camera_poll_on_lvgl_task(now_tick);
    artwork_poll();
    scroll_cache_service(now_tick);
    // Original Mira's 60-second auto return: use verified GT911 physical
    // press counter, not LVGL redraw/Info traffic (which is not user activity).
    mira_display_stats_t last_touch={0};
    mira_display_stats(&last_touch);
    if(last_touch.touch_presses!=last_user_touch_count){
        last_user_touch_count=last_touch.touch_presses;
        last_user_touch_ms=now_tick;
        if(display_sleeping)display_wake();
    }else if(return_home_enabled && !display_sleeping &&
             now_tick-last_user_touch_ms>=60000u && current_page!=0){
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
        apply_auto_brightness();
        // Restore firmware-beta MQTT Display/<key>/state retained feedback.
        // This snapshot is read ONLY from the LVGL task, never an MQTT
        // network callback, and must never generate a Jeedom action.
        mira_mqtt_display_state_t status={
            .day=day_brightness_percent,
            .night=night_brightness_percent,
            .sleep_brightness=sleep_brightness_percent,
            .sleep_timeout=sleep_timeout_seconds,
            .night_auto=night_auto_enabled,
            .clock_sleep=sleep_clock_enabled,
            .night_active=night_auto_enabled&&current_night_period(),
            .sleeping=display_sleeping
        };
        uint8_t brightness=display_sleeping?
            (sleep_clock_enabled?applied_brightness_raw:0):
            applied_brightness_raw;
        status.brightness=brightness==0?0:(brightness<=30?1:
            (uint8_t)(1u+(((uint32_t)brightness-30u)*99u+112u)/225u));
        const int begin=night_start_minute<0?23*60:night_start_minute;
        const int finish=night_end_minute<0?7*60:night_end_minute;
        mira_format_hhmm(status.night_start,begin);
        mira_format_hhmm(status.night_end,finish);
        mira_mqtt_publish_display_states(&status);
        if(clock_status_text){
            wifi_ap_record_t access_point={0};
            char status[150];
            if(esp_wifi_sta_get_ap_info(&access_point)==ESP_OK)
                snprintf(status,sizeof(status),"MIRA PANEL\nWi-Fi  %d dBm\nMode %s",
                    (int)access_point.rssi,current_night_period()?"nuit":"jour");
            else
                snprintf(status,sizeof(status),"MIRA PANEL\nWi-Fi déconnecté\nMode %s",
                    current_night_period()?"nuit":"jour");
            lv_label_set_text(clock_status_text,status);
        }
        time_t now=time(NULL);
        if(now>1700000000){
            struct tm tmv;localtime_r(&now,&tmv);
            char buff[24];snprintf(buff,sizeof(buff),"%02d:%02d",tmv.tm_hour,tmv.tm_min);
            if(clock_text)lv_label_set_text(clock_text,buff);
            if(date_text){
                static const char *const days[]={"Dimanche","Lundi","Mardi","Mercredi",
                                                   "Jeudi","Vendredi","Samedi"};
                static const char *const months[]={"Janvier","Février","Mars","Avril",
                    "Mai","Juin","Juillet","Août","Septembre","Octobre","Novembre","Décembre"};
                char datebuf[80];
                snprintf(datebuf,sizeof(datebuf),"%s, %02d %s %04d",
                         days[tmv.tm_wday],tmv.tm_mday,months[tmv.tm_mon],tmv.tm_year+1900);
                lv_label_set_text(date_text,datebuf);
            }
            if(display_sleeping&&sleep_clock_enabled&&sleep_clock_text)
                lv_label_set_text(sleep_clock_text,buff);
        }
    }
}
// Fresh-install safeguard: a full-chip erase leaves the entire SPIFFS
// partition at 0xff. Mounting it needs a one-time filesystem initialization.
// Never format on a mount failure unless the WHOLE partition is confirmed
// blank; this preserves partially damaged or legacy Mira files for recovery.
static bool spiffs_partition_is_blank(void){
    const esp_partition_t *p=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                              ESP_PARTITION_SUBTYPE_DATA_SPIFFS,"spiffs");
    if(!p)return false;
    // app_main runs with a limited stack: allocate the sector-sized
    // inspection buffer transiently instead of placing 4 KB on its stack.
    const size_t block_size=4096;
    uint8_t *block=malloc(block_size);
    if(!block)return false;
    bool empty=true;
    for(size_t offset=0;offset<p->size&&empty;offset+=block_size){
        const size_t n=(p->size-offset)<block_size?
                       (p->size-offset):block_size;
        if(esp_partition_read(p,offset,block,n)!=ESP_OK){empty=false;break;}
        for(size_t i=0;i<n;i++)if(block[i]!=0xff){empty=false;break;}
    }
    free(block);
    return empty;
}
bool mira_full_storage_init(void){
    if(!state_queue)state_queue=xQueueCreate(24,sizeof(mira_state_update_t));
    if(!cover_update_queue)cover_update_queue=xQueueCreate(2,sizeof(mira_cover_update_t));
    if(!state_queue)ESP_LOGE(TAG,"State queue allocation failed");
    if(!state_cache_lock)state_cache_lock=xSemaphoreCreateMutex();
    if(!state_cache)state_cache=heap_caps_calloc(MIRA_STATE_CACHE_LIMIT,sizeof(*state_cache),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!state_cache||!state_cache_lock)ESP_LOGW(TAG,"Passive state cache unavailable; HTTP /get will return Null");
    if(mira_album_art_init()!=ESP_OK)
        ESP_LOGW(TAG,"Album cover worker unavailable; other media controls still work");
    if(!display_command_queue)display_command_queue=xQueueCreate(12,sizeof(mira_display_command_t));
    if(!display_command_queue)ESP_LOGE(TAG,"Display command queue allocation failed");
    const bool blank=spiffs_partition_is_blank();
    const esp_vfs_spiffs_conf_t cfg={
        .base_path="/spiffs",.partition_label="spiffs",
        .max_files=4,.format_if_mount_failed=blank
    };
    if(blank)ESP_LOGW(TAG,"Fresh flash: blank SPIFFS detected; initializing empty filesystem");
    esp_err_t err=esp_vfs_spiffs_register(&cfg);
    spiffs_ready=(err==ESP_OK);
    if(!spiffs_ready)
        ESP_LOGW(TAG,"SPIFFS mount failed (%s); existing data left unchanged, import disabled",
                 esp_err_to_name(err));
    else if(blank)ESP_LOGI(TAG,"Fresh Mira SPIFFS ready for JSON import");
    else ESP_LOGI(TAG,"Existing Mira SPIFFS mounted without formatting");
    return spiffs_ready;
}
// The native beta home page owns the normal URL, not this experiment screen.
static esp_err_t page_get(httpd_req_t *req){
    if(!mira_beta_setup_unconfigured() && !mira_beta_web_authorized(req))
        return ESP_FAIL;
    const char *html=
        "<!doctype html><html lang='fr'><meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>Mira LVGL 9.6 - Import JSON</title><h2>Mira LVGL 9.6 / Waveshare 4.3C</h2>"
        "<p>Rendu experimental des widgets reels depuis <code>/miralvgl.json</code> (firmware 0.7.37).</p>"
        "<p>Transport MQTT natif disponible apres configuration. API HTTP Jeedom historique, camera et widgets composites en cours de portage.</p>"
        "<p><b>Les anciennes donnees SPIFFS sont conservees. Seule une partition entierement vierge est initialisee.</b></p>"
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
static esp_err_t legacy_mira_get(httpd_req_t *req){
    httpd_resp_set_status(req,"302 Found");
    httpd_resp_set_hdr(req,"Location",mira_beta_setup_unconfigured()?"/load":"/");
    httpd_resp_set_hdr(req,"Cache-Control","no-store");
    return httpd_resp_sendstr(req,"Mira Panel");
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
    // Experimental passive-state injection is not a public Jeedom endpoint.
    // Real Jeedom events use the authenticated legacy /updatenr API.
    if(!mira_beta_web_authorized(req))return ESP_FAIL;
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
    // Share the same bounded state ingestion + HTTP /get cache as Jeedom
    // /updatenr and MQTT; never invent a second state synchronization path.
    bool queued=mira_full_enqueue_state(name->valuestring,val);
    cJSON_Delete(json);
    if(!queued){
        httpd_resp_set_status(req,"503 Service Unavailable");
        return httpd_resp_sendstr(req,"State queue full");
    }
    httpd_resp_set_type(req,"application/json");
    return httpd_resp_sendstr(req,"{\"queued\":true,\"action\":false}");
}

void mira_full_register_http(httpd_handle_t server){
    httpd_uri_t a={.uri="/recovery/mira",.method=HTTP_GET,.handler=page_get};
    httpd_uri_t legacy={.uri="/mira",.method=HTTP_GET,.handler=legacy_mira_get};
    httpd_uri_t b={.uri="/mira/import",.method=HTTP_POST,.handler=import_post};
    httpd_uri_t c={.uri="/mira/state",.method=HTTP_POST,.handler=state_post};
    ESP_ERROR_CHECK_WITHOUT_ABORT(httpd_register_uri_handler(server,&a));
    ESP_ERROR_CHECK_WITHOUT_ABORT(httpd_register_uri_handler(server,&legacy));
    ESP_ERROR_CHECK_WITHOUT_ABORT(httpd_register_uri_handler(server,&b));
    ESP_ERROR_CHECK_WITHOUT_ABORT(httpd_register_uri_handler(server,&c));
}
uint32_t mira_full_widget_count(void){return widget_count;}
