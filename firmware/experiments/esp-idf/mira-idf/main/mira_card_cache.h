#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"

// Shared RGB565 background for all benchmark cards, precomposited once.
// Keeps border, rounded corners and a soft 4px shadow; 1 image ~130KB PSRAM.
bool mira_card_cache_prepare(int32_t width, int32_t height);
const lv_image_dsc_t *mira_card_cache_image(void);
