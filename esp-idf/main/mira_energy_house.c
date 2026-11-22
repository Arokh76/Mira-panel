// Native Mira 7 STD energy-house preview, branched from MIRA7_OTA_20261120.
// Built entirely from LVGL primitives (no image assets, no HTTP/MQTT changes).
#include "mira_energy_house.h"
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
    bool cumulative; // kWh is energy, not instantaneous power.
    float watts;
    float phase;
    bool backwards;
} energy_route_t;

static lv_obj_t *scene, *artwork, *fields[7], *battery_shapes[3];
static lv_obj_t *solar_layer,*ev_layer,*garage_door;
static lv_obj_t *ecs_card,*ecs_shape,*ecs_temp,*ecs_power;
static lv_obj_t *ev_card,*ev_shape,*ev_soc,*ev_power;
static lv_obj_t *solar_card,*solar_shape,*batt_card;
static lv_obj_t *batt_extra[2], *battery_track[3];
static lv_obj_t *battery_cell_fill[3][3], *batt_status;
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

static void draw_background(void){
    const uint32_t cyan=0x08DFE9u;
    const uint32_t white=0xF2F6F7u;
    const uint32_t silver=0xBECBD1u;
    const uint32_t dark=0x0C151Bu;

    // 1. APPROVED SHELL. Pylon detached on the far left; meter on wall.
    // Two roof edges form the two-plane gable seen in the approved drawing.
    STROKE(artwork,white,4,{218,171},{305,91},{545,54},{667,172});
    STROKE(artwork,white,4,{218,171},{428,171},{545,54});
    STROKE(artwork,white,4,{665,170},{635,170},{635,224});
    STROKE(artwork,white,4,{244,171},{244,386});
    STROKE(artwork,white,4,{220,386},{788,386});
    // Chimney: no illumination or ambient glow.
    STROKE(artwork,white,4,{576,86},{576,45},{608,45},{608,112});

    // 2. PERMANENT INTERIOR: cyan upper floor and furnished living area.
    STROKE(artwork,cyan,3,{244,266},{594,266});
    STROKE(artwork,cyan,3,{265,266},{265,385});
    STROKE(artwork,cyan,3,{412,266},{412,385});
    // Upstairs utility room: angled ceiling and cyan partition.
    STROKE(artwork,cyan,3,{440,266},{440,170},{485,126},{571,126},{622,170},{622,224});
    // Two-door storage cabinet and under-counter linen shelf (static).
    STROKE(artwork,white,2,{459,165},{524,165},{524,201},{459,201},{459,165});
    STROKE(artwork,white,1,{491,165},{491,201});
    shape(artwork,486,182,2,9,white,0,0,1);
    shape(artwork,495,182,2,9,white,0,0,1);
    STROKE(artwork,white,2,{459,231},{530,231});
    STROKE(artwork,white,2,{463,231},{463,263});
    STROKE(artwork,white,2,{525,231},{525,263});
    for(int n=0;n<2;n++)shape(artwork,469,217+n*6,23,5,CLR_BG,white,1,3);
    shape(artwork,507,212,10,18,CLR_BG,white,1,2); // Soap bottle.
    shape(artwork,507,208,9,4,white,0,0,1);
    shape(artwork,478,242,33,19,CLR_BG,white,1,1); // Laundry basket.
    STROKE(artwork,white,1,{481,245},{485,258},{491,245},{495,258},{501,245},{505,258});

    // White-outline sofa + non-illuminated pendant light at ground floor.
    shape(artwork,284,330,99,38,CLR_BG,white,2,10);
    shape(artwork,279,344,15,29,CLR_BG,white,2,7);
    shape(artwork,376,344,15,29,CLR_BG,white,2,7);
    STROKE(artwork,white,2,{289,354},{380,354});
    STROKE(artwork,white,2,{292,373},{291,383});
    STROKE(artwork,white,2,{382,373},{384,383});
    STROKE(artwork,white,2,{330,267},{330,304});
    STROKE(artwork,white,2,{314,311},{319,303},{341,303},{346,311},{314,311});
    // Plant on a small stool. Leaves remain cyan, no light/glow.
    STROKE(artwork,white,2,{389,357},{416,357});
    STROKE(artwork,white,2,{394,358},{391,384});
    STROKE(artwork,white,2,{412,358},{415,384});
    shape(artwork,395,343,15,14,white,0,0,2);
    STROKE(artwork,cyan,2,{402,344},{402,320});
    STROKE(artwork,cyan,2,{402,335},{391,329},{390,319},{402,324});
    STROKE(artwork,cyan,2,{402,329},{413,317},{417,313},{416,329},{402,338});

    // 3. GARAGE FRAME IS PERMANENT. Door is a DISTINCT object layer.
    STROKE(artwork,white,4,{594,226},{781,226});
    STROKE(artwork,white,4,{594,226},{594,386});
    STROKE(artwork,white,4,{781,226},{781,386});
    garage_door=shape(artwork,0,0,800,430,CLR_BG,0,0,0);
    STROKE(garage_door,cyan,3,{612,252},{766,252},{766,385},{612,385},{612,252});
    STROKE(garage_door,cyan,2,{612,281},{766,281});
    STROKE(garage_door,cyan,2,{612,314},{766,314});
    STROKE(garage_door,cyan,2,{612,349},{766,349});

    // 4. EDF pylon and EXTERIOR METER: intentionally NO solid wire.
    STROKE(artwork,white,3,{27,242},{53,132},{81,242});
    STROKE(artwork,white,3,{25,241},{83,241});
    STROKE(artwork,white,3,{20,175},{55,155},{91,175},{91,185});
    STROKE(artwork,white,3,{20,175},{20,185});
    STROKE(artwork,white,3,{23,213},{55,192},{88,213},{88,222});
    STROKE(artwork,white,3,{23,213},{23,222});
    STROKE(artwork,white,2,{37,207},{72,234},{38,237},{72,207},{44,187},{66,171},{44,171},{66,187});
    STROKE(artwork,white,2,{41,156},{64,156},{43,177},{65,200});
    STROKE(artwork,white,2,{68,157},{43,182});
    // Meter box sits directly against the LEFT house wall (x244).
    shape(artwork,209,258,32,49,dark,white,2,6);
    shape(artwork,214,263,22,10,0x4B5C65,silver,1,2);
    STROKE(artwork,0xFDE700u,3,{224,278},{217,290},{225,290},{221,302},{232,285},{223,285});
    shape(artwork,217,308,18,3,silver,0,0,2);

    // 5. SOLAR is detachable from the roof: no panels when Jeedom disables it.
    solar_layer=shape(artwork,0,0,800,430,CLR_BG,0,0,0);
    solar_shape=solar_layer;
    STROKE(solar_layer,white,3,{256,154},{315,105},{491,82},{426,153},{256,154});
    STROKE(solar_layer,cyan,2,{260,151},{317,108},{486,85},{423,149},{260,151});
    STROKE(solar_layer,white,1,{300,120},{470,102});
    STROKE(solar_layer,white,1,{286,133},{454,120});
    STROKE(solar_layer,white,1,{273,144},{438,135});
    STROKE(solar_layer,white,1,{350,102},{294,151});
    STROKE(solar_layer,white,1,{384,98},{326,151});
    STROKE(solar_layer,white,1,{419,94},{358,150});
    STROKE(solar_layer,white,1,{454,90},{392,150});

    // 6. ECS is a removable, independent object INSIDE upstairs utility room.
    ecs_shape=shape(artwork,539,182,47,81,CLR_BG,0,0,0);
    shape(ecs_shape,4,3,39,67,0xDDE5E8u,white,2,16);
    shape(ecs_shape,8,5,31,5,0xEFF5F6u,0,0,3);
    shape(ecs_shape,8,63,31,6,0x91A9B1u,0,0,5);
    shape(ecs_shape,21,70,4,7,silver,0,0,2);
    shape(ecs_shape,12,76,22,3,silver,0,0,1);
    shape(ecs_shape,23,21,3,22,0xF04439u,0,0,2);
    shape(ecs_shape,19,39,11,11,0xE84032u,0,0,LV_RADIUS_CIRCLE);

    // 7. EV replaces the whole garage-door layer, without hiding garage walls.
    ev_layer=shape(artwork,0,0,800,430,CLR_BG,0,0,0);
    ev_shape=ev_layer;
    STROKE(ev_layer,white,3,{635,365},{638,342},{651,326},{671,319},{714,319},
           {735,330},{743,345},{747,369},{740,376},{641,376},{635,365});
    STROKE(ev_layer,white,2,{651,335},{663,324},{720,324},{731,337},{651,337});
    shape(ev_layer,648,347,22,5,cyan,0,0,3);
    shape(ev_layer,714,347,22,5,cyan,0,0,3);
    STROKE(ev_layer,white,2,{653,364},{674,366},{711,366},{730,364});
    shape(ev_layer,645,376,19,7,white,0,0,3);
    shape(ev_layer,718,376,19,7,white,0,0,3);
    STROKE(ev_layer,0xDBF414u,3,{694,344},{686,356},{696,356},{690,369},{704,350},{693,350});
}
static void set_route(int i,const lv_point_precise_t *p,unsigned n,uint32_t color){
    energy_route_t *r=&routes[i];
    if(n>8)n=8;
    r->n=(uint8_t)n;
    for(unsigned j=0;j<n;j++)r->p[j]=p[j];
    r->track=poly(artwork,r->p,(int)n,color,2);
    lv_obj_set_style_line_opa(r->track,LV_OPA_50,0);
    // Three short, spaced dashes rather than one large traveling dot.
    for(unsigned j=0;j<3;j++){
        r->pulse[j]=shape(artwork,0,0,9,3,color,0,0,LV_RADIUS_CIRCLE);
        lv_obj_add_flag(r->pulse[j],LV_OBJ_FLAG_HIDDEN);
    }
}
POLY(route_solar,{359,105},{359,157},{454,194},{454,281});
POLY(route_home,{454,281},{309,281});
POLY(route_grid,{454,281},{241,281},{224,282},{116,282},{55,282},{55,245});
POLY(route_battery,{454,281},{454,307},{488,307});
POLY(route_ecs,{454,281},{520,281},{563,261});
POLY(route_ev,{454,281},{598,281},{598,345},{640,345});
static void routes_create(void){
    set_route(0,route_solar,4,0x8EC740u);
    set_route(1,route_home,2,0x42C7EEu);
    set_route(2,route_grid,4,0x42C7EEu);
    set_route(3,route_battery,3,0x55DAABu);
    set_route(4,route_ecs,4,0xEAA048u);
    set_route(5,route_ev,4,0x62D6A7u);
    // Keep the central junction small, above the home/battery path.
    shape(artwork,451,278,6,6,0x42C7EEu,0,0,LV_RADIUS_CIRCLE);
}
static void set_visibility(lv_obj_t *object,bool show){
    if(!object)return;
    if(show)lv_obj_remove_flag(object,LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(object,LV_OBJ_FLAG_HIDDEN);
}
static void set_power_valid(unsigned route,const char *text){
    if(route>=6)return;
    energy_route_t *r=&routes[route];
    if(!text||!*text||!strcmp(text,"--")){
        r->valid=false;
        for(unsigned j=0;j<3;j++)set_visibility(r->pulse[j],false);
        return;
    }
    char scratch[64];
    snprintf(scratch,sizeof(scratch),"%s",text);
    for(char *p=scratch;*p;p++)if(*p==',')*p='.';
    char *end=NULL;
    float v=strtof(scratch,&end);
    if(end==scratch||!isfinite(v)){
        r->valid=false;
        for(unsigned j=0;j<3;j++)set_visibility(r->pulse[j],false);
        return;
    }
    const bool cumulative=strstr(scratch,"Wh")!=NULL||strstr(scratch,"wh")!=NULL;
    if(!cumulative&&(strstr(scratch,"kW")||strstr(scratch,"kw")))v*=1000.f;
    r->valid=true;
    r->cumulative=cumulative;
    r->watts=v;
    r->backwards=(route==2&&v>0.f)||(route==3&&v<0.f);
    if(route==2)r->backwards=v>0.f; // positive=import, negative=export
    if(route==3)r->backwards=v<0.f; // negative=discharge
}
static void animate(lv_timer_t *timer){
    (void)timer;
    if(!scene||lv_obj_has_flag(scene,LV_OBJ_FLAG_HIDDEN))return;
    for(int k=0;k<6;k++){
        energy_route_t *r=&routes[k];
        const bool available=r->valid&&fabsf(r->watts)>=0.01f&&r->n>=2&&r->pulse[0];
        const bool enabled_route=(k!=0||enabled(option_solar,seen_solar))&&
            (k!=3||enabled(option_battery,seen_battery))&&
            (k!=4||enabled(option_ecs,seen_ecs))&&
            (k!=5||enabled(option_ev,seen_ev));
        if(!available||!enabled_route){
            for(unsigned dot=0;dot<3;dot++)set_visibility(r->pulse[dot],false);
            continue;
        }
        float dist=0.f;
        for(unsigned j=1;j<r->n;j++){
            const float dx=(float)(r->p[j].x-r->p[j-1].x);
            const float dy=(float)(r->p[j].y-r->p[j-1].y);
            dist+=sqrtf(dx*dx+dy*dy);
        }
        if(dist<1.f)continue;
        // Energy in kWh uses a neutral speed, not a fictitious kW rating.
        const float speed=r->cumulative?0.022f:
            (0.018f+fminf(fabsf(r->watts)/9000.f,0.045f));
        r->phase+=speed;
        if(r->phase>=1.f)r->phase-=1.f;
        for(unsigned dot=0;dot<3;dot++){
            float progress=r->phase+(float)dot/3.f;
            if(progress>=1.f)progress-=1.f;
            float target=(r->backwards?1.f-progress:progress)*dist;
            float x=(float)r->p[0].x,y=(float)r->p[0].y;
            for(unsigned j=1;j<r->n;j++){
                const float dx=(float)(r->p[j].x-r->p[j-1].x);
                const float dy=(float)(r->p[j].y-r->p[j-1].y);
                const float len=sqrtf(dx*dx+dy*dy);
                if(target<=len||j==r->n-1){
                    const float t=len>0.f?target/len:0.f;
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
    // Three independent vector battery icons in the lower service area.
    lv_obj_t *b=shape(artwork,x,307,34,76,0x101C21u,CLR_WHITE,2,6);
    shape(b,10,3,14,4,0x93A6AAu,0,0,2);
    for(int n=0;n<3;n++){
        shape(b,6,13+n*12,22,9,0x23363Fu,0x46636Bu,1,2);
        battery_cell_fill[i][n]=shape(b,7,14+n*12,20,7,0x62D6A7u,0,0,2);
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
    for(unsigned cell=0;cell<3;cell++){
        lv_obj_t *fill=battery_cell_fill[battery][cell];
        if(!fill)continue;
        const float fraction=fmaxf(0.f,fminf(1.f,(pct-(100.f/3.f)*(float)cell)/(100.f/3.f)));
        const int width=(int)roundf(20.f*fraction);
        set_visibility(fill,width>0);
        if(width>0)lv_obj_set_width(fill,width);
        lv_obj_set_style_bg_color(fill,lv_color_hex(color),0);
    }
}
lv_obj_t *mira_energy_house_create(lv_obj_t *screen,void (*on_back)(void)){
    mira_energy_house_destroy();
    back_cb=on_back;
    // Native vector viewport is 800x430, leaving bottom 50px to Mira navigation.
    scene=shape(screen,0,0,800,430,0x000001u,0,0,0);
    lv_obj_set_style_bg_color(scene,lv_color_hex(CLR_BG),0);
    lv_obj_set_style_bg_opa(scene,LV_OPA_COVER,0);
    lv_obj_add_flag(scene,LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_style_pad_all(scene,0,0);
    artwork=shape(scene,0,0,800,430,CLR_BG,0,0,0);
    lv_obj_add_flag(artwork,LV_OBJ_FLAG_IGNORE_LAYOUT);
    draw_background();
    routes_create();
    solar_card=card(239,3,179,63,"Panneaux",CLR_SOLAR);
    fields[0]=txt(solar_card,"--",7,31,165,CLR_SOLAR,&lv_font_montserrat_20,LV_TEXT_ALIGN_CENTER);
    lv_obj_t *grid=card(89,178,113,69,"Réseau",CLR_GRID);
    fields[2]=txt(grid,"--",4,29,105,CLR_GRID,&lv_font_montserrat_20,LV_TEXT_ALIGN_CENTER);
    lv_obj_t *home=card(251,209,144,61,"Maison",CLR_HOME);
    fields[1]=txt(home,"--",5,28,134,CLR_HOME,&lv_font_montserrat_20,LV_TEXT_ALIGN_CENTER);
    ecs_card=card(513,114,115,64,"ECS",CLR_ECS);
    ecs_temp=txt(ecs_card,"--",5,25,105,CLR_ECS,&mira_font_fr_16,LV_TEXT_ALIGN_CENTER);
    ecs_power=txt(ecs_card,"--",5,44,105,CLR_WHITE,&mira_font_fr_12,LV_TEXT_ALIGN_CENTER);
    ev_card=card(644,154,147,73,"Véhicule",CLR_EV);
    ev_soc=txt(ev_card,"--",6,26,135,CLR_EV,&lv_font_montserrat_20,LV_TEXT_ALIGN_CENTER);
    ev_power=txt(ev_card,"--",6,50,135,CLR_WHITE,&mira_font_fr_12,LV_TEXT_ALIGN_CENTER);
    batt_card=card(360,309,105,66,"Batterie",CLR_BATT);
    fields[3]=txt(batt_card,"--",5,26,95,CLR_BATT,&mira_font_fr_16,LV_TEXT_ALIGN_CENTER);
    batt_status=txt(batt_card,"Charge / déch.",2,48,101,CLR_WHITE,&mira_font_fr_12,LV_TEXT_ALIGN_CENTER);
    for(int i=0;i<3;i++){
        battery_shapes[i]=batt_box((unsigned)i,473+i*38);
        fields[4+i]=txt(battery_shapes[i],"--",0,58,34,CLR_WHITE,&mira_font_fr_12,LV_TEXT_ALIGN_CENTER);
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
    if(field<4)set_power_valid(field,new_value);
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
    else if(!strcmp(name,"ecs_power")){field=ecs_power;set_power_valid(4,value);}
    else if(!strcmp(name,"ev_soc"))field=ev_soc;
    else if(!strcmp(name,"ev_power")){field=ev_power;set_power_valid(5,value);}
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
        set_visibility(routes[i].track,on&&routes[i].valid&&fabsf(routes[i].watts)>0.01f);
        if(!on)for(unsigned j=0;j<3;j++)set_visibility(routes[i].pulse[j],false);
    }
}
void mira_energy_house_options(int solar,int battery,int ev,int ecs){
    option_solar=solar;option_battery=battery;option_ev=ev;option_ecs=ecs;
    refresh_visibility();
}
