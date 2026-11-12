#pragma once
#include "lvgl.h"
// Match MiraPanelUI.cpp's INFO glyph palette & auto-detection.
const char *mira_info_glyph_kind(const char *requested,const char *name,const char *unit);
lv_obj_t *mira_info_glyph_create(lv_obj_t *card,const char *kind,
                                 int side,lv_color_t accent);
