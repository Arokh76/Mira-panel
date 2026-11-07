# Mira Panel 7" — hardware RGB diagnostic (isolated)

**DO NOT MERGE into firmware-beta or the stable branch.**
This branch contains a USB-flash-only **hardware test**, not a usable Mira build.

## Baseline
- Waveshare official ESP32-S3-Touch-LCD-7 ESP-IDF \`09_lvgl_v9_demo\`: 800x480 RGB565, 16MHz, 4/8/8 porches, official 16000-pixel (=32KiB) DMA bounce buffers, two bounce buffers, two or more PSRAM framebuffers managed by the LVGL adapter.
- ESP-IDF v5.5.6; esp_lvgl_adapter v0.5.2; LVGL v9.6.
- Target hardware: Waveshare ESP32-S3-Touch-LCD-7 **STANDARD**, with CH422G reset + GT911 I2C.
- This experiment retains the existing proven Mira CH422G / GT911 driver wiring; it **does not** replace it with the vendor's legacy I2C driver.

## Expected visual
- Dark header "MIRA 7 - DIAGNOSTIC WAVESHARE".
- Static vertical bars: **RED / GREEN / BLUE**.
- White marker at lower edge moves from left to right every half second.
- Touching a color increments the visible counter and serial log.

## Scope and safety
- No Wi-Fi, HTTP, captive portal, NVS, SPIFFS, Jeedom, MQTT, camera or OTA server.
- No writes to saved user settings.
- Use **USB factory binary at offset 0x0**; do **not** select full chip erase.
- The application has no web provisioning by design. Return to the normal Mira firmware via USB flash after testing.
- ESP-IDF logs should show \`DIAG_STAGE\`, \`TEST_SCREEN_CREATED\` and \`DIAG_READY\`.
- If there is still a black screen, keep the serial output after a single reset (this firmware will log the error and remain running instead of rebooting on returned errors).

## Why this test
The last Mira build got beyond Jeedom yet remained black and had an inaccessible AP.
Testing RGB and LVGL without any network services separates hardware/display
issues from resource and scheduling contention. Do not optimize cores until a
real panel displays this pattern correctly.
