#pragma once
#include "lvgl.h"
void mira_ui_bench_create(lv_display_t *disp, bool touch_ready);
void mira_ui_update_fps(uint32_t fps_tenths);
void mira_ui_set_shadows(bool enabled);
