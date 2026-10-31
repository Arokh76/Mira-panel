// Mira LVGL9 scroll benchmark: compare raster cost of card styles.
// Single UI source, independent of Waveshare hardware adapter.
#include <stdbool.h>
#include <stdint.h>
#include "mira_ui.h"
#include "esp_log.h"

enum { CARD_NO_SHADOW = 0, CARD_NATIVE_SHADOW = 1,
       CARD_LIGHT_SHADOW = 2, CARD_RELIEF_OUTLINE = 3 };
#define CARD_COUNT 35

static lv_obj_t *scroll_list;
static lv_obj_t *fps_label;
static lv_obj_t *auto_label;
static lv_obj_t *shadow_label;
static lv_obj_t *cards[CARD_COUNT];
static bool automatic = true;
static bool scrolling_down = true;
static int shadow_mode = -1;
static const char *TAG = "mira-compare";

static void toggle_scroll(lv_event_t *event) {
    (void)event;
    automatic = !automatic;
    lv_label_set_text(auto_label, automatic ? "AUTO: ON" : "AUTO: OFF");
}

void mira_ui_set_shadow_mode(int mode) {
    if (mode < CARD_NO_SHADOW || mode > CARD_RELIEF_OUTLINE) return;
    if (shadow_mode == mode) return;
    shadow_mode = mode;
    const char *names[] = { "OMBRE: OFF", "OMBRE: LVGL",
                            "OMBRE: LEGERE", "RELIEF: ON" };
    if (shadow_label) lv_label_set_text(shadow_label, names[mode]);

    // Change only the rendering style, not the cards, child count, fonts,
    // scroll speed or visible area. No PSRAM image cache, no image objects.
    for (int k = 0; k < CARD_COUNT; k++) {
        lv_obj_t *card = cards[k];
        if (!card) continue;
        lv_obj_set_style_shadow_width(card,
            mode == CARD_NATIVE_SHADOW ? 4 :
            (mode == CARD_LIGHT_SHADOW ? 2 : 0), 0);
        lv_obj_set_style_shadow_opa(card,
            mode == CARD_LIGHT_SHADOW ? LV_OPA_50 : LV_OPA_COVER, 0);
        lv_obj_set_style_shadow_color(card, lv_color_hex(0x000000), 0);

        // Cheap solid outline approximates a shadow/relief without LVGL blur.
        // Deliberately distinct from a true softened shadow.
        lv_obj_set_style_outline_width(card,
            mode == CARD_RELIEF_OUTLINE ? 2 : 0, 0);
        lv_obj_set_style_outline_opa(card,
            mode == CARD_RELIEF_OUTLINE ? LV_OPA_50 : LV_OPA_TRANSP, 0);
        lv_obj_set_style_outline_pad(card, 1, 0);
        lv_obj_set_style_outline_color(card, lv_color_hex(0x000000), 0);

        // Preserve original rounded corners, fill, and 1px border in ALL modes.
        lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(card, 14, 0);
        lv_obj_set_style_border_width(card, 1, 0);
    }
    ESP_LOGI(TAG, "Card style mode=%d (0=OFF 1=LVGL4px 2=LIGHT2px 3=RELIEF)", mode);
}
int mira_ui_shadow_mode(void) { return shadow_mode; }
void mira_ui_set_shadows(bool enabled) {
    mira_ui_set_shadow_mode(enabled ? CARD_NATIVE_SHADOW : CARD_NO_SHADOW);
}
static void toggle_shadows(lv_event_t *event) {
    (void)event;
    mira_ui_set_shadow_mode((shadow_mode + 1) % 4);
}
static void auto_tick(lv_timer_t *timer) {
    (void)timer;
    if (!scroll_list || !automatic) return;
    if (scrolling_down && lv_obj_get_scroll_bottom(scroll_list) <= 3)
        scrolling_down = false;
    if (!scrolling_down && lv_obj_get_scroll_top(scroll_list) <= 3)
        scrolling_down = true;
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

    lv_obj_t *touch_label = lv_label_create(screen);
    lv_obj_set_pos(touch_label, 277, 19);
    lv_obj_set_style_text_color(touch_label,
        lv_color_hex(touch_ready ? 0x86eead : 0xff7777), 0);
    lv_label_set_text(touch_label, touch_ready ? "GT911 I2C: OK" : "GT911: ABSENT");

    lv_obj_t *shadow_btn = lv_button_create(screen);
    lv_obj_set_pos(shadow_btn, 460, 5);
    lv_obj_set_size(shadow_btn, 163, 45);
    lv_obj_add_event_cb(shadow_btn, toggle_shadows, LV_EVENT_CLICKED, NULL);
    shadow_label = lv_label_create(shadow_btn);
    lv_label_set_text(shadow_label, "OMBRE: OFF");
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

    for (int i = 0; i < CARD_COUNT; i++) {
        lv_obj_t *card = lv_obj_create(scroll_list);
        cards[i] = card;
        lv_obj_set_width(card, lv_pct(100));
        lv_obj_set_height(card, 86);
        lv_obj_set_style_bg_color(card, lv_color_hex(0x173149), 0);
        lv_obj_set_style_radius(card, 14, 0);
        lv_obj_set_style_shadow_width(card, 0, 0);
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
    // Default to the known-good bare-card baseline. Switch using /shadow/
    // even if GT911 still does not return valid touch events.
    mira_ui_set_shadow_mode(CARD_NO_SHADOW);
    lv_timer_create(auto_tick, 16, NULL);
}
