idf_component_register(
  SRCS "app_main.c" "mira_lvgl_psram_malloc.c" "mira_splash.c" "mira_net_diag.c" "mira_display.c" "mira_gpio43.c" "mira_full.c" "mira_energy_house.c" "mira_recovery.c" "mira_async_web.cpp" "mira_async_pages.cpp" "mira_mqtt.c" "mira_jeedom.c" "mira_camera.c" "mira_album_art.c" "mira_jpegdec_fallback.cpp" "mira_jpegdec_core.cpp" "mira_legacy_cipher.c" "mira_beta_setup.c" "mira_beta_web.c" "mira_beta_ota.c" "mira_beta_update.c" "mira_beta_data.c" "mira_beta_networks.c" "mira_beta_captive_dns.c" "mira_french_font.c" "mira_info_glyphs.c" "mira_home_96.c" "mira_sleep_120.c"
  INCLUDE_DIRS "."
  EMBED_TXTFILES
    "energy_svg/maison_fond.svg"
    "energy_svg/porte_garage.svg"
    "energy_svg/panneaux_solaires.svg"
    "energy_svg/ballon_ecs.svg"
    "energy_svg/vehicule_electrique.svg"
    "energy_svg/batterie_coque.svg"
  REQUIRES lvgl esp_lvgl_adapter esp_lcd esp_timer esp_driver_i2c esp_wifi esp_netif esp_event
           esp_http_server espressif__arduino-esp32 esp32async__asynctcp esp32async__espasyncwebserver esp_http_client app_update nvs_flash spiffs json mqtt mbedtls esp-tls jpegdec
)
