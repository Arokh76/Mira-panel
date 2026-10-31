#pragma once
#include "esp_err.h"
esp_err_t mira_recovery_init(void);
// Called in the LVGL render task; -1 none, 0 shadows OFF, 1 ON.
int mira_recovery_take_shadow_request(void);
void mira_recovery_set_shadow_mode(int mode);
