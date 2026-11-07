#pragma once
#include "esp_err.h"

// Isolated manufacturer-baseline test: Wi-Fi access point, captive DNS and
// tiny HTTP diagnostics only. No persistent network configuration is changed.
esp_err_t mira_net_diag_start(void);
