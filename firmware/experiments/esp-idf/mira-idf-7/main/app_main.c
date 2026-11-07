// HARDWARE-ONLY Waveshare 7 STD (800x480) RGB + LVGL9 + GT911 diagnostic.
// Intentionally NO Wi-Fi, DNS, HTTP, NVS write, SPIFFS, OTA, MQTT, Jeedom or camera.
// Source reference: waveshareteam/ESP32-S3-Touch-LCD-7 09_lvgl_v9_demo.
// If RGB fails, leave USB serial alive with an error instead of rebooting.
#include <stdint.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_lv_adapter.h"
#include "lvgl.h"
#include "mira_display.h"

static const char *TAG = "mira7-hw-diag";
static lv_obj_t *touch_label;
static lv_obj_t *touch_indicator;
static lv_obj_t *moving_marker;
static uint32_t touch_count;
static uint32_t ticks;

static lv_obj_t *solid_rect(lv_obj_t *parent, int x, int y, int w, int h, uint32_t rgb)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_bg_color(obj, lv_color_hex(rgb), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    return obj;
}

static void on_color_pressed(lv_event_t *event)
{
    (void)event;
    touch_count++;
    ESP_LOGI(TAG, "TOUCH detected by LVGL: count=%" PRIu32, touch_count);
    if (touch_label != NULL) {
        lv_label_set_text_fmt(touch_label, "TACTILE OK : %" PRIu32 " pressions", touch_count);
    }
    if (touch_indicator != NULL) {
        lv_obj_set_style_bg_color(touch_indicator,
                                lv_color_hex((touch_count & 1U) ? 0x00FF00 : 0xFFFFFF),
                                LV_PART_MAIN);
    }
}

static void test_heartbeat(lv_timer_t *t)
{
    (void)t;
    ticks++;
    if (moving_marker != NULL) {
        // The marker must alternate positions if LVGL really keeps refreshing.
        lv_obj_set_x(moving_marker, (ticks & 1U) ? 30 : 740);
    }
    if ((ticks % 4U) == 0U) {
        mira_display_stats_t s;
        mira_display_stats(&s);
        ESP_LOGI(TAG, "LVGL running: refreshes=%" PRIu32 ", touch=%" PRIu32
                      ", GT911=%s, tests_elapsed=%" PRIu32 "s",
                 s.frames, touch_count, s.touch_ready ? "ready" : "missing", ticks / 2U);
    }
}

static void create_test_screen(void)
{
    lv_obj_t *screen = lv_screen_active();
    lv_obj_remove_style_all(screen);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x101827), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    // Three large RGB test patterns to identify missing color channels or drift.
    lv_obj_t *red   = solid_rect(screen,   0, 72, 267, 320, 0xFF0000);
    lv_obj_t *green = solid_rect(screen, 267, 72, 266, 320, 0x00FF00);
    lv_obj_t *blue  = solid_rect(screen, 533, 72, 267, 320, 0x0000FF);
    lv_obj_add_event_cb(red,   on_color_pressed, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(green, on_color_pressed, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(blue,  on_color_pressed, LV_EVENT_PRESSED, NULL);

    lv_obj_t *title = lv_label_create(screen);
    lv_label_set_text(title, "MIRA 7 - DIAGNOSTIC WAVESHARE");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 16);

    touch_label = lv_label_create(screen);
    lv_label_set_text(touch_label, "TOUCHEZ UNE COULEUR");
    lv_obj_set_style_text_color(touch_label, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_pos(touch_label, 220, 415);

    touch_indicator = solid_rect(screen, 30, 412, 160, 28, 0xFFFFFF);
    moving_marker = solid_rect(screen, 30, 380, 30, 12, 0xFFFFFF);
    ESP_LOGI(TAG, "TEST_SCREEN_CREATED: RGB stripes, moving white marker, touch status");
}

static void halt_on_error(const char *where, esp_err_t err)
{
    ESP_LOGE(TAG, "DIAG_FAILED at %s: %s (0x%x), serial remains available",
             where, esp_err_to_name(err), (unsigned)err);
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "Waveshare ESP32-S3-Touch-LCD-7 hardware-only diagnostic 800x480 16MHz");
    ESP_LOGW(TAG, "No Wi-Fi, no Jeedom, no MQTT, no camera, no storage writes");
    // The original Waveshare example uses 16000-pixel DMA bounce buffers.
    // Reserve them while all internal DMA memory is still free.
    esp_err_t err = mira_display_preallocate_rgb();
    if (err != ESP_OK) halt_on_error("RGB DMA allocation", err);

    // The panel driver performs the CH422G LCD reset and GT911 detection.
    err = mira_display_init();
    if (err != ESP_OK) halt_on_error("RGB/GT911/LVGL adapter initialization", err);
    ESP_LOGI(TAG, "DIAG_STAGE: RGB panel and LVGL adapter registered");

    err = esp_lv_adapter_start();
    if (err != ESP_OK) halt_on_error("LVGL adapter start", err);
    ESP_LOGI(TAG, "DIAG_STAGE: LVGL started");

    err = esp_lv_adapter_lock(-1);
    if (err != ESP_OK) halt_on_error("LVGL lock", err);
    create_test_screen();
    lv_timer_create(test_heartbeat, 500, NULL);
    esp_lv_adapter_unlock();
    ESP_LOGI(TAG, "DIAG_READY: expected RED | GREEN | BLUE bars and a moving white marker");
}
