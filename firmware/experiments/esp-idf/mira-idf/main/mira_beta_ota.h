#pragma once
#include "esp_http_server.h"
// Original beta /doUpdateFile multipart upload, native IDF OTA.
esp_err_t mira_beta_ota_upload(httpd_req_t *req);
