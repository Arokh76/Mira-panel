// Native Mira 7 STD energy-house preview, branched from MIRA7_OTA_20261120.
// Approved graphics are embedded real SVG files decoded by LVGL 9.6 / ThorVG.
#include "mira_energy_house.h"
#include "mira_full.h"
#include "mira_french_font.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "esp_log.h"

#define CLR_SOLAR 0x09DBECu
#define CLR_HOME  0x09DBECu
#define CLR_GRID  0x42C7EEu
#define CLR_BATT  0x62D6A7u
#define CLR_ECS   0xF6BF49u
#define CLR_EV    0x62D6A7u
#define CLR_WHITE 0xF3F6F7u
#define CLR_SOFT  0x88949Bu
#define CLR_BG    0x000000u
#define CLR_PANEL 0x09141Au

typedef struct {
    lv_point_precise_t p[8];
    uint8_t n;
    lv_obj_t *pulse[3];
    lv_obj_t *track;
    bool valid;
    bool dedicated; // A dedicated W/kW sensor overrides any old display value.
    float watts;
    float phase;
    uint32_t last_seen_ms;
    bool backwards;
} energy_route_t;

static lv_obj_t *scene, *artwork, *fields[7], *battery_shapes[3];
static lv_obj_t *solar_layer,*ev_layer,*garage_door;
static lv_obj_t *ecs_card,*ecs_shape,*ecs_temp,*ecs_power;
static lv_obj_t *ev_card,*ev_shape,*ev_soc,*ev_power;
static lv_obj_t *solar_card,*solar_shape,*batt_card;
static lv_obj_t *batt_extra[2], *battery_track[3];
static lv_obj_t *battery_cell_fill[3][4], *batt_status;
static lv_timer_t *animation_timer;
static energy_route_t routes[6];
static void (*back_cb)(void);
// -1: infer presence from a real Jeedom state (old plugin), 0: disabled
// in Jeedom, 1: explicitly selected in Jeedom.
static int option_solar=-1,option_battery=-1,option_ev=-1,option_ecs=-1;
static bool seen_solar,seen_battery,seen_ev,seen_ecs;
static bool has_value(const char *value) {
    return value && *value && strncmp(value,"--",2)!=0 &&
           strcmp(value,"null")!=0 && strcmp(value,"undefined")!=0;
}
static bool enabled(int option,bool seen) {
    return option<0 ? seen : option>0;
}
static void refresh_visibility(void);

static lv_obj_t *shape(lv_obj_t *p,int x,int y,int w,int h,uint32_t fill,
                       uint32_t border,int bw,int radius){
    lv_obj_t *o=lv_obj_create(p);
    lv_obj_set_pos(o,x,y);
    lv_obj_set_size(o,w,h);
    lv_obj_set_style_pad_all(o,0,0);
    lv_obj_set_style_radius(o,radius,0);
    lv_obj_set_style_bg_color(o,lv_color_hex(fill),0);
    lv_obj_set_style_bg_opa(o,fill==CLR_BG?LV_OPA_TRANSP:LV_OPA_COVER,0);
    lv_obj_set_style_border_color(o,lv_color_hex(border),0);
    lv_obj_set_style_border_width(o,bw,0);
    lv_obj_set_style_shadow_width(o,0,0);
    lv_obj_remove_flag(o,LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_CLICKABLE);
    return o;
}
static lv_obj_t *txt(lv_obj_t *p,const char *s,int x,int y,int w,uint32_t color,
                     const lv_font_t *font,lv_text_align_t align){
    lv_obj_t *o=lv_label_create(p);
    lv_obj_set_pos(o,x,y);lv_obj_set_width(o,w);
    lv_label_set_text(o,s);
    lv_label_set_long_mode(o,LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(o,lv_color_hex(color),0);
    lv_obj_set_style_text_font(o,font,0);
    lv_obj_set_style_text_align(o,align,0);
    lv_obj_remove_flag(o,LV_OBJ_FLAG_CLICKABLE);
    return o;
}
static lv_obj_t *card(int x,int y,int w,int h,const char *title,uint32_t tint){
    lv_obj_t *c=shape(artwork,x,y,w,h,CLR_PANEL,tint,1,9);
    txt(c,title,6,5,w-12,CLR_WHITE,&mira_font_fr_14,LV_TEXT_ALIGN_CENTER);
    return c;
}
static lv_obj_t *poly(lv_obj_t *p,const lv_point_precise_t *pts,int count,
                      uint32_t color,int width){
    lv_obj_t *o=lv_line_create(p);
    lv_line_set_points(o,pts,count);
    lv_obj_set_style_line_color(o,lv_color_hex(color),0);
    lv_obj_set_style_line_width(o,width,0);
    lv_obj_set_style_line_rounded(o,true,0);
    lv_obj_remove_flag(o,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
    return o;
}
// Approved house design on the 800x430 visible area of Mira 7 STD.
// The bottom 50 pixels belong exclusively to Mira's own navigation.
// Every geometry is a native LVGL vector primitive (not a baked-in bitmap).
#define POLY(name,...) static const lv_point_precise_t name[]={__VA_ARGS__}
#define STROKE(parent,color,width,...) do { \
    static const lv_point_precise_t pts[]={__VA_ARGS__}; \
    poly(parent,pts,(int)(sizeof(pts)/sizeof(pts[0])),color,width); \
} while(0)

// The graphics are exact rasterizations of the six approved SVG sources.
// An offline generator converts them to a compact I8 palette + RLE format.
// LVGL draws native images; SVG and ThorVG are never invoked on ESP32-S3.
#define IMAGE_EXTERN(name) \
    extern const uint8_t name##_begin[] asm("_binary_" #name "_i8r_start"); \
    extern const uint8_t name##_end[] asm("_binary_" #name "_i8r_end")
IMAGE_EXTERN(maison_fond);
IMAGE_EXTERN(pylone);
IMAGE_EXTERN(porte_garage);
IMAGE_EXTERN(panneaux_solaires);
IMAGE_EXTERN(ballon_ecs);
IMAGE_EXTERN(vehicule_electrique);
IMAGE_EXTERN(batterie_coque);
#undef IMAGE_EXTERN

typedef enum {SVG_HOUSE,SVG_PYLON,SVG_GARAGE_DOOR,SVG_SOLAR,SVG_ECS,SVG_EV,SVG_BATTERY,SVG_COUNT} mira_svg_id_t;
static lv_image_dsc_t svg_dsc[SVG_COUNT];
static uint8_t *svg_pixels[SVG_COUNT];

static void init_svg_assets(void){
    static bool initialized=false;
    if(initialized)return;
    initialized=true;
    const struct {const uint8_t *start,*end;uint16_t w,h;} assets[SVG_COUNT]={
        {maison_fond_begin,maison_fond_end,800,450},
        {pylone_begin,pylone_end,78,138},
        {porte_garage_begin,porte_garage_end,184,147},
        {panneaux_solaires_begin,panneaux_solaires_end,292,112},
        {ballon_ecs_begin,ballon_ecs_end,49,79},
        {vehicule_electrique_begin,vehicule_electrique_end,136,97},
        {batterie_coque_begin,batterie_coque_end,46,90},
    };
    for(unsigned i=0;i<SVG_COUNT;i++){
        const uint32_t pixels=(uint32_t)assets[i].w*assets[i].h;
        const size_t encoded_size=(size_t)(assets[i].end-assets[i].start);
        if(encoded_size<1024||((encoded_size-1024)&1u)){
            ESP_LOGE("mira-energy","Invalid RLE asset %u",i);
            continue;
        }
        const size_t output_size=1024u+(size_t)pixels;
        uint8_t *buf=lv_malloc(output_size); // LVGL custom allocator = PSRAM
        if(!buf){
            ESP_LOGE("mira-energy","Image %u allocation failed (%u bytes)",i,(unsigned)output_size);
            continue;  // Leave a blank asset rather than rebooting the panel.
        }
        memcpy(buf,assets[i].start,1024);
        uint32_t written=0;
        bool valid=true;
        for(const uint8_t *cur=assets[i].start+1024;cur<assets[i].end;cur+=2){
            const uint32_t run=cur[0];
            if(run==0||run>pixels-written){valid=false;break;}
            memset(buf+1024+written,cur[1],run);
            written+=run;
        }
        if(!valid||written!=pixels){
            lv_free(buf);
            ESP_LOGE("mira-energy","Corrupted RLE image %u (%u/%u pixels)",
                     i,(unsigned)written,(unsigned)pixels);
            continue;
        }
        svg_pixels[i]=buf;
        lv_image_dsc_t *d=&svg_dsc[i];
        memset(d,0,sizeof(*d));
        d->header.magic=LV_IMAGE_HEADER_MAGIC;
        d->header.cf=LV_COLOR_FORMAT_I8;
        d->header.w=assets[i].w;
        d->header.h=assets[i].h;
        d->header.stride=assets[i].w;
        d->data=buf;
        d->data_size=(uint32_t)output_size;
        ESP_LOGI("mira-energy","Native image %u decoded in PSRAM: %u bytes",
                 i,(unsigned)output_size);
    }
}
static lv_obj_t *svg_object(lv_obj_t *parent,mira_svg_id_t id,int x,int y){
    if(id>=SVG_COUNT||!svg_pixels[id])
        return shape(parent,x,y,1,1,CLR_BG,0,0,0);
    lv_obj_t *obj=lv_image_create(parent);
    lv_image_set_src(obj,&svg_dsc[id]);
    lv_obj_set_pos(obj,x,y);
    lv_obj_remove_flag(obj,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}
static void draw_background(void){
    init_svg_assets();
    // Original validated house, unchanged source SVG; the pylon pixels
    // are split offline into a separate image so the tower can be repositioned.
    (void)svg_object(artwork,SVG_HOUSE,0,0);
    // Approved Wave 7 preview: whole composition is offset -35px,
    // including the Réseau card. Its global center is x=110.5.
    // The 78px-wide pylon is placed at local x=108 (global x=73),
    // center x=112, immediately beneath the Réseau card.
    // Visible tower base originally ends at crop-local y=131;
    // moving the crop to y=260 aligns it with the house ground y=391.
    (void)svg_object(artwork,SVG_PYLON,108,260);
    // Independent door: visible only when EV module is absent.
    garage_door=svg_object(artwork,SVG_GARAGE_DOOR,598,250);
    // Independent panels, ECS cylinder upstairs and VE; controlled by Jeedom.
    // User-provided 800x450 SVG is rendered 1:1 and cropped to x240..532,
    // y68..180 at build time. Placing this native crop at (240,68) preserves
    // every original point in the house's shared coordinate space.
    solar_layer=svg_object(artwork,SVG_SOLAR,240,68);
    solar_shape=solar_layer;
    ecs_shape=svg_object(artwork,SVG_ECS,549,171);
    ev_layer=svg_object(artwork,SVG_EV,633,300);
    ev_shape=ev_layer;
}

// Five energy branches converge at the HOUSE junction (415,241), drawn
// from the user's annotated Wave 7 photo. Artwork itself is offset -35px.
// No extra home-power branch: the Maison card is already the central sink.
// Distinct accent colors below follow the user's colored route sketch.
// The 8-point network route stays under Maison's card and skirts the meter.
POLY(route_solar,{400,173},{415,190},{415,241});
POLY(route_grid,{415,241},{415,280},{395,283},{245,283},
                {225,304},{225,350},{186,350},{186,323});
POLY(route_battery,{415,241},{429,264},{429,291},{455,304});
POLY(route_ecs,{415,241},{456,241},{456,211},{548,211});
POLY(route_ev,{415,241},{435,268},{599,268},{609,327},{634,327});
// A route owns a stable point buffer plus its LVGL line and three animated dashes.
// This native helper is required by routes_create() after the SVG transition.
static void set_route(int i,const lv_point_precise_t *p,unsigned n,uint32_t color){
    if(i<0||i>=6||!p)return;
    energy_route_t *r=&routes[i];
    if(n>8)n=8;
    r->n=(uint8_t)n;
    for(unsigned j=0;j<n;j++)r->p[j]=p[j];
    r->track=poly(artwork,r->p,(int)n,color,2);
    lv_obj_set_style_line_opa(r->track,LV_OPA_50,0);
    for(unsigned j=0;j<3;j++){
        r->pulse[j]=shape(artwork,0,0,9,3,color,0,0,LV_RADIUS_CIRCLE);
        lv_obj_add_flag(r->pulse[j],LV_OBJ_FLAG_HIDDEN);
    }
}
static void routes_create(void){
    // The center is a common point, NOT a separate animated Maison route.
    // route[1] remains uninitialized on purpose: the Jeedom home-power
    // value can still be received without drawing a redundant sixth flow.
    set_route(0,route_solar,3,0x28C859u);   // green: rooftop -> house
    set_route(2,route_grid,8,0x318BFAu);    // blue: EDF, bidirectional
    set_route(3,route_battery,4,0xAE43EAu); // violet: charging/discharging
    set_route(4,route_ecs,4,0xF79031u);     // orange: hot-water heater
    set_route(5,route_ev,5,0xDE2984u);      // pink: car charger
    // Small neutral junction, shared by the five independent branches.
    shape(artwork,412,238,6,6,0x42C7EEu,0,0,LV_RADIUS_CIRCLE);
}
static void set_visibility(lv_obj_t *object,bool show){
    if(!object)return;
    if(show)lv_obj_remove_flag(object,LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(object,LV_OBJ_FLAG_HIDDEN);
}
// Inputs for moving flows are instantaneous signed W/kW ONLY.
// kWh/Wh energy totals must never be interpreted as power or cause movement.
// Dedicated __*_power infos override the old __solar/__home/... values.
static void set_power_valid(unsigned route,const char *text,bool dedicated){
    if(route>=6)return;
    energy_route_t *r=&routes[route];
    if(dedicated)r->dedicated=true;
    if(!dedicated&&r->dedicated)return;
    r->valid=false;
    r->watts=0.f;
    if(!has_value(text))return;
    char scratch[64];
    snprintf(scratch,sizeof(scratch),"%s",text);
    for(char *p=scratch;*p;p++)if(*p==',')*p='.';
    char *end=NULL;
    float v=strtof(scratch,&end);
    if(end==scratch||!isfinite(v))return;
    while(*end==' '||*end=='\t')end++;
    bool kw=false;
    if((*end=='k'||*end=='K')&&(end[1]=='W'||end[1]=='w')){
        kw=true;end+=2;
    }else if(*end=='W'||*end=='w'){
        end++;
    }else return; // Missing unit, Wh, kWh, %, volts... -> not a power.
    while(*end==' '||*end=='\t')end++;
    if(*end!='\0')return;
    if(kw)v*=1000.f;
    if(!isfinite(v)||fabsf(v)>50000.f)return;
    if(route!=2&&route!=3&&v<0.f)return;
    r->valid=true;
    r->watts=v;
    r->last_seen_ms=lv_tick_get();
    // Grid +W = import -> home; -W = export -> pylon.
    // Battery +W = charging (home -> pack); -W = discharging.
    r->backwards=(route==2&&v>0.f)||(route==3&&v<0.f);
}
static bool route_is_live(const energy_route_t *r){
    return r->valid&&fabsf(r->watts)>0.5f&&
           ((uint32_t)(lv_tick_get()-r->last_seen_ms)<180000u);
}
static void animate(lv_timer_t *timer){
    (void)timer;
    static uint32_t previous=0;
    const uint32_t now=lv_tick_get();
    uint32_t elapsed=previous?now-previous:90u;
    previous=now;
    if(elapsed>250u)elapsed=250u;
    if(!scene||lv_obj_has_flag(scene,LV_OBJ_FLAG_HIDDEN))return;
    for(unsigned k=0;k<6;k++){
        energy_route_t *r=&routes[k];
        const bool allowed=(k!=0||enabled(option_solar,seen_solar))&&
            (k!=3||enabled(option_battery,seen_battery))&&
            (k!=4||enabled(option_ecs,seen_ecs))&&
            (k!=5||enabled(option_ev,seen_ev));
        const bool live=allowed&&route_is_live(r)&&r->n>=2&&r->pulse[0];
        set_visibility(r->track,live);
        if(!live){
            for(unsigned dot=0;dot<3;dot++)set_visibility(r->pulse[dot],false);
            continue;
        }
        float distance=0.f;
        for(unsigned j=1;j<r->n;j++){
            const float dx=(float)(r->p[j].x-r->p[j-1].x);
            const float dy=(float)(r->p[j].y-r->p[j-1].y);
            distance+=sqrtf(dx*dx+dy*dy);
        }
        if(distance<1.f)continue;
        // Dash motion proportional to MEASURED instantaneous watts.
        // No motion from accumulated kWh, and no idle "decorative" speed.
        const float watts=fabsf(r->watts);
        const float pixels_per_second=fminf(75.f,10.f+watts*0.014f);
        r->phase+=pixels_per_second*((float)elapsed/1000.f)/distance;
        while(r->phase>=1.f)r->phase-=1.f;
        for(unsigned dot=0;dot<3;dot++){
            float progress=r->phase+(float)dot/3.f;
            if(progress>=1.f)progress-=1.f;
            float target=(r->backwards?1.f-progress:progress)*distance;
            float x=(float)r->p[0].x,y=(float)r->p[0].y;
            for(unsigned j=1;j<r->n;j++){
                const float dx=(float)(r->p[j].x-r->p[j-1].x);
                const float dy=(float)(r->p[j].y-r->p[j-1].y);
                const float len=sqrtf(dx*dx+dy*dy);
                if(target<=len||j==r->n-1){
                    const float t=len>0.f?fminf(1.f,target/len):0.f;
                    x=(float)r->p[j-1].x+t*dx;
                    y=(float)r->p[j-1].y+t*dy;
                    break;
                }
                target-=len;
            }
            lv_obj_set_pos(r->pulse[dot],(int)x-4,(int)y-1);
            set_visibility(r->pulse[dot],true);
        }
    }
}
static void return_home(lv_event_t *ev){
    (void)ev;
    if(back_cb)back_cb();
}
static lv_obj_t *batt_box(unsigned i,int x){
    // Preserve approved SVG artwork, extend its body DOWN to reach the floor.
    // Four SOC bars and the percentage remain inside the 46x90 shell.
    lv_obj_t *b=shape(artwork,x,301,46,93,CLR_BG,0,0,0);
    (void)svg_object(b,SVG_BATTERY,0,0);
    for(int n=0;n<4;n++){
        battery_cell_fill[i][n]=shape(b,10,46+n*8,26,6,0x6AF900u,0,0,2);
        lv_obj_add_flag(battery_cell_fill[i][n],LV_OBJ_FLAG_HIDDEN);
    }
    battery_track[i]=b;
    return b;
}
static void update_battery_soc(unsigned battery,const char *text){
    if(battery>=3)return;
    char number[32];
    snprintf(number,sizeof(number),"%s",text?text:"");
    for(char *p=number;*p;p++)if(*p==',')*p='.';
    char *end=NULL;
    const float percentage=strtof(number,&end);
    const bool valid=has_value(text)&&end!=number&&isfinite(percentage);
    const float pct=valid?fmaxf(0.f,fminf(100.f,percentage)):0.f;
    const uint32_t color=pct<=10.f?0xE35B64u:
        (pct<=20.f?0xEAA048u:0x62D6A7u);
    for(unsigned cell=0;cell<4;cell++){
        lv_obj_t *fill=battery_cell_fill[battery][cell];
        if(!fill)continue;
        // Cell zero is at the TOP: fill from the BOTTOM as SOC rises.
        // 0%: none, 25%: bottom, 50%: 2, 75%: 3, 100%: 4.
        // Values between steps fill the next bar proportionally.
        const float step_from_bottom=(float)(3u-cell);
        const float fraction=fmaxf(0.f,fminf(1.f,(pct-25.f*step_from_bottom)/25.f));
        const int width=(int)roundf(26.f*fraction);
        set_visibility(fill,width>0);
        if(width>0)lv_obj_set_width(fill,width);
        lv_obj_set_style_bg_color(fill,lv_color_hex(color),0);
    }
}
lv_obj_t *mira_energy_house_create(lv_obj_t *page,void (*on_back)(void)){
    mira_energy_house_destroy();
    back_cb=on_back;
    // PAGE 4 owns the full scene; it never overlays root navigation or picker.
    // Unlike an ordinary flex card, the 800x430 canvas must cover the whole
    // PAGE viewport. Page 4 has 6px left / 8px top content padding: LVGL
    // positions child objects relative to that content origin. Compensate
    // those insets once so the 786x414 energy frame is really at (7,8),
    // just like the centered thermostat card, with no right/bottom clipping.
    scene=shape(page,0,0,800,430,0x071421u,0,0,0);
    lv_obj_add_flag(scene,LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_pos(scene,
        -lv_obj_get_style_pad_left(page,LV_PART_MAIN),
        -lv_obj_get_style_pad_top(page,LV_PART_MAIN));
    lv_obj_set_style_pad_all(scene,0,0);
    // Use EXACT same theme-aware background, border and relief as thermostat.
    lv_obj_t *thermostat_style_card=shape(scene,7,8,786,414,0x0E2032u,0x17354Bu,1,14);
    mira_full_style_energy_panel(scene,thermostat_style_card);
    // Move the entire validated house composition left by only 35 pixels.
    // The approved house and roof-aligned solar SVG are not rescaled.
    // The Réseau card moves with the house; paths use the same local origin.
    artwork=shape(scene,-35,0,800,430,CLR_BG,0,0,0);
    lv_obj_add_flag(artwork,LV_OBJ_FLAG_IGNORE_LAYOUT);
    draw_background();
    routes_create();
    solar_card=card(239,20,179,63,"Panneaux",CLR_SOLAR);
    fields[0]=txt(solar_card,"--",7,31,165,CLR_SOLAR,&lv_font_montserrat_20,LV_TEXT_ALIGN_CENTER);
    // Réseau follows the house by -35px, just like EVERY other card.
    // Its global x is 54, centered above the pylon (center x=112).
    lv_obj_t *grid=card(89,178,113,69,"Réseau",CLR_GRID);
    fields[2]=txt(grid,"--",4,29,105,CLR_GRID,&lv_font_montserrat_20,LV_TEXT_ALIGN_CENTER);
    lv_obj_t *home=card(251,209,144,61,"Maison",CLR_HOME);
    fields[1]=txt(home,"--",5,28,134,CLR_HOME,&lv_font_montserrat_20,LV_TEXT_ALIGN_CENTER);
    ecs_card=card(513,88,115,64,"ECS",CLR_ECS);
    ecs_temp=txt(ecs_card,"--",5,25,105,CLR_ECS,&mira_font_fr_16,LV_TEXT_ALIGN_CENTER);
    ecs_power=txt(ecs_card,"--",5,44,105,CLR_WHITE,&mira_font_fr_12,LV_TEXT_ALIGN_CENTER);
    ev_card=card(619,154,147,73,"Véhicule",CLR_EV);
    ev_soc=txt(ev_card,"--",6,26,135,CLR_EV,&lv_font_montserrat_20,LV_TEXT_ALIGN_CENTER);
    ev_power=txt(ev_card,"--",6,50,135,CLR_WHITE,&mira_font_fr_12,LV_TEXT_ALIGN_CENTER);
    batt_card=card(338,308,105,65,"Batterie",CLR_BATT);
    fields[3]=txt(batt_card,"--",5,26,95,CLR_BATT,&mira_font_fr_16,LV_TEXT_ALIGN_CENTER);
    batt_status=txt(batt_card,"Charge / déch.",2,48,101,CLR_WHITE,&mira_font_fr_12,LV_TEXT_ALIGN_CENTER);
    for(int i=0;i<3;i++){
        battery_shapes[i]=batt_box((unsigned)i,455+i*47);
        fields[4+i]=txt(battery_shapes[i],"--",2,23,42,CLR_WHITE,&mira_font_fr_12,LV_TEXT_ALIGN_CENTER);
        if(i)lv_obj_add_flag(battery_shapes[i],LV_OBJ_FLAG_HIDDEN);
    }
    // Invisible touch target on unused black space, never a visible menu.
    lv_obj_t *return_zone=lv_obj_create(scene);
    lv_obj_set_pos(return_zone,0,0);
    lv_obj_set_size(return_zone,64,65);
    lv_obj_set_style_bg_opa(return_zone,LV_OPA_TRANSP,0);
    lv_obj_set_style_border_width(return_zone,0,0);
    lv_obj_set_style_shadow_width(return_zone,0,0);
    lv_obj_set_style_pad_all(return_zone,0,0);
    lv_obj_remove_flag(return_zone,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(return_zone,return_home,LV_EVENT_CLICKED,NULL);
    animation_timer=lv_timer_create(animate,90,NULL);
    lv_obj_add_flag(scene,LV_OBJ_FLAG_HIDDEN);
    mira_energy_house_options(-1,-1,-1,-1);
    return scene;
}
void mira_energy_house_destroy(void){
    if(animation_timer){lv_timer_delete(animation_timer);animation_timer=NULL;}
    if(scene){lv_obj_delete(scene);scene=NULL;}
    artwork=NULL;
    solar_layer=NULL;ev_layer=NULL;garage_door=NULL;
    seen_solar=false;seen_battery=false;seen_ecs=false;seen_ev=false;
    option_solar=-1;option_battery=-1;option_ev=-1;option_ecs=-1;
    memset(fields,0,sizeof(fields));
    memset(routes,0,sizeof(routes));
    memset(battery_shapes,0,sizeof(battery_shapes));
    memset(battery_track,0,sizeof(battery_track));
    memset(battery_cell_fill,0,sizeof(battery_cell_fill));
    batt_status=NULL;
    ecs_card=NULL;ecs_shape=NULL;ecs_temp=NULL;ecs_power=NULL;
    ev_card=NULL;ev_shape=NULL;ev_soc=NULL;ev_power=NULL;
    batt_card=NULL;solar_card=NULL;solar_shape=NULL;
    memset(batt_extra,0,sizeof(batt_extra));
}
bool mira_energy_house_exists(void){return scene!=NULL;}
void mira_energy_house_show(bool show){
    if(!scene)return;
    if(show)lv_obj_move_foreground(scene);
    set_visibility(scene,show);
}
lv_obj_t *mira_energy_house_field(unsigned i){return i<7?fields[i]:NULL;}
void mira_energy_house_update(unsigned field,const char *value){
    if(!scene||field>=7||!fields[field])return;
    const char *new_value=(value&&*value)?value:"--";
    const char *old=lv_label_get_text(fields[field]);
    if(!old||strcmp(old,new_value))lv_label_set_text(fields[field],new_value);
    if(field<4)set_power_valid(field,new_value,false);
    if(field>=4)update_battery_soc(field-4,new_value);
    if(field==3&&batt_status){
        const bool cumulative=strstr(new_value,"Wh")||strstr(new_value,"wh");
        lv_label_set_text(batt_status,cumulative?"Énergie":
            !routes[3].valid?"Charge / déch.":
            routes[3].backwards?"Décharge":"Charge");
    }
    if(field==0)seen_solar=has_value(new_value);
    if(field==3||field==4||field==5||field==6){
        seen_battery=has_value(lv_label_get_text(fields[3]))||
            has_value(lv_label_get_text(fields[4]))||
            has_value(lv_label_get_text(fields[5]))||
            has_value(lv_label_get_text(fields[6]));
    }
    refresh_visibility();
}
void mira_energy_house_aux(const char *name,const char *value){
    if(!scene||!name)return;
    lv_obj_t *field=NULL;
    if(!strcmp(name,"ecs_temp"))field=ecs_temp;
    // ECS/VE display lines may be kWh totals. They remain visible, but
    // only parse as a legacy W fallback for flow animation.
    // Dedicated __ecs_instant_power / __ev_instant_power (W) override them
    // without replacing those user-configured labels and values.
    else if(!strcmp(name,"ecs_power")){field=ecs_power;set_power_valid(4,value,false);}
    else if(!strcmp(name,"ev_soc"))field=ev_soc;
    else if(!strcmp(name,"ev_power")){field=ev_power;set_power_valid(5,value,false);}
    else if(!strcmp(name,"ecs_instant_power"))set_power_valid(4,value,true);
    else if(!strcmp(name,"ev_instant_power"))set_power_valid(5,value,true);
    else if(!strcmp(name,"solar_power"))set_power_valid(0,value,true);
    else if(!strcmp(name,"home_power"))set_power_valid(1,value,true);
    else if(!strcmp(name,"grid_power"))set_power_valid(2,value,true);
    else if(!strcmp(name,"battery_power"))set_power_valid(3,value,true);
    if(field)lv_label_set_text(field,value&&*value?value:"--");
    if(!strncmp(name,"ecs_",4))seen_ecs=has_value(lv_label_get_text(ecs_temp))||has_value(lv_label_get_text(ecs_power));
    if(!strncmp(name,"ev_",3))seen_ev=has_value(lv_label_get_text(ev_soc))||has_value(lv_label_get_text(ev_power));
    refresh_visibility();
}
static void refresh_visibility(void){
    if(!scene)return;
    const bool solar=enabled(option_solar,seen_solar);
    const bool batt=enabled(option_battery,seen_battery);
    const bool ev=enabled(option_ev,seen_ev);
    const bool ecs=enabled(option_ecs,seen_ecs);
    set_visibility(solar_card,solar);
    set_visibility(solar_layer,solar);
    set_visibility(batt_card,batt);
    for(int i=0;i<3;i++){
        const bool populated=(i==0)||
            (fields[4+i] && has_value(lv_label_get_text(fields[4+i])));
        set_visibility(battery_shapes[i],batt&&populated);
    }
    set_visibility(ecs_card,ecs);
    set_visibility(ecs_shape,ecs);
    set_visibility(ev_card,ev);
    set_visibility(ev_layer,ev);
    // Mutually exclusive door and EV: garage walls remain visible in both cases.
    set_visibility(garage_door,!ev);
    // Hide the whole cable as well as moving pixels for absent equipment.
    for(int i=0;i<6;i++){
        const bool on=i==0?solar:i==3?batt:i==4?ecs:i==5?ev:true;
        const bool live=on&&route_is_live(&routes[i]);
        set_visibility(routes[i].track,live);
        if(!live)for(unsigned j=0;j<3;j++)set_visibility(routes[i].pulse[j],false);
    }
}
void mira_energy_house_options(int solar,int battery,int ev,int ecs){
    option_solar=solar;option_battery=battery;option_ev=ev;option_ecs=ecs;
    refresh_visibility();
}
