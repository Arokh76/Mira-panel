// Mira LVGL9 scroll benchmark: compare raster cost of card styles.
// Single UI source, independent of Waveshare hardware adapter.
#include <stdbool.h>
#include <stdint.h>
#include "mira_ui.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include <string.h>

enum { CARD_NO_SHADOW = 0, CARD_NATIVE_SHADOW = 1,
       CARD_LIGHT_SHADOW = 2, CARD_RELIEF_OUTLINE = 3 };
#define CARD_COUNT 35
#define VIRTUAL_SLOTS 8
#define CARD_HEIGHT 86
#define CARD_STEP 94

static lv_obj_t *scroll_list;
static lv_obj_t *fps_label;
static lv_obj_t *auto_label;
static lv_obj_t *shadow_label;
static lv_obj_t *cards[CARD_COUNT];
static lv_obj_t *virtual_titles[VIRTUAL_SLOTS];
static int virtual_indices[VIRTUAL_SLOTS];
static int active_card_count = CARD_COUNT;
static int scroll_mode = 0; // 0 = 35 original LVGL cards, 1 = 8 recycled
static uint32_t scroll_switches;
static uint32_t scroll_rebuild_us;
static bool automatic = true;
static bool scrolling_down = true;
static int shadow_mode = -1;
static const char *TAG = "mira-compare";
static void apply_style(lv_obj_t *card, int mode);
static lv_obj_t *make_card(int row, lv_obj_t **title_out);

void mira_ui_set_auto_scroll(bool enabled) {
    if (automatic == enabled) return;
    automatic = enabled;
    if (auto_label) lv_label_set_text(auto_label, automatic ? "AUTO: ON" : "AUTO: OFF");
    ESP_LOGI(TAG, "Automatic scrolling %s", automatic ? "ON" : "OFF");
}
bool mira_ui_auto_scroll(void) { return automatic; }
static void toggle_scroll(lv_event_t *event) {
    (void)event;
    mira_ui_set_auto_scroll(!automatic);
}

static void apply_style(lv_obj_t *card, int mode) {
    if (!card) return;
    lv_obj_set_style_shadow_width(card,
        mode == CARD_NATIVE_SHADOW ? 4 :
        (mode == CARD_LIGHT_SHADOW ? 2 : 0), 0);
    lv_obj_set_style_shadow_opa(card,
        mode == CARD_LIGHT_SHADOW ? LV_OPA_50 : LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_color(card, lv_color_hex(0x000000), 0);
    lv_obj_set_style_outline_width(card,
        mode == CARD_RELIEF_OUTLINE ? 2 : 0, 0);
    lv_obj_set_style_outline_opa(card,
        mode == CARD_RELIEF_OUTLINE ? LV_OPA_50 : LV_OPA_TRANSP, 0);
    lv_obj_set_style_outline_pad(card, 1, 0);
    lv_obj_set_style_outline_color(card, lv_color_hex(0x000000), 0);
    // Original panel geometry and rounded corners retained in all 4 modes.
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 14, 0);
    lv_obj_set_style_border_width(card, 1, 0);
}
void mira_ui_set_shadow_mode(int mode) {
    if (mode < CARD_NO_SHADOW || mode > CARD_RELIEF_OUTLINE || shadow_mode == mode) return;
    shadow_mode = mode;
    const char *names[] = { "OMBRE: OFF", "OMBRE: LVGL",
                            "OMBRE: LEGERE", "RELIEF: ON" };
    if (shadow_label) lv_label_set_text(shadow_label, names[mode]);
    for (int k = 0; k < active_card_count; k++) apply_style(cards[k], mode);
    ESP_LOGI(TAG, "Card style mode=%d, cards=%d", mode, active_card_count);
}
int mira_ui_shadow_mode(void) { return shadow_mode; }
void mira_ui_set_shadows(bool enabled) {
    mira_ui_set_shadow_mode(enabled ? CARD_NATIVE_SHADOW : CARD_NO_SHADOW);
}
static void toggle_shadows(lv_event_t *event) {
    (void)event;
    mira_ui_set_shadow_mode((shadow_mode + 1) % 4);
}
// On boot we create EXACTLY the previously working 35-card flex list.
// Virtualization is never initialized until explicitly requested via Wi-Fi.
// A crash in an optional mode therefore cannot trap the next reboot in it.
static lv_obj_t *make_card(int row, lv_obj_t **title_out) {
    lv_obj_t *card = lv_obj_create(scroll_list);
    lv_obj_set_width(card, lv_pct(100));
    lv_obj_set_height(card, CARD_HEIGHT);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x173149), 0);
    lv_obj_set_style_radius(card, 14, 0);
    lv_obj_set_style_shadow_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_set_style_border_width(card, 1, 0);

    lv_obj_t *title = lv_label_create(card);
    lv_label_set_text_fmt(title, "Mira ESP-IDF - carte %02d", row + 1);
    lv_obj_set_style_text_color(title, lv_color_hex(0xeaf7ff), 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 12, 8);

    lv_obj_t *sub = lv_label_create(card);
    lv_label_set_text(sub, "Test scroll RGB565 / ESP32-S3 / LVGL 9.6");
    lv_obj_set_style_text_color(sub, lv_color_hex(0x92b6ca), 0);
    lv_obj_align(sub, LV_ALIGN_TOP_LEFT, 12, 34);
    if (title_out) *title_out = title;
    if (shadow_mode >= 0) apply_style(card, shadow_mode);
    return card;
}

static void virtual_refresh(void) {
    if (scroll_mode != 1 || !scroll_list) return;
    int top = lv_obj_get_scroll_top(scroll_list);
    if (top < 0) top = 0;
    int first = top / CARD_STEP - 1;
    if (first < 0) first = 0;
    // Recycle at most one offscreen card at a time. A buffer on either
    // side keeps 6px scroll steps from exposing an unfinished row.
    for (int row = first; row < first + VIRTUAL_SLOTS && row < CARD_COUNT; row++) {
        const int slot = row % VIRTUAL_SLOTS;
        if (virtual_indices[slot] == row) continue;
        lv_obj_set_pos(cards[slot], 0, row * CARD_STEP);
        lv_label_set_text_fmt(virtual_titles[slot],
                              "Mira ESP-IDF - carte %02d", row + 1);
        virtual_indices[slot] = row;
    }
}
void mira_ui_set_scroll_mode(int mode) {
    if (mode != 0 && mode != 1) return;
    if (mode == scroll_mode || !scroll_list) return;
    // All LVGL modifications happen only on the rendering task.
    const int64_t t0 = esp_timer_get_time();
    int old_scroll = lv_obj_get_scroll_top(scroll_list);
    if (old_scroll < 0) old_scroll = 0;

    // Only ONE scroll container ever exists, never two overlapping scenes.
    // Delete the active rows before rebuilding. Restart always uses mode 0.
    lv_obj_clean(scroll_list);
    memset(cards, 0, sizeof(cards));
    memset(virtual_titles, 0, sizeof(virtual_titles));
    active_card_count = mode ? VIRTUAL_SLOTS : CARD_COUNT;
    if (mode == 0) {
        lv_obj_set_flex_flow(scroll_list, LV_FLEX_FLOW_COLUMN);
        for (int row = 0; row < CARD_COUNT; row++) cards[row] = make_card(row, NULL);
    } else {
        lv_obj_set_layout(scroll_list, LV_LAYOUT_NONE);
        // Transparent geometry-only spacer: establishes the logical 35-row
        // scroll height. No RGB image, no second viewport, no sprite/cache.
        lv_obj_t *spacer = lv_obj_create(scroll_list);
        lv_obj_set_pos(spacer, 0, (CARD_COUNT - 1) * CARD_STEP + CARD_HEIGHT - 1);
        lv_obj_set_size(spacer, 1, 1);
        lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(spacer, 0, 0);
        lv_obj_set_style_pad_all(spacer, 0, 0);
        lv_obj_remove_flag(spacer, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        for (int row = 0; row < VIRTUAL_SLOTS; row++) {
            virtual_indices[row] = row;
            cards[row] = make_card(row, &virtual_titles[row]);
            lv_obj_set_pos(cards[row], 0, row * CARD_STEP);
        }
    }
    scroll_mode = mode;
    lv_obj_update_layout(scroll_list);
    lv_obj_scroll_to_y(scroll_list, old_scroll, LV_ANIM_OFF);
    if (scroll_mode == 1) virtual_refresh();
    scroll_switches++;
    scroll_rebuild_us = (uint32_t)(esp_timer_get_time() - t0);
    ESP_LOGW(TAG, "Scroll mode=%d cards=%d rebuild=%luus free heap=%u PSRAM=%u",
             scroll_mode, active_card_count, (unsigned long)scroll_rebuild_us,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}
int mira_ui_scroll_mode(void) { return scroll_mode; }
uint32_t mira_ui_scroll_switches(void) { return scroll_switches; }
uint32_t mira_ui_scroll_rebuild_us(void) { return scroll_rebuild_us; }

static void auto_tick(lv_timer_t *timer) {
    (void)timer;
    if (!scroll_list || !automatic) return;
    if (scrolling_down && lv_obj_get_scroll_bottom(scroll_list) <= 3)
        scrolling_down = false;
    if (!scrolling_down && lv_obj_get_scroll_top(scroll_list) <= 3)
        scrolling_down = true;
    lv_obj_scroll_by(scroll_list, 0, scrolling_down ? -6 : 6, LV_ANIM_OFF);
    if (scroll_mode == 1) virtual_refresh();
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

    // Identical startup to the previous working firmware: ONE flex list,
    // 35 normal cards, no virtual objects or extra containers allocated.
    for (int i = 0; i < CARD_COUNT; i++) cards[i] = make_card(i, NULL);
    // Default to the known-good bare-card baseline. Switch using /shadow/
    // even if GT911 still does not return valid touch events.
    mira_ui_set_shadow_mode(CARD_NO_SHADOW);
    lv_timer_create(auto_tick, 16, NULL);
}
