// Shared LVGL9 scrolling layout benchmark: no hardware APIs.
// A future Mira core can reuse this interface with another display adapter.
#include <stdbool.h>
#include <stdio.h>
#include "mira_ui.h"

static lv_obj_t *scroll_list;
static lv_obj_t *fps_label;
static lv_obj_t *auto_label;
static bool automatic = true;
static bool scrolling_down = true;
static bool show_shadows = true;
static lv_obj_t *shadow_label;
static lv_obj_t *cards[35];

static void toggle_scroll(lv_event_t *event) {
    (void)event;
    automatic = !automatic;
    lv_label_set_text(auto_label, automatic ? "AUTO: ON" : "AUTO: OFF");
}
void mira_ui_set_shadows(bool enabled) {
    if (show_shadows == enabled) return;
    show_shadows = enabled;
    if (shadow_label)
        lv_label_set_text(shadow_label, show_shadows ? "OMBRE: ON" : "OMBRE: OFF");
    // Called only from LVGL render task, even for HTTP commands.
    for (int i = 0; i < 35; i++) {
        if (cards[i]) lv_obj_set_style_shadow_width(cards[i], show_shadows ? 4 : 0, 0);
    }
}
static void toggle_shadows(lv_event_t *event) {
    (void)event;
    mira_ui_set_shadows(!show_shadows);
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

    lv_obj_t *touch_label = lv_label_create(screen);
    lv_obj_set_pos(touch_label, 277, 19);
    lv_obj_set_style_text_color(touch_label,
         lv_color_hex(touch_ready ? 0x86eead : 0xff7777), 0);
    lv_label_set_text(touch_label, touch_ready ? "GT911: OK" : "GT911: ERREUR");

    lv_obj_t *shadow_btn = lv_button_create(screen);
    lv_obj_set_pos(shadow_btn, 460, 5);
    lv_obj_set_size(shadow_btn, 163, 45);
    lv_obj_add_event_cb(shadow_btn, toggle_shadows, LV_EVENT_CLICKED, NULL);
    shadow_label = lv_label_create(shadow_btn);
    lv_label_set_text(shadow_label, "OMBRE: ON");
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
    lv_timer_create(auto_tick, 16, NULL);
}
