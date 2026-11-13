// Mira ESP-IDF Waveshare 7-inch STANDARD 800x480 -- independent experimental firmware.
// Waveshare ESP-IDF LVGL9 baseline: vendor RGB profile + Espressif LVGL adapter.
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
#include "esp_lv_adapter.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_timer.h"

#define WIDTH 800
#define HEIGHT 480
#define LCD_PCLK_HZ (16 * 1000 * 1000) // Waveshare ESP-IDF 09_lvgl_v9_demo baseline
// WaveShare 09_lvgl_v9_demo declares 10 bounce LINES at RGB565.
// Important: esp_lcd_rgb_panel_config_t.bounce_buffer_size_px accepts PIXELS,
// not BYTES. The vendor macro multiplies by 16/8 as if this field used
// bytes, which would allocate 20 lines (32000 bytes) per DMA bounce buffer.
// Keep the intended 10 scanlines: 8000 pixels / 16000 bytes per buffer.
// Only this memory correction differs from the user-validated RGB profile.
#define LCD_BOUNCE_BUFFER_LINES 10
#define LCD_BOUNCE_BUFFER_PIXELS (WIDTH * LCD_BOUNCE_BUFFER_LINES)

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
static volatile uint32_t touch_ready_events;
static volatile uint32_t touch_invalid_coords;
static volatile uint32_t touch_raw_x;
static volatile uint32_t touch_raw_y;
static volatile uint32_t touch_config_x;
static volatile uint32_t touch_config_y;
static volatile uint8_t touch_last_status;
static volatile uint8_t touch_irq_level;
static bool finger_down;
static esp_lcd_panel_handle_t panel;
static lv_display_t *lvdisp;
static volatile uint32_t frames;
static volatile uint32_t vsync_missed;
static volatile uint32_t last_refresh_us;
static volatile uint32_t wait_total_us;
static volatile uint32_t vsync_events;
static volatile uint32_t frame_complete_events;

// No forced per-frame VSYNC restart or manual post-widget RGB restart.
// The Espressif adapter owns panel buffering and VSYNC coordination.
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
    out->touch_ready_events = touch_ready_events;
    out->touch_invalid_coords = touch_invalid_coords;
    out->touch_raw_x = touch_raw_x;
    out->touch_raw_y = touch_raw_y;
    out->touch_config_x = touch_config_x;
    out->touch_config_y = touch_config_y;
    out->touch_last_status = touch_last_status;
    out->touch_irq_level = touch_irq_level;
}
lv_display_t *mira_display_lvgl(void) { return lvdisp; }

// Waveshare ESP32-S3-Touch-LCD-7: GT911 reset = IO expander EXIO1,
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
    touch_irq_level = (uint8_t)gpio_get_level(GPIO_NUM_4);
    uint8_t status = 0;
    if (i2c_reg_read(0x814e, &status, 1) != ESP_OK) {
        finger_down = false;
        return;
    }
    touch_last_status = status;
    if (!(status & 0x80)) {
        // GT911 sets bit 7 only when a NEW sample is ready. No fresh
        // sample does NOT mean finger released: keep LVGL pressed at the
        // last valid coordinate or drag events become discontinuous.
        // A ready sample with count == 0 below is the actual release.
        if (finger_down) {
            data->point.x = (int32_t)touch_last_x;
            data->point.y = (int32_t)touch_last_y;
            data->state = LV_INDEV_STATE_PRESSED;
        }
        return;
    }
    touch_ready_events++;
    const uint8_t count = status & 0x0f;
    if (count > 0 && count <= 5) {
        uint8_t xy[8] = {};
        if (i2c_reg_read(0x814f, xy, sizeof(xy)) == ESP_OK) {
            // GT911 0x814F starts with track ID, followed by X/Y little endian (WT32/4.3C IDF fix).
            const int raw_x = xy[1] | (xy[2] << 8);
            const int raw_y = xy[3] | (xy[4] << 8);
            touch_raw_x = raw_x;
            touch_raw_y = raw_y;
            int x = raw_x;
            int y = raw_y;
            // Some GT911 panel configurations report portrait (480x800)
            // coordinates despite our landscape RGB 800x480 display.
            // Recover a swapped orientation while reporting RAW values.
            if ((x >= WIDTH || y >= HEIGHT) && raw_x < HEIGHT && raw_y < WIDTH) {
                x = raw_y;
                y = raw_x;
            }
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
            } else {
                touch_invalid_coords++;
                if (touch_invalid_coords < 5 || touch_invalid_coords % 50 == 0)
                    ESP_LOGW(TAG, "GT911 invalid raw touch x=%d y=%d status=0x%02x", raw_x, raw_y, status);
            }
        }
    } else {
        finger_down = false;
    }
    touch_clear();
}

// Waveshare 7 STANDARD CH422G protocol (unlike 4.3C's former register/value driver):
// each CH422G command has its own 7-bit I2C address, and receives ONE data byte.
// 0x24: working mode (bit 0 enables outputs); 0x38: IO0..7 output values.
static esp_err_t ioexp_write(i2c_master_dev_handle_t ioexp, uint8_t value) {
    return i2c_master_transmit(ioexp, &value, sizeof(value), 80);
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
    i2c_master_dev_handle_t ioexp_mode = NULL;
    i2c_master_dev_handle_t ioexp_out = NULL;
    esp_err_t err = i2c_master_bus_add_device(i2c_bus, &ext_cfg, &ioexp_mode);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "CH422G mode device creation: %s", esp_err_to_name(err));
        return; // HTTP recovery stays available; no boot-loop.
    }
    ext_cfg.device_address = 0x38;
    err = i2c_master_bus_add_device(i2c_bus, &ext_cfg, &ioexp_out);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "CH422G output device creation: %s", esp_err_to_name(err));
        return;
    }

    // CH422G 7-inch output bit map: bit1 TP_RST, bit2 DISP,
    // bit3 LCD_RST, bit4 SD_CS, bit5 USB/CAN select, bit6 LCD_VDD_EN.
    // Keep USB selected (bit5 LOW), enable panel power (bit6 HIGH).
    // 0x5E: TP_RST/DISP/LCD_RST/SD_CS/VDD high; USB_SEL low.
    const uint8_t lcd_normal = 0x5E;
    err = ioexp_write(ioexp_mode, 0x01);
    if (err == ESP_OK) err = ioexp_write(ioexp_out, lcd_normal);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "CH422G 7-inch initialize failed: %s (no panic; Wi-Fi stays up)",
                 esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "CH422G 7-inch ready: MODE@0x24=0x01 OUT@0x38=0x5E");
    // Critical Waveshare 7 LCD controller cold start: assert EXIO3
    // reset LOW, release HIGH only after panel power has settled.
    vTaskDelay(pdMS_TO_TICKS(40));
    err = ioexp_write(ioexp_out, lcd_normal & ~(1U << 3));
    if (err == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(100));
        err = ioexp_write(ioexp_out, lcd_normal);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LCD_RST EXIO3 pulse failed: %s", esp_err_to_name(err));
        return;
    }
    vTaskDelay(pdMS_TO_TICKS(120));
    ESP_LOGI(TAG, "LCD_RST EXIO3 pulse OK, panel power and DISP enabled");

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
    err = ioexp_write(ioexp_out, 0x5c); // TP_RST bit1 LOW; DISP and LCD_VDD_EN stay HIGH
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "CH422G touch reset LOW failed: %s", esp_err_to_name(err));
        return;
    }
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_ERROR_CHECK(gpio_set_level(GPIO_NUM_4, 0));
    vTaskDelay(pdMS_TO_TICKS(100));
    err = ioexp_write(ioexp_out, 0x5e); // TP_RST bit1 HIGH => GT911 exits reset
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "CH422G touch reset HIGH failed: %s", esp_err_to_name(err));
        return;
    }
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
    err = i2c_master_bus_add_device(i2c_bus, &touch_cfg, &gt911);
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
    // GT911 config 0x8048..0x804B stores X and Y resolution as LE16.
    uint8_t cfg_reg[] = {0x80, 0x48};
    uint8_t xy_res[4] = {};
    if (i2c_master_transmit_receive(gt911, cfg_reg, sizeof(cfg_reg),
                                    xy_res, sizeof(xy_res), 80) == ESP_OK) {
        touch_config_x = xy_res[0] | (xy_res[1] << 8);
        touch_config_y = xy_res[2] | (xy_res[3] << 8);
    }
    ESP_LOGI(TAG, "GT911 config resolution %lu x %lu", (unsigned long)touch_config_x,
             (unsigned long)touch_config_y);
    ESP_LOGI(TAG, "GT911 ACTIVE addr=0x%02x product='%c%c%c%c' GPIO4=IRQ EXIO1=RST",
             touch_addr, product[0], product[1], product[2], product[3]);
}

// Count LVGL refreshes for /perf. Native RGB VSYNC is managed by adapter.
static void refresh_ready_cb(lv_event_t *e) {
    (void)e;
    frames++;
}


static void log_rgb_dma_heap(const char *stage) {
    const uint32_t dma_caps = MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL;
    ESP_LOGI(TAG, "%s: DMA-internal free=%u largest=%u, PSRAM free=%u",
             stage,
             (unsigned)heap_caps_get_free_size(dma_caps),
             (unsigned)heap_caps_get_largest_free_block(dma_caps),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}

// The RGB driver allocates two bounce buffers from internal DMA-capable RAM.
// Reserve these and the PSRAM framebuffers before Wi-Fi/HTTP/Jeedom consume
// the scarce internal heap; LCD timing, CH422G and GT911 remain unchanged.
esp_err_t mira_display_preallocate_rgb(void) {
    if (panel != NULL) return ESP_OK;
    log_rgb_dma_heap("Before early RGB DMA allocation");
    esp_lcd_rgb_panel_config_t cfg = {
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .timings = {
            .pclk_hz = LCD_PCLK_HZ,
            .h_res = WIDTH,
            .v_res = HEIGHT,
            .hsync_pulse_width = 4,
            .hsync_back_porch = 8,
            .hsync_front_porch = 8,
            .vsync_pulse_width = 4,
            .vsync_back_porch = 8,
            .vsync_front_porch = 8,
            .flags.pclk_active_neg = 1,
        },
        .data_width = 16,
        .bits_per_pixel = 16,
        .num_fbs = esp_lv_adapter_get_required_frame_buffer_count(
            ESP_LV_ADAPTER_TEAR_AVOID_MODE_DEFAULT_RGB, ESP_LV_ADAPTER_ROTATE_0),
        // Exactly 10 RGB565 scanlines per bounce buffer (pixel units),
        // so the two internal-DMA allocations use 16KB each, not 32KB.
        .bounce_buffer_size_px = LCD_BOUNCE_BUFFER_PIXELS,
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
    esp_err_t err = esp_lcd_new_rgb_panel(&cfg, &panel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Early RGB panel allocation failed: %s", esp_err_to_name(err));
        log_rgb_dma_heap("RGB panel allocation failed");
        return err;
    }
    log_rgb_dma_heap("After early RGB DMA allocation");
    return ESP_OK;
}

esp_err_t mira_display_init(void) {
    if (panel == NULL) {
        ESP_LOGE(TAG, "RGB panel was not preallocated");
        return ESP_ERR_INVALID_STATE;
    }
    init_i2c();
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(panel), TAG, "RGB reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(panel), TAG, "RGB init");
    ESP_LOGI(TAG, "Waveshare 7 RGB vendor baseline PCLK=%d (800x480, 4/8/8, bounce=%u)",
             LCD_PCLK_HZ, (unsigned)LCD_BOUNCE_BUFFER_PIXELS);
    esp_lv_adapter_config_t adapter_cfg = ESP_LV_ADAPTER_DEFAULT_CONFIG();
    adapter_cfg.task_stack_size = 12 * 1024;
    adapter_cfg.stack_in_psram = true;
    ESP_RETURN_ON_ERROR(esp_lv_adapter_init(&adapter_cfg), TAG, "LVGL adapter init");
    esp_lv_adapter_display_config_t disp_cfg = ESP_LV_ADAPTER_DISPLAY_RGB_DEFAULT_CONFIG(
        panel, NULL, WIDTH, HEIGHT, ESP_LV_ADAPTER_ROTATE_0);
    disp_cfg.profile.use_psram = true;
    lvdisp = esp_lv_adapter_register_display(&disp_cfg);
    if (!lvdisp) return ESP_ERR_NO_MEM;
    lv_display_add_event_cb(lvdisp, refresh_ready_cb, LV_EVENT_REFR_READY, NULL);
    lv_indev_t *touch = lv_indev_create();
    assert(touch);
    lv_indev_set_type(touch, LV_INDEV_TYPE_POINTER);
    lv_indev_set_display(touch, lvdisp);
    lv_indev_set_read_cb(touch, mira_display_touch_read);
    return ESP_OK;
}

esp_err_t mira_display_set_brightness(uint8_t raw) {
    // The Waveshare 7-inch STD has a CH422G binary display/backlight gate,
    // not the 4.3C's CH32 register-0x05 PWM. Never send CH32 writes to CH422G.
    // Keep validated 7-inch IO output / panel power sequence intact.
    // UI brightness slider and sleep dimming are software-only until hardware
    // dimming support is independently verified for the 7-inch panel.
    (void)raw;
    static bool noted=false;
    if(!noted){
        noted=true;
        ESP_LOGW(TAG,"7-inch: hardware PWM brightness unavailable; display power left unchanged");
    }
    return ESP_OK;
}
