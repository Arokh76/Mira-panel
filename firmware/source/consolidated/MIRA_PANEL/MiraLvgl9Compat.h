#pragma once
// Narrow LVGL 8 -> 9.6 source compatibility layer for ORIGINAL MiraPanelUI.
// Do not introduce this into stable LVGL 8.4 targets.
// Goal: migrate the 0.7.37 widgets one by one, not recreate fake widgets.
#include <lvgl.h>
#if LVGL_VERSION_MAJOR != 9
#error "MiraLvgl9Compat requires LVGL 9"
#endif

#define LV_LABEL_LONG_DOT LV_LABEL_LONG_MODE_DOTS
#define LV_LABEL_LONG_CLIP LV_LABEL_LONG_MODE_CLIP
#define LV_LABEL_LONG_WRAP LV_LABEL_LONG_MODE_WRAP
#define LV_LABEL_LONG_SCROLL LV_LABEL_LONG_MODE_SCROLL
#define lv_btn_create lv_button_create
#define lv_img_create lv_image_create
#define lv_img_set_src lv_image_set_src
#define lv_img_set_zoom lv_image_set_scale
#define lv_img_set_angle lv_image_set_rotation
#define lv_img_set_pivot lv_image_set_pivot
#define LV_IMG_CF_TRUE_COLOR LV_COLOR_FORMAT_RGB565
#define LV_IMG_CF_TRUE_COLOR_ALPHA LV_COLOR_FORMAT_ARGB8888
#define LV_RES_OK LV_RESULT_OK

typedef lv_image_dsc_t lv_img_dsc_t;

// No bitmap snapshot cache in the initial full-Mira native port.  This is
// intentional: cache mode produced artifacts and lower FPS in hardware tests.
static inline void mira_lvgl9_image_cache_invalidate(const void *src) {
    (void)src;
}
#define lv_img_cache_invalidate_src mira_lvgl9_image_cache_invalidate

static inline int32_t lv_txt_get_width(const char *txt,uint32_t len,
                                      const lv_font_t *font,int32_t letter_space,
                                      lv_text_flag_t flags) {
    (void)len; // Mira passes strlen(txt): text is zero terminated.
    if(!txt||!font)return 0;
    lv_point_t size={0,0};
    lv_text_get_size(&size,txt,font,letter_space,0,LV_COORD_MAX,flags);
    return size.x;
}

// LVGL9 removed the old color wheel widget.  Use a real editable hue arc
// as a transition, retaining actual Jeedom RGB read/write conversions.
#ifndef LV_COLORWHEEL_MODE_HUE
#define LV_COLORWHEEL_MODE_HUE 0
#endif
static inline lv_obj_t *lv_colorwheel_create(lv_obj_t *parent,bool knob) {
    (void)knob;
    lv_obj_t *arc=lv_arc_create(parent);
    lv_arc_set_range(arc,0,359);
    lv_arc_set_value(arc,200);
    lv_arc_set_rotation(arc,135);
    lv_arc_set_bg_angles(arc,0,270);
    return arc;
}
static inline void lv_colorwheel_set_mode(lv_obj_t *obj,int mode) {
    (void)obj;(void)mode;
}
static inline void lv_colorwheel_set_mode_fixed(lv_obj_t *obj,bool fixed) {
    (void)obj;(void)fixed;
}
static inline void lv_colorwheel_set_rgb(lv_obj_t *obj,lv_color_t color) {
    lv_color_hsv_t hsv=lv_color_to_hsv(color);
    lv_arc_set_value(obj,hsv.h);
}
static inline lv_color_t lv_colorwheel_get_rgb(lv_obj_t *obj) {
    return lv_color_hsv_to_rgb((uint16_t)lv_arc_get_value(obj),100,100);
}
