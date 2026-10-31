// Mira ESP-IDF Waveshare 4.3C board adapter -- not part of the production firmware.
// Native ESP-IDF RGB frame swap + LVGL 9 direct mode, no Arduino/Waveshare LVGL port.
#include <assert.h>
#include <string.h>
#include <inttypes.h>
#include "mira_display.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_panel_ops.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_timer.h"

#define WIDTH 800
#define HEIGHT 480
#define LCD_PCLK_HZ (12 * 1000 * 1000)

static const char *TAG = "mira-idf-rgb";
static i2c_master_bus_handle_t i2c_bus;
static i2c_master_dev_handle_t gt911;
static bool touch_ready;
static uint8_t touch_addr;
static volatile uint32_t touch_reads;
static volatile uint32_t touch_presses;
static volatile uint32_t touch_errors;
static volatile uint32_t touch_last_x;
static volatile uint32_t touch_last_y;
static bool finger_down;
static esp_lcd_panel_handle_t panel;
static lv_display_t *lvdisp;
static TaskHandle_t render_task;
static volatile uint32_t frames;
static volatile uint32_t vsync_missed;
static volatile uint32_t last_refresh_us;
static volatile uint32_t wait_total_us;
static volatile uint32_t vsync_events;
static volatile uint32_t frame_complete_events;

void mira_display_set_render_task(void) {
    render_task = xTaskGetCurrentTaskHandle();
}
void mira_display_stats(mira_display_stats_t *out) {
    out->frames = frames;
    out->vsync_missed = vsync_missed;
    out->last_refresh_us = last_refresh_us;
    out->wait_total_us = wait_total_us;
    out->vsync_events = vsync_events;
    out->frame_complete_events = frame_complete_events;
    out->touch_ready = touch_ready;
    out->touch_addr = touch_addr;
    out->touch_presses = touch_presses;
    out->touch_reads = touch_reads;
    out->touch_errors = touch_errors;
    out->touch_last_x = touch_last_x;
    out->touch_last_y = touch_last_y;
}
lv_display_t *mira_display_lvgl(void) { return lvdisp; }

// Waveshare ESP32-S3-Touch-LCD-4.3C: GT911 reset = IO expander EXIO1,
// GT911 IRQ/address select = ESP32 GPIO4. Matches Waveshare official
// 12_lvgl_transplant/components/touch/gt911.c bring-up sequence.
// Keep EXIO2 backlight HIGH and preserve the existing working RGB pin map.
#define TOUCH_I2C_ADDR_PRIMARY 0x5d
#define TOUCH_I2C_ADDR_BACKUP  0x14

bool mira_display_touch_ready(void) { return touch_ready; }

static esp_err_t i2c_reg_read(uint16_t reg, uint8_t *data, size_t len) {
    if (!touch_ready || !gt911) return ESP_ERR_INVALID_STATE;
    const uint8_t addr[] = {(uint8_t)(reg >> 8), (uint8_t)(reg & 0xff)};
    touch_reads++;
    const esp_err_t err = i2c_master_transmit_receive(gt911, addr, sizeof(addr), data, len, 30);
    if (err != ESP_OK) touch_errors++;
    return err;
}
static void touch_clear(void) {
    const uint8_t cmd[] = {0x81, 0x4e, 0};
    if (gt911 && touch_ready &&
        i2c_master_transmit(gt911, cmd, sizeof(cmd), 30) != ESP_OK) touch_errors++;
}
void mira_display_touch_read(lv_indev_t *indev, lv_indev_data_t *data) {
    (void)indev;
    data->state = LV_INDEV_STATE_RELEASED;
    if (!touch_ready) return;
    uint8_t status = 0;
    if (i2c_reg_read(0x814e, &status, 1) != ESP_OK) {
        finger_down = false;
        return;
    }
    if (!(status & 0x80)) return; // No new coordinates yet; don't clear stale data.
    const uint8_t count = status & 0x0f;
    if (count > 0 && count <= 5) {
        uint8_t xy[8] = {};
        if (i2c_reg_read(0x8150, xy, sizeof(xy)) == ESP_OK) {
            // GT911 0x8150: track ID, X low, X high, Y low, Y high, ...
            const int x = xy[1] | (xy[2] << 8);
            const int y = xy[3] | (xy[4] << 8);
            if (x >= 0 && x < WIDTH && y >= 0 && y < HEIGHT) {
                data->point.x = x;
                data->point.y = y;
                data->state = LV_INDEV_STATE_PRESSED;
                touch_last_x = (uint32_t)x;
                touch_last_y = (uint32_t)y;
                if (!finger_down) {
                    touch_presses++;
                    ESP_LOGI(TAG, "GT911 touch press x=%d y=%d", x, y);
                }
                finger_down = true;
            }
        }
    } else {
        finger_down = false;
    }
    touch_clear();
}

static esp_err_t ioexp_write(i2c_master_dev_handle_t ioexp, uint8_t reg, uint8_t value) {
    uint8_t cmd[] = {reg, value};
    return i2c_master_transmit(ioexp, cmd, sizeof(cmd), 80);
}

static void init_i2c(void) {
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = GPIO_NUM_8,
        .scl_io_num = GPIO_NUM_9,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &i2c_bus));
    i2c_device_config_t ext_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = 0x24,
        .scl_speed_hz = 400000,
    };
    i2c_master_dev_handle_t ioexp = NULL;
    ESP_ERROR_CHECK(i2c_master_bus_add_device(i2c_bus, &ext_cfg, &ioexp));

    // Configure IO0..IO7 outputs; our original 0xff keeps LCD/USB state
    // identical to the last working no-bounce firmware.
    ESP_ERROR_CHECK(ioexp_write(ioexp, 0x02, 0xff));
    ESP_ERROR_CHECK(ioexp_write(ioexp, 0x03, 0xff));

    // Correct missing GT911 hard reset / address selection:
    // (1) EXIO1 low, (2) GPIO4 low => choose GT911 0x5d,
    // (3) EXIO1 high, (4) release GPIO4 to input with pull-up.
    gpio_config_t irq_cfg = {
        .pin_bit_mask = 1ULL << GPIO_NUM_4,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&irq_cfg));
    ESP_ERROR_CHECK(gpio_set_level(GPIO_NUM_4, 0));
    ESP_ERROR_CHECK(ioexp_write(ioexp, 0x03, 0xfd)); // EXIO1 low; EXIO2 backlight stays on
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_ERROR_CHECK(gpio_set_level(GPIO_NUM_4, 0));
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_ERROR_CHECK(ioexp_write(ioexp, 0x03, 0xff)); // EXIO1 high => GT911 exits reset
    vTaskDelay(pdMS_TO_TICKS(200));

    // GPIO4 is GT911 interrupt input during operation. Poll coordinates using I2C.
    irq_cfg.mode = GPIO_MODE_INPUT;
    irq_cfg.pull_up_en = GPIO_PULLUP_ENABLE;
    ESP_ERROR_CHECK(gpio_config(&irq_cfg));

    // Don't install an unreachable GT911 handle: verify the address on the bus.
    if (i2c_master_probe(i2c_bus, TOUCH_I2C_ADDR_PRIMARY, 60) == ESP_OK) {
        touch_addr = TOUCH_I2C_ADDR_PRIMARY;
    } else if (i2c_master_probe(i2c_bus, TOUCH_I2C_ADDR_BACKUP, 60) == ESP_OK) {
        touch_addr = TOUCH_I2C_ADDR_BACKUP;
    } else {
        ESP_LOGE(TAG, "GT911 NOT DETECTED at 0x5d or 0x14 after EXIO1 hard reset");
        return;
    }
    i2c_device_config_t touch_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = touch_addr,
        .scl_speed_hz = 400000,
    };
    esp_err_t err = i2c_master_bus_add_device(i2c_bus, &touch_cfg, &gt911);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "GT911 add I2C device failed: %s", esp_err_to_name(err));
        touch_addr = 0;
        return;
    }

    // Verify product ID before saying the tactile is available.
    uint8_t product[4] = {};
    const uint8_t id_reg[] = {0x81, 0x40};
    err = i2c_master_transmit_receive(gt911, id_reg, sizeof(id_reg), product, sizeof(product), 80);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "GT911 ID read failed on 0x%02x: %s",
                 touch_addr, esp_err_to_name(err));
        touch_errors++;
        touch_addr = 0;
        return;
    }
    touch_ready = true;
    ESP_LOGI(TAG, "GT911 ACTIVE addr=0x%02x product='%c%c%c%c' GPIO4=IRQ EXIO1=RST",
             touch_addr, product[0], product[1], product[2], product[3]);
}

static bool IRAM_ATTR lcd_frame_complete(esp_lcd_panel_handle_t h,
                      const esp_lcd_rgb_panel_event_data_t *data, void *ctx) {
    (void)h; (void)data; (void)ctx;
    frame_complete_events++;
    BaseType_t woken = pdFALSE;
    if (render_task) vTaskNotifyGiveFromISR(render_task, &woken);
    return woken == pdTRUE;
}
static bool IRAM_ATTR lcd_vsync_event(esp_lcd_panel_handle_t h,
                      const esp_lcd_rgb_panel_event_data_t *data, void *ctx) {
    (void)h; (void)data; (void)ctx;
    vsync_events++;
    return false;
}
static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
    if (!lv_display_flush_is_last(disp)) {
        lv_display_flush_ready(disp);
        return;
    }
    // Native IDF frame swap: only submit the final region, as the buffer is already
    // an entire offscreen 800x480 RGB565 framebuffer allocated by esp_lcd.
    ulTaskNotifyTake(pdTRUE, 0);
    esp_err_t err = esp_lcd_panel_draw_bitmap(panel, 0, 0, WIDTH, HEIGHT, px_map);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "RGB swap failed: %s", esp_err_to_name(err));
        lv_display_flush_ready(disp);
    }
}
static void flush_wait_cb(lv_display_t *disp) {
    int64_t t0 = esp_timer_get_time();
    // NEVER mark a framebuffer available while DMA can still reference it:
    // the previous implementation acknowledged a timeout and could render into
    // a framebuffer still being scanned, producing tearing / a moving image.
    while (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(125)) == 0) {
        vsync_missed++;
        if (vsync_missed <= 5 || vsync_missed % 30 == 0) {
            ESP_LOGW(TAG, "RGB frame-complete callback timeout (%lu); retaining framebuffer",
                     (unsigned long)vsync_missed);
        }
        // The WiFi recovery HTTP service remains on another FreeRTOS task.
    }
    last_refresh_us = (uint32_t)(esp_timer_get_time() - t0);
    wait_total_us += last_refresh_us;
    frames++;
    lv_display_flush_ready(disp);
}
static void tick_cb(void *arg) { (void)arg; lv_tick_inc(2); }

esp_err_t mira_display_init(void) {
    init_i2c();
    esp_lcd_rgb_panel_config_t cfg = {
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .timings = {
            .pclk_hz = LCD_PCLK_HZ,
            .h_res = WIDTH,
            .v_res = HEIGHT,
            .hsync_pulse_width = 8,
            .hsync_back_porch = 8,
            .hsync_front_porch = 4,
            .vsync_pulse_width = 8,
            .vsync_back_porch = 8,
            .vsync_front_porch = 4,
            .flags.pclk_active_neg = 1,
        },
        .data_width = 16,
        .bits_per_pixel = 16,
        .num_fbs = 2,
        // At PCLK 12 MHz + octal PSRAM, direct EDMA avoids racing the
        // LVGL rasterizer against ISR-driven PSRAM->SRAM bounce copies.
        .bounce_buffer_size_px = 0,
        .dma_burst_size = 64,
        .sram_trans_align = 4,
        .psram_trans_align = 64,
        .hsync_gpio_num = GPIO_NUM_46,
        .vsync_gpio_num = GPIO_NUM_3,
        .de_gpio_num = GPIO_NUM_5,
        .pclk_gpio_num = GPIO_NUM_7,
        .disp_gpio_num = GPIO_NUM_NC,
        .data_gpio_nums = {14, 38, 18, 17, 10, 39, 0, 45, 48, 47, 21, 1, 2, 42, 41, 40},
        .flags.fb_in_psram = 1,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_rgb_panel(&cfg, &panel), TAG, "RGB panel allocate");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(panel), TAG, "RGB reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(panel), TAG, "RGB init");
    esp_lcd_rgb_panel_event_callbacks_t cb = {
        .on_frame_buf_complete = lcd_frame_complete,
        .on_vsync = lcd_vsync_event,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_rgb_panel_register_event_callbacks(panel, &cb, NULL), TAG, "VSYNC callback");
    void *fb1 = NULL, *fb2 = NULL;
    ESP_RETURN_ON_ERROR(esp_lcd_rgb_panel_get_frame_buffer(panel, 2, &fb1, &fb2), TAG, "RGB double buffers");
    ESP_LOGI(TAG, "RGB565 %dx%d PCLK=%d NO_BOUNCE, fb1=%p fb2=%p, PSRAM=%zu", WIDTH,HEIGHT,
         LCD_PCLK_HZ,fb1,fb2,heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    lv_init();
    lvdisp = lv_display_create(WIDTH, HEIGHT);
    assert(lvdisp);
    lv_display_set_user_data(lvdisp, panel);
    lv_display_set_color_format(lvdisp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(lvdisp, fb1, fb2, WIDTH * HEIGHT * 2, LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(lvdisp, flush_cb);
    lv_display_set_flush_wait_cb(lvdisp, flush_wait_cb);
    lv_indev_t *touch = lv_indev_create();
    assert(touch);
    lv_indev_set_type(touch, LV_INDEV_TYPE_POINTER);
    lv_indev_set_display(touch, lvdisp);
    lv_indev_set_read_cb(touch, mira_display_touch_read);
    esp_timer_handle_t timer;
    const esp_timer_create_args_t timer_cfg = {.callback = tick_cb, .name="lv_tick"};
    ESP_RETURN_ON_ERROR(esp_timer_create(&timer_cfg, &timer), TAG, "LVGL tick");
    ESP_RETURN_ON_ERROR(esp_timer_start_periodic(timer, 2000), TAG, "LVGL tick start");
    return ESP_OK;
}
