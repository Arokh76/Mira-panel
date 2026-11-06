#!/usr/bin/env python3
"""Waveshare 7 STD parity check against the current compiled 4.3C beta.
The full Arduino-beta regression suite stays attached to the 4.3C sources;
only independent 7-inch hardware contracts should differ.
"""
from pathlib import Path

root = Path(__file__).resolve().parents[5]
common = root / "firmware/experiments/esp-idf/mira-idf"
target = root / "firmware/experiments/esp-idf/mira-idf-7"
main = target / "main"

shared = [
    "mira_full.c", "mira_full.h", "mira_recovery.c", "mira_recovery.h",
    "mira_mqtt.c", "mira_mqtt.h", "mira_jeedom.c", "mira_jeedom.h",
    "mira_camera.c", "mira_camera.h", "mira_album_art.c",
    "mira_beta_setup.c", "mira_beta_setup_pages.h", "mira_beta_networks.c",
    "mira_beta_data.c", "mira_beta_captive_dns.c",
    "mira_french_font.c", "mira_sleep_120.c", "mira_home_96.c",
    "mira_info_glyphs.c", "mira_legacy_cipher.c",
]
for name in shared:
    orig = (common / "main" / name).read_bytes()
    port = (main / name).read_bytes()
    assert orig == port, f"7-inch software core diverged from beta: {name}"
assert (common / "partitions.csv").read_bytes() == (target / "partitions.csv").read_bytes()
# Display adapter is deliberately added only on the 7-inch test hardware.
deps = (main / "idf_component.yml").read_text()
assert 'espressif/esp_lvgl_adapter: "0.5.2"' in deps
assert 'espressif/esp_jpeg: "1.3.2"' in deps
# Display timing and DMA mitigation are independent 7-inch hardware choices.
# Keep all other ESP-IDF settings synchronized with the 4.3C beta.
def config_values(path):
    return dict(line.split("=", 1) for line in path.read_text().splitlines()
                if line.startswith("CONFIG_") and "=" in line)
common_config = config_values(common / "sdkconfig.defaults")
target_config = config_values(target / "sdkconfig.defaults")
display_only = {
    "CONFIG_LCD_RGB_RESTART_IN_VSYNC", "CONFIG_SPIRAM_XIP_FROM_PSRAM",
    "CONFIG_LV_DEF_REFR_PERIOD", "CONFIG_SPIRAM_RODATA", "CONFIG_SPIRAM_MEMTEST",
    "CONFIG_ESP32S3_INSTRUCTION_CACHE_32KB", "CONFIG_ESP32S3_DATA_CACHE_64KB",
    "CONFIG_ESP32S3_DATA_CACHE_LINE_64B",
}
assert {k: v for k, v in common_config.items() if k not in display_only} == {
    k: v for k, v in target_config.items() if k not in display_only
}, "7-inch shared SDK settings diverged"
assert target_config["CONFIG_LCD_RGB_RESTART_IN_VSYNC"] == "n"
assert target_config["CONFIG_SPIRAM_XIP_FROM_PSRAM"] == "y"
assert target_config["CONFIG_LV_DEF_REFR_PERIOD"] == "15"
assert target_config["CONFIG_ESP32S3_DATA_CACHE_LINE_64B"] == "y"
print("PASS 7-inch shares latest 4.3C app and SDK settings except isolated RGB DMA/PSRAM controls")
# Initial provisioning must have all three IPv4 modes on both display targets.
page = (main / "mira_beta_setup_pages.h").read_text(encoding="utf-8")
setup = (main / "mira_beta_setup.c").read_text(encoding="utf-8")
for value in ('auto', 'fixed_auto', 'manual'):
    assert f'value=\\\"{value}\\\"' in page, value
for field in ('mask', 'gateway', 'dns1', 'dns2', 'ip_mode'):
    assert f'name=\\\"{field}\\\"' in page, field
assert 'serve_saved_wifi_page(req,mode,ip)' in setup
assert 'nvs_set_str(nv,"web_profile",encoded)' in setup
print("PASS 7-inch first-boot DHCP, fixed-auto, manual and DNS UI parity")
# Platform-specific Web pages and OTA must not share 4.3C hardware identity.
web=(main / "mira_beta_web.c").read_text(encoding="utf-8")
pages=(main / "mira_beta_web_pages.h").read_text(encoding="utf-8")
ota=(main / "mira_beta_ota.c").read_text(encoding="utf-8")
update=(main / "mira_beta_update.c").read_text(encoding="utf-8")
project=(target / "CMakeLists.txt").read_text(encoding="utf-8")
pipeline=(root / ".github/workflows/build-mira-idf-waveshare7.yml").read_text(encoding="utf-8")
assert "Waveshare ESP32-S3 Touch LCD 7 STD" in pages
assert "Waveshare ESP32-S3 Touch LCD 4.3C" not in pages
assert "waveshare-esp32s3-touch-lcd-7-std-idf" in pages
assert "waveshare-esp32s3-touch-lcd-4.3c-idf" not in pages
assert "render_system_7std(req)" in web
assert 'if(!strcmp(nav,"gpio"))return html_err(req,HTTPD_404_NOT_FOUND' in web
assert 'GPIO Waveshare 4,3C</a>' not in web
assert "static esp_err_t beta_43c_gpio_page(" not in web
assert "project(mira_idf_7std)" in project
assert 'strcmp(candidate.project_name,"mira_idf_7std")!=0' in ota
assert 'strcmp(candidate.project_name,"mira_idf_7std")!=0' in update
assert "waveshare-esp32s3-touch-lcd-7-std-idf" in update
assert "mira_idf_7std.bin" in pipeline
print("PASS 7-inch hardware identity, isolated GPIO routes and distinct OTA project")



driver = (main / "mira_display.c").read_text(encoding="utf-8")
app = (main / "app_main.c").read_text(encoding="utf-8")
gpio = (main / "mira_gpio43.c").read_text(encoding="utf-8")
h = (main / "mira_display.h").read_text(encoding="utf-8")
assert '#define WIDTH 800' in driver and '#define HEIGHT 480' in driver
assert '#define LCD_PCLK_HZ (16 * 1000 * 1000)' in driver
assert '#define LCD_BOUNCE_BUFFER_PIXELS (WIDTH * 10 * 16 / 8)' in driver
assert '.bounce_buffer_size_px = LCD_BOUNCE_BUFFER_PIXELS' in driver
assert 'esp_lv_adapter_get_required_frame_buffer_count(' in driver
for v in (
    'ext_cfg.device_address = 0x38;',
    'ioexp_write(ioexp_mode, 0x01)',
    'ioexp_write(ioexp_out, 0x5e)',
    'GPIO_NUM_4',
    'i2c_reg_read(0x814f, xy, sizeof(xy))',
    'esp_lv_adapter_register_display(&disp_cfg)',
):
    assert v in driver, f"7-inch hardware driver missing {v}"
assert 'mira_gpio43_init()' not in app
assert 'vendor RGB 800x480 16MHz' in app
assert 'esp_lv_adapter_start()' in app
assert 'lv_timer_create(mira_lvgl_timer, 10, NULL)' in app
assert 'esp_lv_adapter_lock(-1)' in app
assert 'ioexp_write(ioexp_device' not in driver
assert 'mira_display_io_write(' not in driver
assert 'mira_display_io_write(' not in h
assert 'ESP_ERR_NOT_SUPPORTED' in gpio
assert 'mira_gpio43_action_mapped' in gpio and 'return false;' in gpio
assert 'return ESP_OK;' in driver.split('esp_err_t mira_display_set_brightness(uint8_t raw)',1)[1]
assert 'esp_lcd_rgb_panel_restart(panel)' not in driver
assert 'mira_display_schedule_rgb_recovery' not in driver
assert 'esp_lv_adapter_register_display' in driver
assert 'esp_lv_adapter_get_required_frame_buffer_count' in driver
print("PASS 7-inch uses Waveshare ESP-IDF 16MHz RGB bounce buffers and Espressif LVGL adapter")
