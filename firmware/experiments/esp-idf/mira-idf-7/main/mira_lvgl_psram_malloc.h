#pragma once
#include <stddef.h>
// Explicitly referenced by app_main to force libmain.a custom LVGL backend
// to link before LVGL archives (static linker order on ESP-IDF).
size_t mira_lvgl_psram_largest(void);
