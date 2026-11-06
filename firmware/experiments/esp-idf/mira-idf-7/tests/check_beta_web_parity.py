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
    "mira_beta_setup.c", "mira_beta_web.c", "mira_beta_networks.c",
    "mira_beta_ota.c", "mira_beta_data.c", "mira_beta_captive_dns.c",
    "mira_french_font.c", "mira_sleep_120.c", "mira_home_96.c",
    "mira_info_glyphs.c", "mira_legacy_cipher.c",
    "CMakeLists.txt", "idf_component.yml",
]
for name in shared:
    orig = (common / "main" / name).read_bytes()
    port = (main / name).read_bytes()
    assert orig == port, f"7-inch software core diverged from beta: {name}"
for name in ("partitions.csv", "sdkconfig.defaults", "CMakeLists.txt"):
    assert (common / name).read_bytes() == (target / name).read_bytes(), name
print("PASS 7-inch includes unchanged latest 4.3C beta app, config, NTP/DNS, Jeedom/MQTT and standby core")

driver = (main / "mira_display.c").read_text(encoding="utf-8")
app = (main / "app_main.c").read_text(encoding="utf-8")
gpio = (main / "mira_gpio43.c").read_text(encoding="utf-8")
h = (main / "mira_display.h").read_text(encoding="utf-8")
assert '#define WIDTH 800' in driver and '#define HEIGHT 480' in driver
assert '#define LCD_PCLK_HZ (13 * 1000 * 1000)' in driver
for v in (
    'ext_cfg.device_address = 0x38;',
    'ioexp_write(ioexp_mode, 0x01)',
    'ioexp_write(ioexp_out, 0x5e)',
    'GPIO_NUM_4',
    'i2c_reg_read(0x814f, xy, sizeof(xy))',
    'lv_display_set_buffers(lvdisp, fb1, fb2',
):
    assert v in driver, f"7-inch hardware driver missing {v}"
assert 'mira_gpio43_init()' not in app
assert '7 STD 13MHz RGB + CH422G + GT911' in app
assert 'ioexp_write(ioexp_device' not in driver
assert 'mira_display_io_write(' not in driver
assert 'mira_display_io_write(' not in h
assert 'ESP_ERR_NOT_SUPPORTED' in gpio
assert 'mira_gpio43_action_mapped' in gpio and 'return false;' in gpio
assert 'return ESP_OK;' in driver.split('esp_err_t mira_display_set_brightness(uint8_t raw)',1)[1]
print("PASS 7-inch uses 13MHz RGB, CH422G, GT911 and no unsafe 4.3C GPIO/PWM commands")
