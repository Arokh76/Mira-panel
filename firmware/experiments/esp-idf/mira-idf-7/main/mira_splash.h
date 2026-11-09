#pragma once
#include <stdint.h>
#include "lvgl.h"

// Lightweight, temporary WT32-style splash; call only with the LVGL lock held.
void mira_splash_show(lv_display_t *disp);
void mira_splash_set_progress(uint8_t percent);
void mira_splash_hide(void);
