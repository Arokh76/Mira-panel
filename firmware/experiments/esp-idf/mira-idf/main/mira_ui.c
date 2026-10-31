// Shared LVGL9 scrolling layout benchmark: no hardware APIs.
// A future Mira core can reuse this interface with another display adapter.
#include <stdbool.h>
#include <stdio.h>
#include "mira_ui.h"
#include "mira_card_cache.h"
#include "esp_log.h"

static lv_obj_t *scroll_list;
static lv_obj_t *fps_label;
static lv_obj_t *auto_label;
static bool automatic = true;
static bool scrolling_down = true;
// Render comparisons, keep all geometry, labels and scrolling rate constant.
enum { SHADOW_OFF = 0, SHADOW_LVGL = 1, SHADOW_CACHE = 2 };
static int shadow_mode = -1;
static bool card_cache_ready;
static lv_obj_t *shadow_label;
static lv_obj_t *touch_label;
static lv_obj_t *cards[35];
static lv_obj_t *card_images[35];
static const char *TAG = "mira-compare";

static void toggle_scroll(lv_event_t *event) {
    (void)event;
    automatic = !automatic;
    lv_label_set_text(auto_label, automatic ? "AUTO: ON" : "AUTO: OFF");
}
void mira_ui_set_shadow_mode(int mode) {
    if (mode < SHADOW_OFF || mode > SHADOW_CACHE) return;
    if (mode == SHADOW_CACHE && !card_cache_ready) mode = SHADOW_LVGL;
    if (shadow_mode == mode) return;
    shadow_mode = mode;
    if (shadow_label) {
        lv_label_set_text(shadow_label,
            mode == SHADOW_CACHE ? "OMBRE: CACHE" :
            (mode == SHADOW_LVGL ? "OMBRE: LVGL" : "OMBRE: OFF"));
    }
    for (int k = 0; k < 35; k++) {
        lv_obj_t *card = cards[k];
        if (!card) continue;
        if (mode == SHADOW_CACHE) {
            lv_obj_set_style_bg_opa(card, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(card, 0, 0);
            lv_obj_set_style_shadow_width(card, 0, 0);
            lv_obj_set_style_radius(card, 0, 0);
            if (card_images[k]) lv_obj_remove_flag(card_images[k], LV_OBJ_FLAG_HIDDEN);
        } else {
            if (card_images[k]) lv_obj_add_flag(card_images[k], LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(card, 1, 0);
            lv_obj_set_style_shadow_width(card, mode == SHADOW_LVGL ? 4 : 0, 0);
            lv_obj_set_style_radius(card, 14, 0);
        }
    }
    ESP_LOGI(TAG, "Shadow mode %d: 0=OFF, 1=LVGL, 2=CACHE", mode);
}
int mira_ui_shadow_mode(void) { return shadow_mode; }
void mira_ui_set_shadows(bool enabled) {
    mira_ui_set_shadow_mode(enabled ? SHADOW_LVGL : SHADOW_OFF);
}
static void toggle_shadows(lv_event_t *event) {
    (void)event;
    // Touch button cycles: CACHED -> LVGL -> OFF -> CACHED.
    mira_ui_set_shadow_mode(shadow_mode == SHADOW_CACHE ? SHADOW_LVGL :
                            (shadow_mode == SHADOW_LVGL ? SHADOW_OFF : SHADOW_CACHE));
}
static void auto_tick(lv_timer_t *timer) {
    (void)timer;
    if (!scroll_list || !automatic) return;
    if (scrolling_down && lv_obj_get_scroll_bottom(scroll_list) <= 3) scrolling_down = false;
    if (!scrolling_down && lv_obj_get_scroll_top(scroll_list) <= 3) scrolling_down = true;
    lv_obj_scroll_by(scroll_list, 0, scrolling_down ? -6 : 6, LV_ANIM_OFF);
}
void mira_ui_update_fps(uint32_t fps_tenths) {
    if (fps_label) {
        lv_label_set_text_fmt(fps_label, "ESP-IDF | FPS: %lu.%01lu",
                         (unsigned long)(fps_tenths / 10),
                         (unsigned long)(fps_tenths % 10));
    }
}
void mira_ui_bench_create(lv_display_t *disp, bool touch_ready) {
    lv_obj_t *screen = lv_display_get_screen_active(disp);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x071421), 0);
    lv_obj_set_style_pad_all(screen, 0, 0);
    lv_obj_set_style_border_width(screen, 0, 0);

    fps_label = lv_label_create(screen);
    lv_obj_set_style_text_color(fps_label, lv_color_hex(0x86eead), 0);
    lv_obj_set_style_text_font(fps_label, &lv_font_montserrat_20, 0);
    lv_obj_set_pos(fps_label, 22, 16);
    lv_label_set_text(fps_label, "ESP-IDF | FPS: --");

    touch_label = lv_label_create(screen);
    lv_obj_set_pos(touch_label, 277, 19);
    lv_obj_set_style_text_color(touch_label,
         lv_color_hex(touch_ready ? 0x86eead : 0xff7777), 0);
    lv_label_set_text(touch_label, touch_ready ? "GT911 I2C: OK" : "GT911: ABSENT");

    lv_obj_t *shadow_btn = lv_button_create(screen);
    lv_obj_set_pos(shadow_btn, 460, 5);
    lv_obj_set_size(shadow_btn, 163, 45);
    lv_obj_add_event_cb(shadow_btn, toggle_shadows, LV_EVENT_CLICKED, NULL);
    shadow_label = lv_label_create(shadow_btn);
    lv_label_set_text(shadow_label, "OMBRE: LVGL");
    lv_obj_center(shadow_label);

    lv_obj_t *btn = lv_button_create(screen);
    lv_obj_set_pos(btn, 630, 5);
    lv_obj_set_size(btn, 155, 45);
    lv_obj_add_event_cb(btn, toggle_scroll, LV_EVENT_CLICKED, NULL);
    auto_label = lv_label_create(btn);
    lv_label_set_text(auto_label, "AUTO: ON");
    lv_obj_center(auto_label);

    scroll_list = lv_obj_create(screen);
    lv_obj_set_pos(scroll_list, 18, 58);
    lv_obj_set_size(scroll_list, 764, 407);
    lv_obj_set_flex_flow(scroll_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(scroll_list, 8, 0);
    lv_obj_set_style_pad_row(scroll_list, 8, 0);
    lv_obj_set_scroll_dir(scroll_list, LV_DIR_VER);
    lv_obj_set_style_bg_color(scroll_list, lv_color_hex(0x071421), 0);

    for (int i = 0; i < 35; i++) {
        lv_obj_t *card = lv_obj_create(scroll_list);
        cards[i] = card;
        lv_obj_set_width(card, lv_pct(100));
        lv_obj_set_height(card, 86);
        lv_obj_set_style_bg_color(card, lv_color_hex(0x173149), 0);
        lv_obj_set_style_radius(card, 14, 0);
        lv_obj_set_style_shadow_width(card, 4, 0);
        lv_obj_set_style_pad_all(card, 0, 0);
        lv_obj_set_style_border_width(card, 1, 0);

        lv_obj_t *title = lv_label_create(card);
        lv_label_set_text_fmt(title, "Mira ESP-IDF - carte %02d", i + 1);
        lv_obj_set_style_text_color(title, lv_color_hex(0xeaf7ff), 0);
        lv_obj_align(title, LV_ALIGN_TOP_LEFT, 12, 8);

        lv_obj_t *sub = lv_label_create(card);
        lv_label_set_text(sub, "Test scroll RGB565 / ESP32-S3 / LVGL 9.6");
        lv_obj_set_style_text_color(sub, lv_color_hex(0x92b6ca), 0);
        lv_obj_align(sub, LV_ALIGN_TOP_LEFT, 12, 34);
    }
    // The single cached bitmap is reused by all 35 cards; never allocate
    // one image per card. The image is added behind live text labels.
    lv_obj_update_layout(screen);
    int32_t card_width = lv_obj_get_width(cards[0]);
    card_cache_ready = mira_card_cache_prepare(card_width, 86);
    if (card_cache_ready) {
        const lv_image_dsc_t *cached = mira_card_cache_image();
        for (int k = 0; k < 35; k++) {
            lv_obj_t *img = lv_image_create(cards[k]);
            card_images[k] = img;
            lv_image_set_src(img, cached);
            lv_obj_set_pos(img, 0, 0);
            lv_obj_move_to_index(img, 0);
            lv_obj_add_flag(img, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(img, LV_OBJ_FLAG_CLICKABLE);
        }
        // Default to the optimized, visually shadowed renderer for testing.
        // Compare CACHED / LVGL / OFF using Wi-Fi or the button.
        mira_ui_set_shadow_mode(SHADOW_CACHE);
    } else {
        mira_ui_set_shadow_mode(SHADOW_LVGL);
    }
    lv_timer_create(auto_tick, 16, NULL);
}
