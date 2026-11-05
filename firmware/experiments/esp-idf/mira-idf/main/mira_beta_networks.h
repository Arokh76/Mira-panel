#pragma once
#include "esp_err.h"
#include <stddef.h>
#include <stdbool.h>
// The beta's reseau.json is read-only. All IDF changes live in a separate
// NVS overlay so an Arduino beta rollback does not get a changed file.
#define MIRA_NET_MAX 16
typedef struct {
    int id;
    char ssid[33];
    char ip_mode[16];
    char ip[32],mask[32],gateway[32],dns1[32],dns2[32];
} mira_beta_network_t;
esp_err_t mira_beta_networks_list(mira_beta_network_t *out,size_t *count,size_t cap);
// One-time migration for older IDF first-run Wi-Fi saved only by the ESP32 driver.
esp_err_t mira_beta_networks_recover_station(void);
esp_err_t mira_beta_networks_get(int id,mira_beta_network_t *out);
esp_err_t mira_beta_networks_upsert(const mira_beta_network_t *value,const char *password);
esp_err_t mira_beta_networks_remove(int id);
esp_err_t mira_beta_networks_activate(int id);
