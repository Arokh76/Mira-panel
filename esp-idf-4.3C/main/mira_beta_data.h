#pragma once
#include "esp_http_server.h"
// Beta reset page actions: GET shows an authenticated confirmation, POST executes.
esp_err_t mira_beta_data_confirm(httpd_req_t *req,const char *operation);
esp_err_t mira_beta_data_reset_post(httpd_req_t *req);
