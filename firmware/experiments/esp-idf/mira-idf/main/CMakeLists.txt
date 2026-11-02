idf_component_register(
  SRCS "app_main.c" "mira_display.c" "mira_full.c" "mira_recovery.c" "mira_mqtt.c" "mira_jeedom.c" "mira_camera.c" "mira_legacy_cipher.c" "mira_beta_setup.c" "mira_french_font.c" "mira_home_96.c"
  INCLUDE_DIRS "."
  REQUIRES lvgl esp_lcd esp_timer esp_driver_i2c esp_wifi esp_netif esp_event
           esp_http_server esp_http_client app_update nvs_flash spiffs json mqtt mbedtls
)
