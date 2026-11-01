idf_component_register(
  SRCS "app_main.c" "mira_display.c" "mira_full.c" "mira_recovery.c" "mira_mqtt.c"
  INCLUDE_DIRS "."
  REQUIRES lvgl esp_lcd esp_timer esp_driver_i2c esp_wifi esp_netif esp_event
           esp_http_server app_update nvs_flash spiffs json mqtt
)
