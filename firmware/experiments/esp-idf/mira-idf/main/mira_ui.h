#pragma once
#include "lvgl.h"
void mira_ui_bench_create(lv_display_t *disp, bool touch_ready);
void mira_ui_update_fps(uint32_t fps_tenths);
// Automatic scroll control: also accessible through HTTP without touchscreen.
void mira_ui_set_auto_scroll(bool enabled);
bool mira_ui_auto_scroll(void);
void mira_ui_set_shadows(bool enabled);
void mira_ui_set_shadow_mode(int mode); // 0=OFF 1=LVGL4px 2=LIGHT2px 3=RELIEF (outline)
int mira_ui_shadow_mode(void);

// 0: 35-card classic; 1: 8-card virtual. Virtual created only on demand.
void mira_ui_set_scroll_mode(int mode);
int mira_ui_scroll_mode(void);
uint32_t mira_ui_scroll_switches(void);
uint32_t mira_ui_scroll_rebuild_us(void);
