// Native Mira 7 STD energy-house preview, branched from MIRA7_OTA_20261120.
// Built entirely from LVGL primitives (no image assets, no HTTP/MQTT changes).
#include "mira_energy_house.h"
#include "mira_french_font.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "esp_log.h"

#define CLR_SOLAR 0xF6BF49u
#define CLR_HOME  0xFF9F61u
#define CLR_GRID  0x42C7EEu
#define CLR_BATT  0x55DAABu
#define CLR_ECS   0xF6BF49u
#define CLR_EV    0x62D6A7u
#define CLR_WHITE 0xF3F6F7u
#define CLR_SOFT  0x88949Bu
#define CLR_BG    0x000000u
#define CLR_PANEL 0x09141Au

typedef struct {
    lv_point_precise_t p[8];
    uint8_t n;
    lv_obj_t *pulse;
    lv_obj_t *track;
    bool valid;
    float watts;
    float phase;
    bool backwards;
} energy_route_t;

static lv_obj_t *scene, *fields[7], *battery_shapes[3];
static lv_obj_t *solar_layer,*ev_layer;
static lv_obj_t *ecs_card,*ecs_shape,*ecs_temp,*ecs_power;
static lv_obj_t *ev_card,*ev_shape,*ev_soc,*ev_power;
static lv_obj_t *solar_card,*solar_shape,*batt_card;
static lv_obj_t *batt_extra[2], *battery_track[3];
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
    lv_obj_t *c=shape(scene,x,y,w,h,CLR_PANEL,tint,1,9);
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
// Coordinates are anchored to the approved 1600x900 maquette, scaled to
// the native 800x480 Mira 7 STD screen; ECS is UPSTAIRS on the right.
#define POLY(name,...) static const lv_point_precise_t name[]={__VA_ARGS__}
POLY(roof_main,{187,239},{278,150},{506,113},{631,240});
POLY(roof_gable,{506,113},{559,166},{631,240});
POLY(roof_eave,{188,239},{400,217});
POLY(roof_back,{278,151},{505,113},{607,214});
POLY(house_walls,{197,238},{197,448},{613,448},{613,239});
POLY(ground_line,{153,450},{770,450});
POLY(garage_top,{593,308},{607,295},{752,295},{767,308});
POLY(garage_walls,{597,309},{597,450},{766,450},{766,309});
POLY(garage_inner,{615,330},{615,450},{749,450},{749,330},{615,330});
POLY(upper_floor,{412,339},{578,339});
POLY(upper_room,{419,249},{419,339},{579,339},{579,247});
POLY(window_shape,{437,257},{490,257},{490,319},{437,319},{437,257});
POLY(window_cross_a,{463,257},{463,318});
POLY(window_cross_b,{438,286},{490,286});
POLY(living_wall,{222,259},{222,449});
POLY(living_ceiling,{222,260},{385,260});
POLY(living_end,{387,341},{387,448});
POLY(living_floor,{220,445},{390,445});
POLY(pv_outline,{214,225},{281,159},{472,135},{409,220},{214,225});
POLY(pv_row_1,{235,203},{423,200});
POLY(pv_row_2,{252,185},{438,181});
POLY(pv_row_3,{269,170},{453,160});
POLY(pv_column_1,{320,155},{263,223});
POLY(pv_column_2,{359,149},{308,222});
POLY(pv_column_3,{405,143},{355,221});
POLY(pv_column_4,{445,139},{400,219});
POLY(car_roof,{637,406},{650,378},{671,369},{715,369},{739,394});
POLY(car_body,{636,408},{640,435},{744,435},{750,413},{739,393});
POLY(car_front,{650,419},{734,419});
POLY(tower_body,{40,274},{48,190},{55,179},{62,190},{71,274});
POLY(tower_upper,{36,221},{56,202},{77,221});
POLY(tower_mid,{32,242},{56,222},{81,242});
POLY(tower_cross,{36,274},{73,209});
POLY(tower_cross_2,{73,274},{38,210});
POLY(meter_bolt,{176,334},{166,349},{175,349},{171,363},{183,343},{173,343});
POLY(boiler_thermometer,{533,274},{533,296});
POLY(plant_stem,{339,414},{340,379},{335,375});
POLY(plant_branch_a,{339,398},{332,385},{327,383});
POLY(plant_branch_b,{339,391},{350,379},{354,376});
POLY(plant_branch_c,{339,408},{349,396});
POLY(lamp_wire,{296,341},{296,386});

static void draw_background(void){
    // Framed silhouette and inhabited rooms, not a schematic empty rectangle.
    poly(scene,roof_main,4,CLR_WHITE,4);
    poly(scene,roof_gable,3,CLR_WHITE,3);
    poly(scene,roof_eave,2,0xA8B8BFu,2);
    poly(scene,house_walls,4,CLR_WHITE,4);
    poly(scene,ground_line,2,CLR_WHITE,4);
    // Attic / upstairs for the ECS cylinder; window stays if ECS is absent.
    poly(scene,upper_floor,2,0xB6C3C7u,2);
    poly(scene,upper_room,4,0x64757Cu,2);
    poly(scene,window_shape,5,0x859AA1u,2);
    poly(scene,window_cross_a,2,0x859AA1u,1);
    poly(scene,window_cross_b,2,0x859AA1u,1);
    // Living area, downstairs left.
    poly(scene,living_wall,2,0x8499A2u,2);
    poly(scene,living_ceiling,2,0x8499A2u,1);
    poly(scene,living_end,2,0x8499A2u,1);
    poly(scene,living_floor,2,0x8499A2u,1);
    // Sofa: cushions, backrest, armrests and feet.
    shape(scene,242,394,74,43,0x0E171Bu,0xAAB9BDu,2,9);
    shape(scene,237,417,85,27,0x10191Du,0xAAB9BDu,2,6);
    shape(scene,238,410,13,32,0x0E171Bu,0xAAB9BDu,2,6);
    shape(scene,310,410,13,32,0x0E171Bu,0xAAB9BDu,2,6);
    shape(scene,254,439,3,7,0xAAB9BDu,0,0,1);
    shape(scene,308,439,3,7,0xAAB9BDu,0,0,1);
    // Suspended lamp.
    poly(scene,lamp_wire,2,0xAAB9BDu,1);
    shape(scene,282,383,28,5,0xF2D4A7u,0,0,3);
    // Plant is deliberately behind the left-of-battery charge vignette.
    shape(scene,331,432,20,15,0x2B4850u,0x88A6AEu,1,3);
    poly(scene,plant_stem,3,0x7BD34Du,2);
    poly(scene,plant_branch_a,3,0x7BD34Du,2);
    poly(scene,plant_branch_b,3,0x7BD34Du,2);
    poly(scene,plant_branch_c,2,0x7BD34Du,2);
    shape(scene,329,378,8,4,0x68C250u,0,0,4);
    shape(scene,350,376,9,4,0x68C250u,0,0,4);
    // Garage must remain even when the car is disabled.
    poly(scene,garage_top,4,CLR_WHITE,4);
    poly(scene,garage_walls,4,CLR_WHITE,4);
    poly(scene,garage_inner,5,0x97ABB1u,2);
    for(int i=1;i<=3;i++){
        shape(scene,616,330+28*i,132,1,0x314B58u,0,0,0);
    }
    // Chimney visible above the right-hand roof.
    shape(scene,535,109,26,53,0x08151Cu,CLR_WHITE,3,1);
    // Solar is a single toggleable layer, leaving an ordinary roof when off.
    solar_layer=shape(scene,0,0,800,480,CLR_BG,0,0,0);
    solar_shape=solar_layer;
    poly(solar_layer,pv_outline,5,0x3EACFFu,3);
    poly(solar_layer,pv_row_1,2,0x4199D7u,1);
    poly(solar_layer,pv_row_2,2,0x4199D7u,1);
    poly(solar_layer,pv_row_3,2,0x4199D7u,1);
    poly(solar_layer,pv_column_1,2,0x4199D7u,1);
    poly(solar_layer,pv_column_2,2,0x4199D7u,1);
    poly(solar_layer,pv_column_3,2,0x4199D7u,1);
    poly(solar_layer,pv_column_4,2,0x4199D7u,1);
    // Grid tower + utility meter, fixed architecture.
    poly(scene,tower_body,5,CLR_WHITE,3);
    poly(scene,tower_upper,3,CLR_WHITE,2);
    poly(scene,tower_mid,3,CLR_WHITE,2);
    poly(scene,tower_cross,2,CLR_WHITE,2);
    poly(scene,tower_cross_2,2,CLR_WHITE,2);
    shape(scene,156,313,38,66,0x0C1720u,CLR_WHITE,2,6);
    shape(scene,163,322,23,16,0x293E44u,0x9AADB1u,1,2);
    poly(scene,meter_bolt,6,CLR_SOLAR,3);
    // Upstairs ECS cylinder is optional, but its room stays.
    ecs_shape=shape(scene,510,250,48,84,CLR_BG,0,0,0);
    shape(ecs_shape,5,4,38,62,0xC7D4D7u,CLR_WHITE,2,12);
    shape(ecs_shape,5,4,38,11,0xE2E8E8u,0,0,10);
    shape(ecs_shape,5,55,38,11,0x9AAEB4u,0,0,10);
    shape(ecs_shape,22,64,4,13,0x9AAEB4u,0,0,1);
    shape(ecs_shape,16,77,16,3,0x9AAEB4u,0,0,1);
    shape(ecs_shape,23,21,3,26,0xE4402Eu,0,0,2);
    shape(ecs_shape,19,42,11,11,0xE4402Eu,0,0,LV_RADIUS_CIRCLE);
    // Car and its lights are a separate optional group, garage remains.
    ev_layer=shape(scene,0,0,800,480,CLR_BG,0,0,0);
    ev_shape=ev_layer;
    poly(ev_layer,car_roof,5,CLR_WHITE,3);
    poly(ev_layer,car_body,5,CLR_WHITE,3);
    poly(ev_layer,car_front,2,0x617A84u,2);
    shape(ev_layer,647,405,21,5,0x2DDBEBu,0,0,4);
    shape(ev_layer,719,405,21,5,0x2DDBEBu,0,0,4);
    shape(ev_layer,646,436,14,10,0x9AACB1u,0,0,4);
    shape(ev_layer,727,436,14,10,0x9AACB1u,0,0,4);
}
static void set_route(int i,const lv_point_precise_t *p,unsigned n,uint32_t color){
    energy_route_t *r=&routes[i];
    if(n>8)n=8;
    r->n=(uint8_t)n;
    for(unsigned j=0;j<n;j++)r->p[j]=p[j];
    r->track=poly(scene,r->p,(int)n,color,3);
    // Small colored moving segment, never a giant white arrow/dot.
    r->pulse=shape(scene,0,0,11,4,color,0,0,LV_RADIUS_CIRCLE);
    lv_obj_add_flag(r->pulse,LV_OBJ_FLAG_HIDDEN);
}
POLY(route_solar,{345,106},{345,210},{410,240},{410,337});
POLY(route_home,{410,337},{197,337});
POLY(route_grid,{410,337},{191,337},{54,337},{54,274});
POLY(route_battery,{410,337},{410,361},{448,361});
POLY(route_ecs,{410,337},{438,337},{458,318},{458,293},{510,293});
POLY(route_ev,{410,337},{596,337},{596,420},{637,420});
static void routes_create(void){
    set_route(0,route_solar,4,0x8EC740u);
    set_route(1,route_home,2,0x42C7EEu);
    set_route(2,route_grid,4,0x42C7EEu);
    set_route(3,route_battery,3,0x55DAABu);
    set_route(4,route_ecs,4,0xEAA048u);
    set_route(5,route_ev,4,0x62D6A7u);
    shape(scene,404,331,12,12,0xFFFFFFu,0x42C7EEu,1,LV_RADIUS_CIRCLE);
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
        set_visibility(r->pulse,false);
        return;
    }
    char scratch[64];
    snprintf(scratch,sizeof(scratch),"%s",text);
    for(char *p=scratch;*p;p++)if(*p==',')*p='.';
    char *end=NULL;
    float v=strtof(scratch,&end);
    if(end==scratch||!isfinite(v)){
        r->valid=false;
        set_visibility(r->pulse,false);
        return;
    }
    if(strstr(scratch,"kW")||strstr(scratch,"kw"))v*=1000.f;
    r->valid=true;
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
        if(!r->valid||fabsf(r->watts)<0.01f||r->n<2||!r->pulse){
            set_visibility(r->pulse,false);continue;
        }
        if((k==0&&!enabled(option_solar,seen_solar))||
           (k==3&&!enabled(option_battery,seen_battery))||
           (k==4&&!enabled(option_ecs,seen_ecs))||
           (k==5&&!enabled(option_ev,seen_ev))){
            set_visibility(r->pulse,false);continue;
        }
        float dist=0.f;
        for(unsigned j=1;j<r->n;j++){
            const float dx=(float)(r->p[j].x-r->p[j-1].x);
            const float dy=(float)(r->p[j].y-r->p[j-1].y);
            dist+=sqrtf(dx*dx+dy*dy);
        }
        if(dist<1.f)continue;
        const float absw=fabsf(r->watts);
        float speed=0.018f+fminf(absw/9000.f,0.045f);
        r->phase+=speed;
        if(r->phase>=1.f)r->phase-=1.f;
        float target=(r->backwards?1.f-r->phase:r->phase)*dist;
        float x=(float)r->p[0].x,y=(float)r->p[0].y;
        for(unsigned j=1;j<r->n;j++){
            float dx=(float)(r->p[j].x-r->p[j-1].x);
            float dy=(float)(r->p[j].y-r->p[j-1].y);
            float len=sqrtf(dx*dx+dy*dy);
            if(target<=len||j==r->n-1){
                float t=len>0.f?target/len:0.f;
                x=(float)r->p[j-1].x+t*dx;
                y=(float)r->p[j-1].y+t*dy;
                break;
            }
            target-=len;
        }
        lv_obj_set_pos(r->pulse,(int)x-5,(int)y-2);
        set_visibility(r->pulse,true);
    }
}
static void return_home(lv_event_t *ev){
    (void)ev;
    if(back_cb)back_cb();
}
static lv_obj_t *batt_box(unsigned i,int x){
    lv_obj_t *b=shape(scene,x,357,43,89,0x101C21u,CLR_WHITE,2,7);
    shape(b,13,3,17,4,0x7F9493u,0,0,1);
    for(int n=0;n<3;n++)shape(b,9,15+n*14,25,10,0x62D6A7u,0,0,2);
    battery_track[i]=b;
    return b;
}
lv_obj_t *mira_energy_house_create(lv_obj_t *screen,void (*on_back)(void)){
    mira_energy_house_destroy();
    back_cb=on_back;
    scene=shape(screen,0,0,800,480,0x000001u,0,0,0);
    lv_obj_set_style_bg_color(scene,lv_color_hex(CLR_BG),0);
    lv_obj_set_style_bg_opa(scene,LV_OPA_COVER,0);
    lv_obj_add_flag(scene,LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_style_pad_all(scene,0,0);
    draw_background();
    routes_create();
    solar_card=card(243,44,193,65,"Panneaux",CLR_SOLAR);
    fields[0]=txt(solar_card,"--",9,34,175,CLR_SOLAR,&lv_font_montserrat_20,LV_TEXT_ALIGN_CENTER);
    lv_obj_t *grid=card(77,201,117,77,"Réseau",CLR_GRID);
    fields[2]=txt(grid,"--",4,30,109,CLR_GRID,&lv_font_montserrat_20,LV_TEXT_ALIGN_CENTER);
    lv_obj_t *home=card(237,267,146,62,"Maison",CLR_HOME);
    fields[1]=txt(home,"--",5,30,136,CLR_HOME,&lv_font_montserrat_20,LV_TEXT_ALIGN_CENTER);
    ecs_card=card(465,172,124,75,"ECS",CLR_ECS);
    ecs_temp=txt(ecs_card,"--",5,28,114,CLR_ECS,&mira_font_fr_16,LV_TEXT_ALIGN_CENTER);
    ecs_power=txt(ecs_card,"--",5,52,114,CLR_WHITE,&mira_font_fr_12,LV_TEXT_ALIGN_CENTER);
    ev_card=card(638,195,155,80,"Véhicule",CLR_EV);
    ev_soc=txt(ev_card,"--",8,28,138,CLR_EV,&lv_font_montserrat_20,LV_TEXT_ALIGN_CENTER);
    ev_power=txt(ev_card,"--",8,53,138,CLR_WHITE,&mira_font_fr_12,LV_TEXT_ALIGN_CENTER);
    batt_card=card(340,370,103,72,"Batterie",CLR_BATT);
    fields[3]=txt(batt_card,"--",5,27,93,CLR_BATT,&mira_font_fr_16,LV_TEXT_ALIGN_CENTER);
    txt(batt_card,"Charge / décharge",2,51,99,CLR_WHITE,&mira_font_fr_12,LV_TEXT_ALIGN_CENTER);
    for(int i=0;i<3;i++){
        battery_shapes[i]=batt_box((unsigned)i,449+i*48);
        fields[4+i]=txt(battery_shapes[i],"--",1,67,41,CLR_WHITE,&mira_font_fr_12,LV_TEXT_ALIGN_CENTER);
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
    solar_layer=NULL;ev_layer=NULL;
    seen_solar=false;seen_battery=false;seen_ecs=false;seen_ev=false;
    option_solar=-1;option_battery=-1;option_ev=-1;option_ecs=-1;
    memset(fields,0,sizeof(fields));
    memset(routes,0,sizeof(routes));
    memset(battery_shapes,0,sizeof(battery_shapes));
    memset(battery_track,0,sizeof(battery_track));
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
    // Hide the whole cable as well as moving pixels for absent equipment.
    for(int i=0;i<6;i++){
        const bool on=i==0?solar:i==3?batt:i==4?ecs:i==5?ev:true;
        set_visibility(routes[i].track,on);
        if(!on)set_visibility(routes[i].pulse,false);
    }
}
void mira_energy_house_options(int solar,int battery,int ev,int ecs){
    option_solar=solar;option_battery=battery;option_ev=ev;option_ecs=ecs;
    refresh_visibility();
}
