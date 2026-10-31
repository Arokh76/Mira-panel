#pragma once
#include "lvgl.h"
void mira_ui_bench_create(lv_display_t *disp, bool touch_ready);
void mira_ui_update_fps(uint32_t fps_tenths);
void mira_ui_set_shadows(bool enabled);
void mira_ui_set_shadow_mode(int mode); // 0=OFF 1=LVGL 2=CACHE
int mira_ui_shadow_mode(void);
