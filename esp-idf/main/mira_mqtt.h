#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_http_server.h"

// Optional MQTT 3.1.1 transport. Disabled until explicitly provisioned.
esp_err_t mira_mqtt_init(void);
void mira_mqtt_register_http(httpd_handle_t server);
bool mira_mqtt_publish_action(const char *name, const char *value);

// Original MiraMqtt::publishDisplayStates parity. Snapshot is built only
// on the LVGL task, and published to retained Display/<key>/state topics.
typedef struct {
    uint8_t day,night,sleep_brightness,brightness;
    uint32_t sleep_timeout;
    bool night_auto,clock_sleep,night_active,sleeping;
    char night_start[6],night_end[6];
} mira_mqtt_display_state_t;
void mira_mqtt_publish_display_states(const mira_mqtt_display_state_t *state);
