#pragma once
#include <stdint.h>
#include "lvgl.h"

// Lightweight, temporary WT32-style splash; call only with the LVGL lock held.
void mira_splash_show(lv_display_t *disp);
void mira_splash_set_progress(uint8_t percent);
// Replace the percentage with a concise on-screen status/error.
void mira_splash_set_message(const char *message);
void mira_splash_hide(void);
