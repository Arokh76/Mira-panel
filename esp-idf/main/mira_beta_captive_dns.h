#pragma once
#include "esp_err.h"
// DNS wildcard for the original Mira Panel Wi-Fi first-boot captive portal.
// Only serves IPv4 A queries from 192.168.4.0/24 while setup is required.
esp_err_t mira_beta_captive_dns_start(void);
