#pragma once
#include "esp_http_server.h"
// Read-only and authenticated Jeedom plugin protocol + safe programming and state commands.
void mira_jeedom_register_http(httpd_handle_t server);
