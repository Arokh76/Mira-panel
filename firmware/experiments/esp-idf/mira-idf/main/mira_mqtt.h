#pragma once
#include <stdbool.h>
#include "esp_err.h"
#include "esp_http_server.h"

// Optional MQTT 3.1.1 transport. Disabled until explicitly provisioned.
esp_err_t mira_mqtt_init(void);
void mira_mqtt_register_http(httpd_handle_t server);
bool mira_mqtt_publish_action(const char *name, const char *value);
