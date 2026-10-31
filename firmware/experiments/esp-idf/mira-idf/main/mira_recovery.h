#pragma once
#include "esp_err.h"
esp_err_t mira_recovery_init(void);
// Called in the LVGL render task; -1 none, 0 shadows OFF, 1 ON.
int mira_recovery_take_shadow_request(void);
void mira_recovery_set_shadow_mode(int mode);

// HTTP commands queued for the LVGL rendering task (never mutate UI in HTTP).
int mira_recovery_take_scroll_request(void);
// No new URI slots: AUTO ON/OFF is controlled via /?auto=on|off.
int mira_recovery_take_auto_request(void);
void mira_recovery_set_auto_state(int enabled);
void mira_recovery_set_scroll_state(uint32_t mode, uint32_t switches, uint32_t rebuild_us);
