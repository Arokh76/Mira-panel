// Mira ESP-IDF / LVGL9 scrolling A/B benchmark (native vs virtualized).
// Scroll speed, viewport, colors and card geometry are IDENTICAL across modes.
// ONLY the number of instantiated, drawable card objects changes.
// RGB panel EDMA framebuffer synchronization is untouched.
#include <stdbool.h>
#include <stdint.h>
#include "mira_ui.h"
#include "esp_log.h"

#define CARD_COUNT 35
#define VIRTUAL_SLOTS 8
#define CARD_H 86
#define CARD_GAP 8
#define CARD_STEP (CARD_H + CARD_GAP)
enum { STYLE_OFF = 0, STYLE_NATIVE = 1, STYLE_LIGHT = 2, STYLE_RELIEF = 3 };
enum { SCROLL_CLASSIC = 0, SCROLL_VIRTUAL = 1 };

static lv_obj_t *native_list;
static lv_obj_t *virtual_list;
static lv_obj_t *active_list;
static lv_obj_t *fps_label, *auto_label, *shadow_label, *scroll_label;
static lv_obj_t *native_cards[CARD_COUNT];
static lv_obj_t *virtual_cards[VIRTUAL_SLOTS];
static lv_obj_t *virtual_titles[VIRTUAL_SLOTS];
static int virtual_indices[VIRTUAL_SLOTS];
static bool automatic = true;
static bool scrolling_down = true;
static int style_mode = -1;
static int scroll_mode = SCROLL_CLASSIC;
static const char *TAG = "mira-scroll-bench";

static void apply_style(lv_obj_t *card) {
    if (!card || style_mode < 0) return;
    lv_obj_set_style_shadow_width(card,
        style_mode == STYLE_NATIVE ? 4 : (style_mode == STYLE_LIGHT ? 2 : 0), 0);
    lv_obj_set_style_shadow_opa(card, style_mode == STYLE_LIGHT ? LV_OPA_50 : LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_color(card, lv_color_hex(0x000000), 0);
    lv_obj_set_style_outline_width(card, style_mode == STYLE_RELIEF ? 2 : 0, 0);
    lv_obj_set_style_outline_opa(card, style_mode == STYLE_RELIEF ? LV_OPA_50 : LV_OPA_TRANSP, 0);
    lv_obj_set_style_outline_pad(card, 1, 0);
    lv_obj_set_style_outline_color(card, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 14, 0);
    lv_obj_set_style_border_width(card, 1, 0);
}
void mira_ui_set_shadow_mode(int mode) {
    if (mode < STYLE_OFF || mode > STYLE_RELIEF || style_mode == mode) return;
    style_mode = mode;
    const char *names[] = {"OMBRE: OFF", "OMBRE: LVGL", "OMBRE: LEGERE", "RELIEF: ON"};
    if (shadow_label) lv_label_set_text(shadow_label, names[mode]);
    for (int i = 0; i < CARD_COUNT; i++) apply_style(native_cards[i]);
    for (int i = 0; i < VIRTUAL_SLOTS; i++) apply_style(virtual_cards[i]);
    ESP_LOGI(TAG, "Card style %d", style_mode);
}
int mira_ui_shadow_mode(void) { return style_mode; }
void mira_ui_set_shadows(bool enable) { mira_ui_set_shadow_mode(enable ? STYLE_NATIVE : STYLE_OFF); }

static void virtual_refresh(void) {
    if (!virtual_list) return;
    // Each physical slot is recycled only after its previous row has
    // scrolled entirely outside the viewport. One extra row on each side.
    int top = lv_obj_get_scroll_top(virtual_list);
    if (top < 0) top = 0;
    int first = top / CARD_STEP - 1;
    if (first < 0) first = 0;
    for (int n = first; n < first + VIRTUAL_SLOTS && n < CARD_COUNT; n++) {
        int slot = n % VIRTUAL_SLOTS;
        if (virtual_indices[slot] == n) continue;
        lv_obj_set_y(virtual_cards[slot], n * CARD_STEP);
        lv_label_set_text_fmt(virtual_titles[slot], "Mira ESP-IDF - carte %02d", n + 1);
        lv_obj_remove_flag(virtual_cards[slot], LV_OBJ_FLAG_HIDDEN);
        virtual_indices[slot] = n;
    }
    // When approaching the end, no stale recycled slots should remain visible.
    // Slots not in the active range will always sit entirely out of view.
}
void mira_ui_set_scroll_mode(int mode) {
    if (mode < SCROLL_CLASSIC || mode > SCROLL_VIRTUAL) return;
    if (scroll_mode == mode) return;
    int y = active_list ? lv_obj_get_scroll_top(active_list) : 0;
    scroll_mode = mode;
    lv_obj_t *next = mode == SCROLL_VIRTUAL ? virtual_list : native_list;
    if (!next) return;
    lv_obj_add_flag(active_list, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(next, LV_OBJ_FLAG_HIDDEN);
    active_list = next;
    // Keep scroll position and AUTO speed for a meaningful A/B comparison.
    lv_obj_scroll_to_y(active_list, y, LV_ANIM_OFF);
    if (scroll_mode == SCROLL_VIRTUAL) virtual_refresh();
    if (scroll_label) lv_label_set_text(scroll_label,
        scroll_mode == SCROLL_VIRTUAL ? "SCROLL: VIRT" : "SCROLL: 35");
    ESP_LOGI(TAG, "Scroll mode %d, offset=%ldpx (0=35 cards 1=8 recycled)",
             scroll_mode, (long)lv_obj_get_scroll_top(active_list));
}
int mira_ui_scroll_mode(void) { return scroll_mode; }
static void toggle_scroll_mode(lv_event_t *event) {
    (void)event;
    mira_ui_set_scroll_mode(scroll_mode == SCROLL_CLASSIC ? SCROLL_VIRTUAL : SCROLL_CLASSIC);
}
static void toggle_shadows(lv_event_t *event) {
    (void)event;
    mira_ui_set_shadow_mode((style_mode + 1) % 4);
}
static void toggle_scroll_auto(lv_event_t *event) {
    (void)event;
    automatic = !automatic;
    lv_label_set_text(auto_label, automatic ? "AUTO: ON" : "AUTO: OFF");
}
static void auto_tick(lv_timer_t *timer) {
    (void)timer;
    if (!automatic || !active_list) return;
    if (scrolling_down && lv_obj_get_scroll_bottom(active_list) <= 3)
        scrolling_down = false;
    if (!scrolling_down && lv_obj_get_scroll_top(active_list) <= 3)
        scrolling_down = true;
    // Same six-pixel step and 16ms timer as the original baseline.
    lv_obj_scroll_by(active_list, 0, scrolling_down ? -6 : 6, LV_ANIM_OFF);
    if (scroll_mode == SCROLL_VIRTUAL) virtual_refresh();
}
void mira_ui_update_fps(uint32_t fps_tenths) {
    if (fps_label) lv_label_set_text_fmt(fps_label, "IDF FPS: %lu.%01lu",
            (unsigned long)(fps_tenths / 10), (unsigned long)(fps_tenths % 10));
}
static lv_obj_t *make_card(lv_obj_t *parent, int row, lv_obj_t **title_out) {
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_width(card, lv_pct(100));
    lv_obj_set_height(card, CARD_H);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x173149), 0);
    lv_obj_set_style_radius(card, 14, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_shadow_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *title = lv_label_create(card);
    lv_label_set_text_fmt(title, "Mira ESP-IDF - carte %02d", row + 1);
    lv_obj_set_style_text_color(title, lv_color_hex(0xeaf7ff), 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 12, 8);
    lv_obj_t *sub = lv_label_create(card);
    lv_label_set_text(sub, "Test scroll RGB565 / ESP32-S3 / LVGL 9.6");
    lv_obj_set_style_text_color(sub, lv_color_hex(0x92b6ca), 0);
    lv_obj_align(sub, LV_ALIGN_TOP_LEFT, 12, 34);
    if (title_out) *title_out = title;
    return card;
}
static lv_obj_t *make_viewport(lv_obj_t *screen) {
    lv_obj_t *list = lv_obj_create(screen);
    lv_obj_set_pos(list, 18, 58);
    lv_obj_set_size(list, 764, 407);
    lv_obj_set_style_pad_all(list, 8, 0);
    lv_obj_set_style_pad_row(list, CARD_GAP, 0);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_color(list, lv_color_hex(0x071421), 0);
    return list;
}
void mira_ui_bench_create(lv_display_t *disp, bool touch_ready) {
    lv_obj_t *screen = lv_display_get_screen_active(disp);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x071421), 0);
    lv_obj_set_style_pad_all(screen, 0, 0);
    lv_obj_set_style_border_width(screen, 0, 0);

    fps_label = lv_label_create(screen);
    lv_obj_set_style_text_color(fps_label, lv_color_hex(0x86eead), 0);
    lv_obj_set_style_text_font(fps_label, &lv_font_montserrat_20, 0);
    lv_obj_set_pos(fps_label, 12, 16);
    lv_label_set_text(fps_label, "IDF FPS: --");

    lv_obj_t *touch_label = lv_label_create(screen);
    lv_obj_set_pos(touch_label, 206, 19);
    lv_obj_set_style_text_color(touch_label,
        lv_color_hex(touch_ready ? 0x86eead : 0xff7777), 0);
    lv_label_set_text(touch_label, touch_ready ? "GT911 I2C: OK" : "GT911: ABSENT");

    lv_obj_t *mode_btn = lv_button_create(screen);
    lv_obj_set_pos(mode_btn, 355, 5);
    lv_obj_set_size(mode_btn, 145, 45);
    lv_obj_add_event_cb(mode_btn, toggle_scroll_mode, LV_EVENT_CLICKED, NULL);
    scroll_label = lv_label_create(mode_btn);
    lv_label_set_text(scroll_label, "SCROLL: 35");
    lv_obj_center(scroll_label);

    lv_obj_t *shadow_btn = lv_button_create(screen);
    lv_obj_set_pos(shadow_btn, 510, 5);
    lv_obj_set_size(shadow_btn, 135, 45);
    lv_obj_add_event_cb(shadow_btn, toggle_shadows, LV_EVENT_CLICKED, NULL);
    shadow_label = lv_label_create(shadow_btn);
    lv_label_set_text(shadow_label, "OMBRE: OFF");
    lv_obj_center(shadow_label);

    lv_obj_t *auto_btn = lv_button_create(screen);
    lv_obj_set_pos(auto_btn, 655, 5);
    lv_obj_set_size(auto_btn, 130, 45);
    lv_obj_add_event_cb(auto_btn, toggle_scroll_auto, LV_EVENT_CLICKED, NULL);
    auto_label = lv_label_create(auto_btn);
    lv_label_set_text(auto_label, "AUTO: ON");
    lv_obj_center(auto_label);

    native_list = make_viewport(screen);
    lv_obj_set_flex_flow(native_list, LV_FLEX_FLOW_COLUMN);
    for (int i = 0; i < CARD_COUNT; i++)
        native_cards[i] = make_card(native_list, i, NULL);

    virtual_list = make_viewport(screen);
    // No flex: children are positioned at absolute content coordinates.
    // A 1px transparent spacer establishes the same scrollable height as
    // the 35-card list while needing no additional rasterization.
    lv_obj_t *spacer = lv_obj_create(virtual_list);
    lv_obj_set_size(spacer, 1, 1);
    lv_obj_set_pos(spacer, 0, (CARD_COUNT - 1) * CARD_STEP + CARD_H);
    lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(spacer, 0, 0);
    lv_obj_set_style_pad_all(spacer, 0, 0);
    lv_obj_remove_flag(spacer, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    for (int i = 0; i < VIRTUAL_SLOTS; i++) {
        virtual_indices[i] = i;
        virtual_cards[i] = make_card(virtual_list, i, &virtual_titles[i]);
        lv_obj_set_pos(virtual_cards[i], 0, i * CARD_STEP);
    }
    lv_obj_add_flag(virtual_list, LV_OBJ_FLAG_HIDDEN);
    active_list = native_list;
    // No shadows is the exact 9.7 FPS baseline. Test other styles independently.
    mira_ui_set_shadow_mode(STYLE_OFF);
    lv_timer_create(auto_tick, 16, NULL);
}
