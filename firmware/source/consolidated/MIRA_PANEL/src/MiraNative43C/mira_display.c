// Mira ESP-IDF Waveshare 4.3C board adapter -- not part of the production firmware.
// Native ESP-IDF RGB frame swap + LVGL 9 direct mode, no Arduino/Waveshare LVGL port.
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include "mira_display.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
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
// Keep the board-verified 12 MHz RGB clock.  8 MHz produced LCD colour bars.
// ESP-IDF RGB hardware desynchronization under PSRAM bandwidth pressure
// requires a buffered scan-out path, not touch-triggered DMA restarts.
#define LCD_PCLK_HZ (12 * 1000 * 1000)

static const char *TAG = "mira-idf-rgb";
static i2c_master_bus_handle_t i2c_bus;
static i2c_master_dev_handle_t expander;
static uint8_t expander_io_state=0xff;
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
// Independent 10 ms GT911 sampler.  At ~9.7 render FPS the LVGL task can
// block for ~100 ms, so polling inside LVGL misses quick taps.  A FreeRTOS
// queue records press/move/release while the RGB renderer is busy.
typedef struct {
    int16_t x;
    int16_t y;
    uint8_t pressed;
} mira_touch_event_t;
static QueueHandle_t touch_events;
static lv_point_t lv_touch_point = {0, 0};
static bool lv_touch_pressed;
static mira_touch_hook_t lv_touch_hook;
void mira_display_set_touch_hook(mira_touch_hook_t hook) { lv_touch_hook = hook; }

static void touch_queue_event(bool pressed, int x, int y) {
    if (!touch_events) return;
    mira_touch_event_t e = {.x=(int16_t)x,.y=(int16_t)y,
                            .pressed=pressed ? 1 : 0};
    if (xQueueSend(touch_events,&e,0)!=pdTRUE) {
        mira_touch_event_t discard;
        (void)xQueueReceive(touch_events,&discard,0);
        (void)xQueueSend(touch_events,&e,0);
    }
}

static void gt911_poll_task(void *arg) {
    (void)arg;
    bool down=false;
    int last_x=0, last_y=0;
    int64_t last_good_us=0;
    for (;;) {
        if (touch_ready && gt911) {
            touch_irq_level=(uint8_t)gpio_get_level(GPIO_NUM_4);
            uint8_t status=0;
            const int64_t now=esp_timer_get_time();
            if (i2c_reg_read(0x814e,&status,1)==ESP_OK) {
                touch_last_status=status;
                if (status & 0x80) {
                    touch_ready_events++;
                    const uint8_t count=status & 0x0f;
                    if (count>=1 && count<=5) {
                        uint8_t xy[8]={0};
                        // GT911 point 0 starts at 0x814F (track ID), then X/Y little endian.
                        // Starting at 0x8150 shifts every field and creates absurd coordinates.
                        if (i2c_reg_read(0x814f,xy,sizeof(xy))==ESP_OK) {
                            const int raw_x=xy[1] | (xy[2]<<8);
                            const int raw_y=xy[3] | (xy[4]<<8);
                            touch_raw_x=(uint32_t)raw_x;
                            touch_raw_y=(uint32_t)raw_y;
                            int x=raw_x, y=raw_y;
                            const bool portrait=(touch_config_x>0 && touch_config_y>0 &&
                                touch_config_x<=HEIGHT && touch_config_y>=WIDTH);
                            if (portrait ||
                                ((x>=WIDTH || y>=HEIGHT) && raw_x<HEIGHT && raw_y<WIDTH)) {
                                x=raw_y;
                                y=raw_x;
                            }
                            if (x>=0 && x<WIDTH && y>=0 && y<HEIGHT) {
                                if (!down) {
                                    touch_presses++;
                                    ESP_LOGI(TAG,"GT911 DOWN (%d,%d) raw (%d,%d)",x,y,raw_x,raw_y);
                                    touch_queue_event(true,x,y);
                                } else if (abs(x-last_x)>=3 || abs(y-last_y)>=3) {
                                    touch_queue_event(true,x,y);
                                }
                                down=true;
                                last_x=x;last_y=y;last_good_us=now;
                                touch_last_x=(uint32_t)x;touch_last_y=(uint32_t)y;
                            } else {
                                touch_invalid_coords++;
                                if (touch_invalid_coords<=5 || touch_invalid_coords%50==0)
                                    ESP_LOGW(TAG,"GT911 invalid touch (%d,%d)",raw_x,raw_y);
                            }
                        }
                    } else if (count==0 && down) {
                        down=false;
                        touch_queue_event(false,last_x,last_y);
                    }
                    touch_clear();
                } else {
                    // Waveshare reference driver acknowledges status even when
                    // DATA_READY is clear.  Hold a press between 10ms samples.
                    touch_clear();
                }
                // Some GT911 firmwares do not produce an explicit zero-touch
                // sample: release after a short interval without a fresh point.
                if (down && now-last_good_us > 100000) {
                    down=false;
                    touch_queue_event(false,last_x,last_y);
                }
            } else if (down && now-last_good_us > 100000) {
                down=false;
                touch_queue_event(false,last_x,last_y);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void mira_display_touch_read(lv_indev_t *indev, lv_indev_data_t *data) {
    (void)indev;
    mira_touch_event_t event;
    if (touch_events && xQueueReceive(touch_events,&event,0)==pdTRUE) {
        lv_touch_pressed=(event.pressed!=0);
        lv_touch_point.x=event.x;
        lv_touch_point.y=event.y;
        // No unconditional VSYNC restart on touch. BETA3 restarted RGB on
        // every release but did not prevent horizontal drift; remove it.
        data->continue_reading = uxQueueMessagesWaiting(touch_events)>0;
    } else {
        data->continue_reading=false;
    }
    // Mira 0.7.37 managed wake-from-sleep in its former LVGL8 indev.
    // Native LVGL9 must forward touch activity to the SAME controller.
    // Suppress the complete initial press/release sequence while waking,
    // so it cannot trigger a Jeedom action behind the clock overlay.
    const bool consume = lv_touch_hook &&
        lv_touch_hook(lv_touch_pressed,
                      (uint16_t)lv_touch_point.x, (uint16_t)lv_touch_point.y);
    data->state = consume ? LV_INDEV_STATE_RELEASED :
                  (lv_touch_pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED);
    data->point = lv_touch_point;
}

static esp_err_t ioexp_write(i2c_master_dev_handle_t ioexp, uint8_t reg, uint8_t value) {
    uint8_t cmd[] = {reg, value};
    return i2c_master_transmit(ioexp, cmd, sizeof(cmd), 80);
}

// Preserve native RGB performance while exposing Mira's brightness preference.
void mira_display_set_brightness(uint8_t raw) {
    if(!expander) return;
    // IO2 is the LCD backlight enable; output PWM uses inverted duty.
    if(raw==0) {
        expander_io_state &= (uint8_t)~(1U<<2);
        (void)ioexp_write(expander,0x03,expander_io_state);
        return;
    }
    expander_io_state |= (1U<<2);
    (void)ioexp_write(expander,0x03,expander_io_state);
    uint8_t pct=(raw<=30)?1:(uint8_t)(1+((uint16_t)(raw-30)*99U+112U)/225U);
    if(pct>100)pct=100;
    uint8_t duty=(uint8_t)(((uint16_t)(100-pct)*80U+49U)/99U);
    (void)ioexp_write(expander,0x05,duty);
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
    expander=ioexp;

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
        // Espressif RGB drift workaround: two DMA bounce buffers in internal
        // SRAM, 20 scanlines each. Prevents direct PSRAM EDMA FIFO underflow
        // when the full Mira UI + Wi-Fi contend for octal PSRAM bandwidth.
        // Requires enough internal SRAM. On failure, fallback below to the
        // original display mode so OTA web recovery is still possible.
        .bounce_buffer_size_px = WIDTH * 20,
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
    ESP_LOGI(TAG,"RGB 12MHz bounce=%lu px (2 x %lu bytes SRAM), internal free=%lu largest=%lu",
             (unsigned long)cfg.bounce_buffer_size_px,
             (unsigned long)(cfg.bounce_buffer_size_px * 2U),
             (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    esp_err_t lcd_init_err = esp_lcd_new_rgb_panel(&cfg, &panel);
    if (lcd_init_err != ESP_OK) {
        ESP_LOGW(TAG, "RGB bounce allocation failed: %s; fallback to direct EDMA",
                 esp_err_to_name(lcd_init_err));
        cfg.bounce_buffer_size_px = 0;
        ESP_RETURN_ON_ERROR(esp_lcd_new_rgb_panel(&cfg, &panel), TAG,
                            "RGB panel allocate (direct fallback)");
    }
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(panel), TAG, "RGB reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(panel), TAG, "RGB init");
    esp_lcd_rgb_panel_event_callbacks_t cb = {
        .on_frame_buf_complete = lcd_frame_complete,
        .on_vsync = lcd_vsync_event,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_rgb_panel_register_event_callbacks(panel, &cb, NULL), TAG, "VSYNC callback");
    void *fb1 = NULL, *fb2 = NULL;
    ESP_RETURN_ON_ERROR(esp_lcd_rgb_panel_get_frame_buffer(panel, 2, &fb1, &fb2), TAG, "RGB double buffers");
    ESP_LOGI(TAG, "RGB565 %dx%d PCLK=%d bounce_lines=%lu, fb1=%p fb2=%p, PSRAM=%zu", WIDTH,HEIGHT, (unsigned long)(cfg.bounce_buffer_size_px / WIDTH),
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
    if (touch_ready) {
        touch_events=xQueueCreate(64,sizeof(mira_touch_event_t));
        if (!touch_events) return ESP_ERR_NO_MEM;
        BaseType_t task_ok=xTaskCreatePinnedToCore(gt911_poll_task,"gt911_10ms",4096,NULL,5,NULL,0);
        if (task_ok!=pdPASS) return ESP_ERR_NO_MEM;
        ESP_LOGI(TAG,"GT911 independent 10ms sampler active; LVGL receives queued events");
    }
    return ESP_OK;
}
