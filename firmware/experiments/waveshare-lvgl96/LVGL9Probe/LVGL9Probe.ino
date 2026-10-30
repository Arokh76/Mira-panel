// LVGL 9.6 RGB565 smoke test -- WAVESHARE ESP32-S3 TOUCH LCD 4.3C ONLY.
// Not the Mira firmware. Do not flash until a human has reviewed it.
// Same RGB wiring, 12 MHz PCLK, dual PSRAM RGB framebuffers as Mira 0.7.37.
#include <Arduino.h>
#include <Wire.h>
#include <lvgl.h>
#include "esp_err.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static constexpr int W = 800, H = 480;
static esp_lcd_panel_handle_t panel = nullptr;
static lv_display_t *display = nullptr;
static TaskHandle_t render_task = nullptr;
static volatile uint32_t frames = 0;
static volatile uint32_t vsync_timeout = 0;
static uint8_t gt911_addr = 0x5D;

static void expander_write(uint8_t command, uint8_t value) {
  Wire.beginTransmission(0x24);
  Wire.write(command);
  Wire.write(value);
  Wire.endTransmission();
}

static bool gt_read(uint16_t reg, uint8_t *buf, uint8_t count) {
  Wire.beginTransmission(gt911_addr);
  Wire.write(reg >> 8);
  Wire.write(reg & 255);
  if (Wire.endTransmission(false)) return false;
  if (Wire.requestFrom((int)gt911_addr, (int)count) != count) return false;
  for (uint8_t i = 0; i < count; ++i) buf[i] = Wire.read();
  return true;
}
static void gt_clear() {
  Wire.beginTransmission(gt911_addr);
  Wire.write(0x81);
  Wire.write(0x4E);
  Wire.write(0);
  Wire.endTransmission();
}
static void touch_read(lv_indev_t *indev, lv_indev_data_t *data) {
  (void)indev;
  uint8_t status = 0;
  data->state = LV_INDEV_STATE_RELEASED;
  if (!gt_read(0x814E, &status, 1) || !(status & 0x80)) return;
  uint8_t count = status & 0x0f;
  if (count > 0 && count <= 5) {
    uint8_t p[8] = {};
    if (gt_read(0x8150, p, 8)) {
      data->point.x = (p[1] | (p[2] << 8));
      data->point.y = (p[3] | (p[4] << 8));
      data->state = LV_INDEV_STATE_PRESSED;
    }
  }
  gt_clear();
}
static bool IRAM_ATTR on_vsync(esp_lcd_panel_handle_t,
                              const esp_lcd_rgb_panel_event_data_t *, void *) {
  if (!render_task) return false;
  BaseType_t woken = pdFALSE;
  vTaskNotifyGiveFromISR(render_task, &woken);
  return woken == pdTRUE;
}
static void flush(lv_display_t *d, const lv_area_t *area, uint8_t *px) {
  if (lv_display_flush_is_last(d)) {
    esp_err_t err = esp_lcd_panel_draw_bitmap(panel,
             area->x1, area->y1, area->x2 + 1, area->y2 + 1, px);
    if (err == ESP_OK) {
      ulTaskNotifyValueClear(nullptr, ULONG_MAX);
      if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100)) == 0) ++vsync_timeout;
    }
    ++frames;
  }
  lv_display_flush_ready(d);
}
static void tick(void *) { lv_tick_inc(2); }

static void init_rgb() {
  esp_lcd_rgb_panel_config_t c = {};
  c.clk_src = LCD_CLK_SRC_DEFAULT;
  c.timings.pclk_hz = 12 * 1000 * 1000;
  c.timings.h_res = W;
  c.timings.v_res = H;
  c.timings.hsync_pulse_width = 8;
  c.timings.hsync_back_porch = 8;
  c.timings.hsync_front_porch = 4;
  c.timings.vsync_pulse_width = 8;
  c.timings.vsync_back_porch = 8;
  c.timings.vsync_front_porch = 4;
  c.timings.flags.pclk_active_neg = 1;
  c.data_width = 16;
  c.bits_per_pixel = 16;
  c.num_fbs = 2;
  c.bounce_buffer_size_px = W * 10;
  c.sram_trans_align = 4;
  c.psram_trans_align = 64;
  c.hsync_gpio_num = GPIO_NUM_46;
  c.vsync_gpio_num = GPIO_NUM_3;
  c.de_gpio_num = GPIO_NUM_5;
  c.pclk_gpio_num = GPIO_NUM_7;
  c.disp_gpio_num = GPIO_NUM_NC;
  const int gpios[] = {14,38,18,17,10,39,0,45,48,47,21,1,2,42,41,40};
  for (int i = 0; i < 16; i++) c.data_gpio_nums[i] = gpios[i];
  c.flags.fb_in_psram = 1;
  ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&c, &panel));
  ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
  esp_lcd_rgb_panel_event_callbacks_t callbacks = {};
  callbacks.on_bounce_frame_finish = on_vsync;
  ESP_ERROR_CHECK(esp_lcd_rgb_panel_register_event_callbacks(panel, &callbacks, nullptr));
}
static void make_scene() {
  lv_obj_t *root = lv_screen_active();
  lv_obj_set_style_bg_color(root, lv_color_hex(0x071421), 0);
  lv_obj_t *column = lv_obj_create(root);
  lv_obj_set_pos(column, 18, 10);
  lv_obj_set_size(column, 764, 454);
  lv_obj_set_flex_flow(column, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(column, 8, 0);
  lv_obj_set_style_pad_row(column, 8, 0);
  lv_obj_set_scroll_dir(column, LV_DIR_VER);
  lv_obj_set_style_bg_color(column, lv_color_hex(0x071421), 0);
  for (int i = 0; i < 35; i++) {
    lv_obj_t *card = lv_obj_create(column);
    lv_obj_set_width(card, lv_pct(100));
    lv_obj_set_height(card, 86);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x173149), 0);
    lv_obj_set_style_radius(card, 14, 0);
    lv_obj_set_style_shadow_width(card, 4, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_t *title = lv_label_create(card);
    lv_label_set_text_fmt(title, "Mira LVGL 9.6 - carte %02d", i + 1);
    lv_obj_set_style_text_color(title, lv_color_hex(0xEAF7FF), 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 12, 8);
    lv_obj_t *description = lv_label_create(card);
    lv_label_set_text(description, "Test de défilement RGB565 / ESP32-S3 / PSRAM");
    lv_obj_set_style_text_color(description, lv_color_hex(0x92B6CA), 0);
    lv_obj_align(description, LV_ALIGN_TOP_LEFT, 12, 34);
  }
}
void setup() {
  Serial.begin(115200);
  delay(300);
  Wire.begin(8, 9, 400000);
  expander_write(0x02, 0xFF);
  expander_write(0x03, 0xFF);
  Wire.beginTransmission(0x5D);
  if (Wire.endTransmission() != 0) gt911_addr = 0x14;
  render_task = xTaskGetCurrentTaskHandle();
  init_rgb();
  void *fb1 = nullptr, *fb2 = nullptr;
  ESP_ERROR_CHECK(esp_lcd_rgb_panel_get_frame_buffer(panel, 2, &fb1, &fb2));
  lv_init();
  display = lv_display_create(W,H);
  lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
  lv_display_set_buffers(display, fb1, fb2, W * H * 2, LV_DISPLAY_RENDER_MODE_DIRECT);
  lv_display_set_flush_cb(display, flush);
  lv_indev_t *pointer = lv_indev_create();
  lv_indev_set_type(pointer, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(pointer, touch_read);
  lv_indev_set_display(pointer, display);
  esp_timer_handle_t timer;
  esp_timer_create_args_t ta = {};
  ta.callback = tick;
  ta.name = "lv_tick";
  ESP_ERROR_CHECK(esp_timer_create(&ta, &timer));
  ESP_ERROR_CHECK(esp_timer_start_periodic(timer, 2000));
  make_scene();
  Serial.printf("[MIRA-LVGL9-PROBE] LVGL %d.%d.%d; RGB565 800x480; PSRAM=%u\n",
        LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH,
        (unsigned)ESP.getFreePsram());
}
void loop() {
  static uint32_t last = millis(), prev = 0;
  lv_timer_handler();
  uint32_t now = millis();
  if (now-last >= 5000) {
    Serial.printf("[MIRA-LVGL9-PROBE] frames/5s=%lu vsync_timeouts=%lu heap=%u psram=%u\n",
         (unsigned long)(frames-prev), (unsigned long)vsync_timeout,
         (unsigned)ESP.getFreeHeap(),(unsigned)ESP.getFreePsram());
    prev=frames; last=now;
  }
  vTaskDelay(pdMS_TO_TICKS(2));
}
