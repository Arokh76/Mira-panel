// A/B benchmark optimization: render the costly rounded rectangle shadow
// only ONCE into an RGB565 PSRAM bitmap. The 35 card labels remain live LVGL
// objects. The cached mode may look slightly different from native LVGL
// shadows; compare both visually and on /perf before reusing in Mira.
#include "mira_card_cache.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include <math.h>
#include <string.h>

static lv_image_dsc_t card_dsc;
static uint16_t *card_pixels;
static const char *TAG = "mira-shadow-cache";

static uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return ((uint16_t)(r >> 3) << 11) | ((uint16_t)(g >> 2) << 5) | (uint16_t)(b >> 3);
}
static uint8_t lerp_u8(uint8_t base, uint8_t above, uint8_t opacity) {
    return (uint8_t)((((uint16_t)base * (255 - opacity)) +
                        ((uint16_t)above * opacity) + 127) / 255);
}
// Signed distance to the rounded box (negative within the fill).
static float rounded_distance(float px, float py, float w, float h, float radius) {
    float qx = fabsf(px - w * 0.5f) - (w * 0.5f - radius);
    float qy = fabsf(py - h * 0.5f) - (h * 0.5f - radius);
    float ax = fmaxf(qx, 0.0f);
    float ay = fmaxf(qy, 0.0f);
    return sqrtf(ax * ax + ay * ay) + fminf(fmaxf(qx, qy), 0.0f) - radius;
}

bool mira_card_cache_prepare(int32_t width, int32_t height) {
    if (card_pixels) return card_dsc.header.w == width && card_dsc.header.h == height;
    if (width < 100 || width > 1000 || height < 32 || height > 200) return false;
    const size_t bytes = (size_t)width * height * sizeof(uint16_t);
    card_pixels = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!card_pixels) {
        ESP_LOGW(TAG, "No PSRAM for card cache (%u bytes), native mode only", (unsigned)bytes);
        return false;
    }
    // Original colors: scroll background #071421, card #173149, border #233a50.
    // Leave 5px inside each image for a soft shadow; no alpha blending during scroll.
    const float left = 4.0f, top = 3.0f, right = (float)width - 4.0f;
    const float bottom = (float)height - 5.0f;
    const float rw = right - left, rh = bottom - top;
    for (int32_t y = 0; y < height; y++) {
        for (int32_t x = 0; x < width; x++) {
            float dx = (float)x + 0.5f - left;
            float dy = (float)y + 0.5f - top;
            float d = rounded_distance(dx, dy, rw, rh, 14.0f);
            uint8_t r = 0x07, g = 0x14, b = 0x21;
            if (d > 0.0f && d < 5.0f) {
                // Sample the shadow against its existing uniform background.
                uint8_t opacity = (uint8_t)((5.0f - d) * (45.0f / 5.0f));
                r = lerp_u8(r, 0, opacity);
                g = lerp_u8(g, 0, opacity);
                b = lerp_u8(b, 0, opacity);
            }
            if (d < 0.0f) {
                if (d > -1.0f) {r = 0x23; g = 0x3a; b = 0x50;} // border
                else {r = 0x17; g = 0x31; b = 0x49;} // fill
            }
            card_pixels[(size_t)y * width + x] = rgb565(r, g, b);
        }
    }

    memset(&card_dsc, 0, sizeof(card_dsc));
    card_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    card_dsc.header.cf = LV_COLOR_FORMAT_RGB565;
    card_dsc.header.w = (uint16_t)width;
    card_dsc.header.h = (uint16_t)height;
    card_dsc.header.stride = (uint16_t)(width * sizeof(uint16_t));
    card_dsc.data_size = bytes;
    card_dsc.data = (const uint8_t *)card_pixels;
    ESP_LOGI(TAG, "Cached rounded-shadow card %ldx%ld RGB565 in %u B PSRAM",
             (long)width, (long)height, (unsigned)bytes);
    return true;
}

const lv_image_dsc_t *mira_card_cache_image(void) {
    return card_pixels ? &card_dsc : NULL;
}
