// Port of the shape-based INFO glyphs in firmware-beta MiraPanelUI.cpp.
// Do not depend on an extra icon font; objects are cached as part of tiles.
#include "mira_info_glyphs.h"
#include <string.h>
#include <strings.h>
#include <stdint.h>
#include <stdbool.h>

static bool has(const char *text,const char *word){
    if(!text||!word||!*word)return false;
    const size_t n=strlen(word);
    for(const char *p=text;*p;p++)
        if(!strncasecmp(p,word,n))return true;
    return false;
}
static bool known(const char *kind){
    return kind&&(!strcasecmp(kind,"temperature")||!strcasecmp(kind,"humidity")||
                 !strcasecmp(kind,"pressure")||!strcasecmp(kind,"light")||
                 !strcasecmp(kind,"energy")||!strcasecmp(kind,"plug")||
                 !strcasecmp(kind,"door")||
                 !strcasecmp(kind,"cloud")||!strcasecmp(kind,"rain"));
}
const char *mira_info_glyph_kind(const char *override,const char *name,const char *unit){
    // Explicit unknown icons are NOT replaced with guessed ones.
    if(known(override)){
        static const char *const names[]={
            "temperature","humidity","pressure","light","energy","plug","door","cloud","rain"
        };
        for(size_t i=0;i<sizeof(names)/sizeof(names[0]);i++)
            if(!strcasecmp(override,names[i]))return names[i];
    }
    if(override&&*override&&strcasecmp(override,"auto"))return NULL;
    if(has(name,"temp")||has(name,"thermo")||has(unit,"°"))return "temperature";
    if(has(name,"humid")||has(name,"hygro"))return "humidity";
    if(has(name,"press")||!strcasecmp(unit?unit:"","hpa")||
       !strcasecmp(unit?unit:"","mbar"))return "pressure";
    if(has(name,"lumi")||has(name,"éclai")||has(name,"eclai")||
       !strcasecmp(unit?unit:"","lux"))return "light";
    if(has(name,"porte")||has(name,"fenêtre"))return "door";
    if(has(name,"prise"))return "plug";
    if(has(name,"énergie")||has(name,"energie")||has(name,"puissance")||
       !strcasecmp(unit?unit:"","w")||!strcasecmp(unit?unit:"","kw")||
       !strcasecmp(unit?unit:"","wh")||!strcasecmp(unit?unit:"","kwh"))
        return "energy";
    return NULL;
}
static void part(lv_obj_t *parent,int x,int y,int width,int height,
                 bool outline,int radius,lv_color_t accent){
    lv_obj_t *o=lv_obj_create(parent);
    lv_obj_set_pos(o,x,y);
    lv_obj_set_size(o,width,height);
    lv_obj_remove_flag(o,LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_pad_all(o,0,0);
    lv_obj_set_style_radius(o,radius,0);
    lv_obj_set_style_bg_opa(o,outline?LV_OPA_TRANSP:LV_OPA_COVER,0);
    lv_obj_set_style_bg_color(o,accent,0);
    lv_obj_set_style_border_width(o,outline?2:0,0);
    if(outline)lv_obj_set_style_border_color(o,accent,0);
}
lv_obj_t *mira_info_glyph_create(lv_obj_t *card,const char *kind,
                                int side,lv_color_t accent){
    if(!card||!known(kind)||side<18)return NULL;
    lv_obj_t *root=lv_obj_create(card);
    lv_obj_set_size(root,side,side);
    lv_obj_remove_flag(root,LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(root,LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_style_bg_opa(root,LV_OPA_TRANSP,0);
    lv_obj_set_style_border_width(root,0,0);
    lv_obj_set_style_pad_all(root,0,0);
    const int d=side,stroke=d>=42?4:3;
    if(!strcmp(kind,"temperature")){
        part(root,d*40/100,d/8,d/5,d*54/100,true,7,accent);
        part(root,d*31/100,d*58/100,d*38/100,d*38/100,false,LV_RADIUS_CIRCLE,accent);
        part(root,d*46/100,d/3,stroke,d*35/100,false,2,accent);
    }else if(!strcmp(kind,"light")){
        part(root,d/3,d/3,d/3,d/3,false,LV_RADIUS_CIRCLE,accent);
        part(root,d/2-stroke/2,0,stroke,d/5,false,2,accent);
        part(root,d/2-stroke/2,d*4/5,stroke,d/5,false,2,accent);
        part(root,0,d/2-stroke/2,d/5,stroke,false,2,accent);
        part(root,d*4/5,d/2-stroke/2,d/5,stroke,false,2,accent);
    }else if(!strcmp(kind,"cloud")||!strcmp(kind,"rain")){
        // Direct C port of beta MiraPanelUI::miraCreateInfoGlyph:
        // same cloud lobes and rain strokes, no bitmap or icon font.
        part(root,d/9,d*43/100,d*45/100,d*38/100,
             false,LV_RADIUS_CIRCLE,accent);
        part(root,d*31/100,d/4,d*45/100,d*49/100,
             false,LV_RADIUS_CIRCLE,accent);
        part(root,d*52/100,d*45/100,d*38/100,d*32/100,
             false,LV_RADIUS_CIRCLE,accent);
        part(root,d/8,d*57/100,d*3/4,d/5,false,4,accent);
        if(!strcmp(kind,"rain")){
            part(root,d/4,d*82/100,stroke,d/6,false,1,accent);
            part(root,d*62/100,d*82/100,stroke,d/6,false,1,accent);
        }
    }else if(!strcmp(kind,"pressure")){
        part(root,d/7,d/6,d*5/7,d*5/7,true,LV_RADIUS_CIRCLE,accent);
        part(root,d*29/100,d*56/100,d*42/100,stroke,false,2,accent);
        part(root,d*44/100,d*43/100,d/8,d/8,false,LV_RADIUS_CIRCLE,accent);
    }else if(!strcmp(kind,"humidity")){
        part(root,d*25/100,d*41/100,d/2,d/2,false,LV_RADIUS_CIRCLE,accent);
        part(root,d*41/100,d/7,d*18/100,d/2,false,4,accent);
    }else if(!strcmp(kind,"door")){
        part(root,d/4,d/10,d/2,d*8/10,true,2,accent);
        part(root,d*59/100,d/2,stroke,stroke,false,LV_RADIUS_CIRCLE,accent);
    }else{
        part(root,d/4,d*33/100,d/2,d*4/10,false,6,accent);
        part(root,d*36/100,d*14/100,stroke,d/4,false,2,accent);
        part(root,d*59/100,d*14/100,stroke,d/4,false,2,accent);
        part(root,d/2-stroke/2,d*7/10,stroke,d/5,false,2,accent);
    }
    return root;
}
