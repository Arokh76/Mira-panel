#pragma once
#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>
esp_err_t mira_recovery_init(void);
// Last internal network step for on-screen first-run diagnosis.
const char *mira_recovery_last_stage(void);
// Called in the LVGL render task; -1 none, 0 shadows OFF, 1 ON.
int mira_recovery_take_shadow_request(void);
void mira_recovery_set_shadow_mode(int mode);

// HTTP commands queued for the LVGL rendering task (never mutate UI in HTTP).
int mira_recovery_take_scroll_request(void);
// No new URI slots: AUTO ON/OFF is controlled via /?auto=on|off.
int mira_recovery_take_auto_request(void);
void mira_recovery_set_auto_state(int enabled);
// Request from root /?bench=static|off; processed only in LVGL thread.
int mira_recovery_take_bench_request(void);
void mira_recovery_set_bench_state(int enabled);
// Reuse existing root handler, no new HTTP URI slots.
int mira_recovery_take_cache_request(void);
void mira_recovery_set_cache_state(int state, int cards);

void mira_recovery_set_scroll_state(uint32_t mode, uint32_t switches, uint32_t rebuild_us);

// Native adapter for the ORIGINAL beta /save?config=wifi form.
// Old /spiffs/reseau.json remains untouched for safe rollback.
esp_err_t mira_recovery_save_beta_wifi(const char *ssid,const char *password,
    const char *mode,const char *ip,const char *mask,
    const char *gateway,const char *dns1,const char *dns2);

// Beta ConfigAP form, stored in native NVS; settings applied on next boot.
esp_err_t mira_recovery_save_beta_ap(const char *password,const char *ip,bool active);
