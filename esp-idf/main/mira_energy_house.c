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
    return value && *value && strcmp(value,"--")!=0 &&
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
#define POLY(name,...) static const lv_point_precise_t name[]={__VA_ARGS__}
POLY(roof_left,{160,230},{276,146},{498,115},{631,228});
POLY(roof_right,{498,115},{555,157},{631,228});
POLY(house_floor,{160,230},{160,429},{631,429},{631,228});
POLY(garage_roof,{589,293},{779,293});
POLY(garage_outline,{591,294},{591,429},{779,429},{779,294});
POLY(pv_outline,{219,202},{287,163},{455,139},{421,206},{219,202});
POLY(pv_r1,{229,193},{436,193});
POLY(pv_r2,{241,183},{442,183});
POLY(pv_r3,{256,173},{449,173});
POLY(pv_c1,{274,170},{249,202});
POLY(pv_c2,{320,159},{298,204});
POLY(pv_c3,{370,153},{350,205});
POLY(pv_c4,{412,146},{404,205});
POLY(car_top,{661,392},{678,376},{718,376},{739,392});
POLY(car_bottom,{650,397},{650,418},{748,418},{748,397});
POLY(power_tower,{49,294},{61,202},{76,294});
POLY(tower_cross1,{39,229},{83,229});
POLY(tower_cross2,{44,257},{79,257});
POLY(tower_top,{52,218},{63,198},{75,218});

static void draw_background(void){
    poly(scene,roof_left,4,CLR_WHITE,3);
    poly(scene,house_floor,4,CLR_WHITE,3);
    poly(scene,roof_right,3,CLR_WHITE,3);
    poly(scene,garage_roof,2,CLR_WHITE,3);
    poly(scene,garage_outline,4,CLR_WHITE,3);
    shape(scene,608,314,154,115,CLR_BG,0x6C777Bu,2,0);
    shape(scene,521,97,20,24,CLR_BG,CLR_WHITE,2,0); // discreet chimney
    solar_layer=shape(scene,0,0,800,480,CLR_BG,0,0,0);
    solar_shape=solar_layer;
    poly(solar_layer,pv_outline,5,0x397CD6u,3);
    poly(solar_layer,pv_r1,2,0x2677B6u,1);
    poly(solar_layer,pv_r2,2,0x2677B6u,1);
    poly(solar_layer,pv_r3,2,0x2677B6u,1);
    poly(solar_layer,pv_c1,2,0x2677B6u,1);
    poly(solar_layer,pv_c2,2,0x2677B6u,1);
    poly(solar_layer,pv_c3,2,0x2677B6u,1);
    poly(solar_layer,pv_c4,2,0x2677B6u,1);
    shape(scene,179,319,169,110,CLR_BG,0x6D777Bu,1,0);
    // Interior is intentionally understated: a sofa, a lamp and a plant.
    shape(scene,201,390,100,32,CLR_BG,0x99A3A5u,2,5);
    shape(scene,214,380,75,15,CLR_BG,0x99A3A5u,2,5);
    shape(scene,322,388,2,38,0x87999Eu,0,0,1);
    shape(scene,310,386,26,2,0x87999Eu,0,0,1);
    shape(scene,338,408,17,3,0x87999Eu,0,0,1);
    shape(scene,341,398,12,12,CLR_BG,0x749C59u,1,2);
    shape(scene,344,390,2,9,0x749C59u,0,0,0);
    ev_layer=shape(scene,0,0,800,480,CLR_BG,0,0,0);
    ev_shape=ev_layer;
    poly(ev_layer,car_top,4,CLR_WHITE,2);
    poly(ev_layer,car_bottom,4,CLR_WHITE,2);
    shape(ev_layer,660,402,11,3,0x3BC6E4u,0,0,1);
    shape(ev_layer,725,402,11,3,0x3BC6E4u,0,0,1);
    shape(ev_layer,665,418,12,7,0x9CA5A9u,0,0,3);
    shape(ev_layer,726,418,12,7,0x9CA5A9u,0,0,3);
    poly(scene,power_tower,3,CLR_WHITE,2);
    poly(scene,tower_top,3,CLR_WHITE,2);
    poly(scene,tower_cross1,2,CLR_WHITE,2);
    poly(scene,tower_cross2,2,CLR_WHITE,2);
    shape(scene,163,290,30,52,0x14242Bu,CLR_WHITE,2,4);
    txt(scene,"⚡",164,303,27,CLR_SOLAR,&mira_font_fr_16,LV_TEXT_ALIGN_CENTER);
    ecs_shape=shape(scene,517,265,37,70,0xBFC6C8u,CLR_WHITE,1,11);
    shape(ecs_shape,15,15,9,30,0x7C898Au,0,0,4);
    // Inner temperatures are data labels, not permanently baked into scenery.
}
static void set_route(int i,const lv_point_precise_t *p,unsigned n,uint32_t color){
    energy_route_t *r=&routes[i];
    if(n>8)n=8;
    r->n=(uint8_t)n;
    for(unsigned j=0;j<n;j++)r->p[j]=p[j];
    r->track=poly(scene,r->p,(int)n,color,3);
    r->pulse=shape(scene,0,0,8,8,0xFFFFFFu,0,0,LV_RADIUS_CIRCLE);
    lv_obj_add_flag(r->pulse,LV_OBJ_FLAG_HIDDEN);
}
POLY(route_solar,{352,86},{352,183},{416,204},{419,310});
POLY(route_home,{419,310},{190,310});
POLY(route_grid,{419,310},{178,310},{91,310},{91,278});
POLY(route_battery,{419,310},{419,380},{448,380});
POLY(route_ecs,{419,310},{492,310},{503,294},{518,294});
POLY(route_ev,{419,310},{607,310},{607,367},{702,367});
static void routes_create(void){
    set_route(0,route_solar,4,0x8EC740u);
    set_route(1,route_home,2,0x42C7EEu);
    set_route(2,route_grid,4,0x42C7EEu);
    set_route(3,route_battery,3,0x55DAABu);
    set_route(4,route_ecs,4,0xEAA048u);
    set_route(5,route_ev,4,0x62D6A7u);
    shape(scene,413,304,12,12,0xFFFFFFu,0x42C7EEu,1,LV_RADIUS_CIRCLE);
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
        if(!r->valid||fabsf(r->watts)<3.f||r->n<2||!r->pulse){
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
        lv_obj_set_pos(r->pulse,(int)x-4,(int)y-4);
        set_visibility(r->pulse,true);
    }
}
static void return_home(lv_event_t *ev){
    (void)ev;
    if(back_cb)back_cb();
}
static lv_obj_t *batt_box(unsigned i,int x){
    lv_obj_t *b=shape(scene,x,355,43,78,0x131E21u,CLR_WHITE,2,7);
    shape(b,13,-5,17,5,0x7F9493u,0,0,1);
    for(int n=0;n<3;n++)shape(b,9,12+n*12,25,8,0x62D6A7u,0,0,1);
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
    solar_card=card(250,16,200,70,"Panneaux",CLR_SOLAR);
    fields[0]=txt(solar_card,"--",12,36,176,CLR_SOLAR,&lv_font_montserrat_20,LV_TEXT_ALIGN_CENTER);
    lv_obj_t *grid=card(77,186,127,76,"Réseau",CLR_GRID);
    fields[2]=txt(grid,"--",5,30,117,CLR_GRID,&lv_font_montserrat_20,LV_TEXT_ALIGN_CENTER);
    lv_obj_t *home=card(212,248,159,64,"Maison",CLR_HOME);
    fields[1]=txt(home,"--",5,32,149,CLR_HOME,&lv_font_montserrat_20,LV_TEXT_ALIGN_CENTER);
    ecs_card=card(478,171,129,73,"ECS",CLR_ECS);
    ecs_temp=txt(ecs_card,"-- °C",5,26,119,CLR_ECS,&mira_font_fr_16,LV_TEXT_ALIGN_CENTER);
    ecs_power=txt(ecs_card,"-- W",5,48,119,CLR_WHITE,&mira_font_fr_12,LV_TEXT_ALIGN_CENTER);
    ev_card=card(638,202,154,78,"Véhicule",CLR_EV);
    ev_soc=txt(ev_card,"-- %",8,28,138,CLR_EV,&lv_font_montserrat_20,LV_TEXT_ALIGN_CENTER);
    ev_power=txt(ev_card,"-- W",8,53,138,CLR_WHITE,&mira_font_fr_12,LV_TEXT_ALIGN_CENTER);
    batt_card=card(314,355,124,73,"Batterie",CLR_BATT);
    fields[3]=txt(batt_card,"--",6,29,112,CLR_BATT,&mira_font_fr_16,LV_TEXT_ALIGN_CENTER);
    txt(batt_card,"Charge / décharge",5,53,114,CLR_WHITE,&mira_font_fr_12,LV_TEXT_ALIGN_CENTER);
    for(int i=0;i<3;i++){
        battery_shapes[i]=batt_box((unsigned)i,450+i*48);
        fields[4+i]=txt(battery_shapes[i],"-- %",1,56,41,CLR_WHITE,&mira_font_fr_12,LV_TEXT_ALIGN_CENTER);
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
