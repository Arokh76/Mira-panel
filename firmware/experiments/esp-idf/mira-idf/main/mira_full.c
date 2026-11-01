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
#include "lvgl.h"

#define MIRA_WIDTH 800
#define MIRA_HEIGHT 480
#define NAV_H 52
#define CONTENT_H (MIRA_HEIGHT - NAV_H)
#define MAX_WIDGETS 64
#define JSON_MAX 49152
static const char *TAG="mira-full-43";
static const char *JSON_PATH="/spiffs/miralvgl.json";
static volatile bool reload_pending=false;
static bool spiffs_ready=false;
static lv_obj_t *screen, *pages[6], *nav, *page_picker;
static lv_obj_t *favorite_btn[4];
static uint8_t current_page=0;
static const char *page_names[6]={"Accueil","Lumières","Températures","Paramètres","Énergie","Multimédia"};
static const int page_order[6]={0,1,2,4,5,3};
static lv_obj_t *clock_text,*date_text;
static uint32_t widget_count;
static uint32_t last_clock_ms;
static lv_obj_t *feedback;

typedef struct {
    char name[96], unit[24], type[24], value[160];
    lv_obj_t *card,*control,*value_label;
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
    int index=(int)(intptr_t)lv_event_get_user_data(e);
    if(index==6) {
        if(lv_obj_has_flag(page_picker,LV_OBJ_FLAG_HIDDEN))
            lv_obj_remove_flag(page_picker,LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(page_picker,LV_OBJ_FLAG_HIDDEN);
    } else show_page(index);
}
static void event_local(lv_event_t *e){
    mira_widget_t *b=(mira_widget_t*)lv_event_get_user_data(e);
    if(!b)return;
    lv_obj_t *src=lv_event_get_target_obj(e);
    if(!strcmp(b->type,"toggle")){
        snprintf(b->value,sizeof(b->value),"%s",lv_obj_has_state(src,LV_STATE_CHECKED)?"On":"Off");
    }else if(!strcmp(b->type,"range")||!strcmp(b->type,"roundrange")||!strcmp(b->type,"thermostat")){
        int v=(!strcmp(b->type,"roundrange"))?lv_arc_get_value(src):lv_slider_get_value(src);
        snprintf(b->value,sizeof(b->value),"%d",v);
        if(b->value_label){
            char txt[192];snprintf(txt,sizeof(txt),"%s %s",b->value,b->unit);
            lv_label_set_text(b->value_label,txt);
        }
    }else if(!strcmp(b->type,"list")){
        uint16_t n=lv_dropdown_get_selected(src);
        lv_dropdown_get_selected_str(src,b->value,sizeof(b->value));
        (void)n;
    }else if(!strcmp(b->type,"button")||!strcmp(b->type,"multiaction")){
        strcpy(b->value,"1");
    }
    // UI-only safety: interactive preview never triggers any Jeedom action.
    if(feedback) {
        char result[180];
        snprintf(result,sizeof(result),"Aperçu local : %.60s = %.60s (aucune commande Jeedom)",
                 b->name,b->value);
        lv_label_set_text(feedback,result);
    }
}
static int clamp(int a,int lo,int hi){return a<lo?lo:(a>hi?hi:a);}
static void make_widget(const cJSON *o,int info_total,int *info_seen){
    if(widget_n>=MAX_WIDGETS)return;
    const char *type=str(o,"type",""),*loc=str(o,"emplacement","accueil");
    const char *style=str(o,"style","standard"),*layout=str(o,"layout","auto");
    int pg=to_page(loc),is_auto=!strcmp(layout,"auto");
    const char *name=str(o,"name","");
    const char *display=str(o,"displayName","");
    if(!*display)display=name;
    if(!*type)return;
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
        lv_obj_t *right=label(c,"MIRA PANEL\nMode jour\nLVGL 9.6 - ESP-IDF",0xA8B2B8,&lv_font_montserrat_16);
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
                lv_obj_t *since=label(w->card,"En attente des données",0xA8B2B8,&lv_font_montserrat_12);
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
        lv_obj_add_event_cb(w->control,event_local,LV_EVENT_VALUE_CHANGED,w);
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
        // LVGL 9.6 does not provide the old LVGL8 colorwheel widget.
        // This is a color hue arc PREVIEW only; no Jeedom action.
        w->control=lv_arc_create(w->card);
        lv_obj_set_size(w->control,150,150);
        lv_arc_set_range(w->control,0,360);
        lv_arc_set_value(w->control,200);
        lv_arc_set_rotation(w->control,135);
        lv_arc_set_bg_angles(w->control,0,270);
        lv_obj_set_style_arc_color(w->control,cyan_c(),LV_PART_INDICATOR);
        lv_obj_align(w->control,LV_ALIGN_CENTER,0,12);
        return;
    }
    // Full composite widget names are retained. Their complex sub-layouts
    // (camera stream, weather artwork, energy flows, media transport) are
    // not yet native: never suggest a working Jeedom control.
    if(!strcmp(type,"energy")||!strcmp(type,"media")||!strcmp(type,"camera")){
        w->card=card(pg,!strcmp(type,"camera")?390:786,190);
        label(w->card,display,0xF3F6F7,&lv_font_montserrat_20);
        const char *msg=(!strcmp(type,"energy"))?"Énergie - champs Jeedom non connectés":
                        (!strcmp(type,"media"))?"Multimédia - transport non connecté":
                        "Caméra - flux non connecté";
        lv_obj_t *note=label(w->card,msg,0xA8B2B8,&lv_font_montserrat_16);
        lv_obj_align(note,LV_ALIGN_CENTER,0,0);
        return;
    }
    w->card=card(pg,cw,50);
    label(w->card,display,0xA8B2B8,&lv_font_montserrat_16);
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
    lv_slider_set_range(sl,30,255);lv_slider_set_value(sl,220,LV_ANIM_OFF);
    label(display,"Thème : Mira",0xA8B2B8,&lv_font_montserrat_16);
    lv_obj_t *n=card(3,640,72);
    label(n,"Navigation",0x22BEEA,&lv_font_montserrat_16);
    label(n,"Retour accueil : 60 s",0xF3F6F7,&lv_font_montserrat_16);
    lv_obj_t *sleep=card(3,640,104);
    label(sleep,"Veille",0x22BEEA,&lv_font_montserrat_16);
    label(sleep,"Mise en veille : 120 s",0xF3F6F7,&lv_font_montserrat_16);
    label(sleep,"Horloge en veille",0xF3F6F7,&lv_font_montserrat_16);
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
    widget_n=0;clock_text=NULL;date_text=NULL;widget_count=0;
    for(int i=0;i<6;i++)if(i!=3)lv_obj_clean(pages[i]);
    FILE *f=spiffs_ready?fopen(JSON_PATH,"rb"):NULL;
    if(!f){
        empty_notice("Mira LVGL 9.6 : configuration non trouvée.\n"
                     "Importer le vrai miralvgl.json via 192.168.4.1/mira");
        ESP_LOGW(TAG,"Mira 0.7.37 JSON absent, import via /mira");
        show_page(0);return;
    }
    if(fseek(f,0,SEEK_END)!=0){fclose(f);empty_notice("Erreur de lecture JSON Mira");return;}
    long len=ftell(f);rewind(f);
    if(len<=0||len>JSON_MAX){fclose(f);empty_notice("JSON Mira trop grand ou vide");return;}
    char *buf=malloc((size_t)len+1);
    if(!buf){fclose(f);empty_notice("Mémoire insuffisante pour JSON Mira");return;}
    size_t n=fread(buf,1,(size_t)len,f);fclose(f);buf[n]=0;
    cJSON *root=cJSON_ParseWithLength(buf,n);free(buf);
    cJSON *array=root?cJSON_GetObjectItemCaseSensitive(root,"lvgl"):NULL;
    if(!cJSON_IsArray(array)){
        empty_notice("Le JSON Mira ne contient pas de tableau lvgl");
        if(root)cJSON_Delete(root);
        return;
    }
    int count=cJSON_GetArraySize(array),home_info=0,seen=0;
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
void mira_full_ui_create(lv_display_t *disp){
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
    feedback=label(screen,"APERÇU IDF : interactions locales (Jeedom non connecté)",0xA8B2B8,&lv_font_montserrat_12);
    lv_obj_set_pos(feedback,6,CONTENT_H-17);
    lv_obj_add_flag(feedback,LV_OBJ_FLAG_HIDDEN); // avoid covering real UI
    last_clock_ms=lv_tick_get();
    ESP_LOGI(TAG,"Mira LVGL 9.6 rendering tree ready, source 0.7.37 JSON");
}
void mira_full_ui_tick(void){
    if(reload_pending){reload_pending=false;build_config();}
    uint32_t ms=lv_tick_get();
    if(ms-last_clock_ms>2000){
        last_clock_ms=ms;
        time_t now=time(NULL);
        if(now>1700000000&&clock_text){
            struct tm tmv;localtime_r(&now,&tmv);
            char buff[24];snprintf(buff,sizeof(buff),"%02d:%02d",tmv.tm_hour,tmv.tm_min);
            lv_label_set_text(clock_text,buff);
        }
    }
}
bool mira_full_storage_init(void){
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
        "<p>Les commandes Jeedom et MQTT NE SONT PAS actives : aperçu visuel et tactile uniquement.</p>"
        "<p><b>La partition SPIFFS n'est jamais formatée automatiquement.</b></p>"
        "<p>Charge uniquement un export JSON Mira contenant le tableau <code>lvgl</code>.</p>"
        "<input id='file' type='file' accept='.json,application/json'>"
        "<button onclick='send()'>Importer dans SPIFFS</button><pre id='out'></pre>"
        "<p><a href='/perf'>Mesures FPS</a> | <a href='/'>OTA et récupération</a></p>"
        "<script>async function send(){let f=document.getElementById('file').files[0];"
        "if(!f)return;let r=await fetch('/mira/import',{method:'POST',headers:{'Content-Type':'application/json'},body:f});"
        "document.getElementById('out').textContent=await r.text();}</script></html>";
    httpd_resp_set_type(req,"text/html; charset=utf-8");
    return httpd_resp_send(req,html,HTTPD_RESP_USE_STRLEN);
}
static esp_err_t import_post(httpd_req_t *req){
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
void mira_full_register_http(httpd_handle_t server){
    httpd_uri_t a={.uri="/mira",.method=HTTP_GET,.handler=page_get};
    httpd_uri_t b={.uri="/mira/import",.method=HTTP_POST,.handler=import_post};
    ESP_ERROR_CHECK_WITHOUT_ABORT(httpd_register_uri_handler(server,&a));
    ESP_ERROR_CHECK_WITHOUT_ABORT(httpd_register_uri_handler(server,&b));
}
uint32_t mira_full_widget_count(void){return widget_count;}
