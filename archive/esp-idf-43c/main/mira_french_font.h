#pragma once
#include "lvgl.h"
// French accented characters from the Mira 0.7.37 beta, ported to LVGL 9.6.
// ASCII and digits fall back to the corresponding Montserrat font.
extern const lv_font_t mira_font_fr_12;
extern const lv_font_t mira_font_fr_14;
extern const lv_font_t mira_font_fr_16;
// 28px French accented glyphs for the 800x480 standby date.
extern const lv_font_t mira_font_fr_28;

// Exact 96px home clock from the Waveshare 4.3C beta.
extern const lv_font_t mira_home_montserrat_96;
// Exact 120px sleep clock glyphs from Arduino Waveshare beta (LVGL9).
extern const lv_font_t mira_sleep_montserrat_120;
