#include "MiraPanelUI.h"
#include "MiraMqtt.h"
#include "MiraFrenchFont.h"
#include <WiFi.h>
#include <time.h>

#if defined(MIRA_TARGET_WAVESHARE_43C)
  #if LVGL_VERSION_MAJOR != 8 || LVGL_VERSION_MINOR != 4
    #error "Mira Waveshare requires the official Waveshare LVGL 8.4 library"
  #endif
#endif

lv_disp_draw_buf_t MiraPanelUI::_drawBuffer;
#if MIRA_RGB_DIRECT_FRAMEBUFFER
lv_color_t* MiraPanelUI::_buf1 = nullptr;
lv_color_t* MiraPanelUI::_buf2 = nullptr;
#else
lv_color_t MiraPanelUI::_buf1[MIRA_DISPLAY_WIDTH * MIRA_DRAW_BUFFER_LINES];
lv_color_t MiraPanelUI::_buf2[MIRA_DISPLAY_WIDTH * MIRA_DRAW_BUFFER_LINES];
#endif
MiraDisplay* MiraPanelUI::_displayForLvgl = nullptr;
MiraPanelUI* MiraPanelUI::_uiForLvgl = nullptr;

static const lv_color_t COL_TEXT    = lv_color_hex(0xF3F6F7);
static const lv_color_t COL_MUTED   = lv_color_hex(0xA8B2B8);
static const lv_color_t COL_ACCENT  = lv_color_hex(0x22BEEA);
static const lv_color_t COL_ACCENT2 = lv_color_hex(0x18637A);
static const lv_color_t COL_GREEN   = lv_color_hex(0x62D6A7);
static const lv_color_t COL_RED     = lv_color_hex(0xE36A6A);

// Automatic widget grid from screen geometry, without affecting free layout.
// 800px page - 12px margins = 788px; 4*192+3*6 = 2*390+6 = 786px.
static constexpr lv_coord_t MIRA_CARD_AUTO = MIRA_DISPLAY_WIDTH >= 800 ? 390 : 304;
static constexpr lv_coord_t MIRA_CARD_TILE = MIRA_DISPLAY_WIDTH >= 800 ? 192 : 149;
static constexpr lv_coord_t MIRA_CARD_FULL = MIRA_DISPLAY_WIDTH >= 800 ? 786 : 304;
static constexpr lv_coord_t MIRA_LABEL_TILE = MIRA_DISPLAY_WIDTH >= 800 ? 180 : 137;
static constexpr lv_coord_t MIRA_LABEL_AUTO = MIRA_DISPLAY_WIDTH >= 800 ? 370 : 286;
static constexpr lv_coord_t MIRA_LABEL_WIDE_NAME = MIRA_DISPLAY_WIDTH >= 800 ? 440 : 150;
static constexpr lv_coord_t MIRA_LABEL_WIDE_VALUE = MIRA_DISPLAY_WIDTH >= 800 ? 292 : 116;

// Waveshare clock and WT32 clock now share the Montserrat family.
static void miraFitInfoValue(lv_obj_t* label, lv_obj_t* icon);
static void miraMarkInfoArrival(lv_obj_t* label) {
  if (!label) return;
  time_t epoch = time(nullptr);
  if (epoch < 1700000000) { lv_label_set_text(label, "Valeur reçue"); return; }
  struct tm t;
  localtime_r(&epoch, &t);
  char stamp[24];
  snprintf(stamp, sizeof(stamp), "MAJ %02d:%02d", t.tm_hour, t.tm_min);
  lv_label_set_text(label, stamp);
}


static bool miraIconKnown(const String& kind) {
  return kind == "temperature" || kind == "humidity" || kind == "pressure" ||
         kind == "light" || kind == "energy" || kind == "plug" || kind == "door";
}
static String miraResolveInfoIcon(const String& overrideIcon,
                                  const String& name, const String& unit) {
  if (miraIconKnown(overrideIcon)) return overrideIcon;
  if (overrideIcon != "auto" && overrideIcon.length()) return "";
  String n = name, u = unit;
  n.toLowerCase(); u.toLowerCase();
  if (n.indexOf("temp") >= 0 || n.indexOf("thermo") >= 0 ||
      u.indexOf("°") >= 0) return "temperature";
  if (n.indexOf("humid") >= 0 || n.indexOf("hygro") >= 0) return "humidity";
  if (n.indexOf("press") >= 0 || u == "hpa" || u == "mbar") return "pressure";
  if (n.indexOf("lumi") >= 0 || n.indexOf("éclai") >= 0 ||
      n.indexOf("eclai") >= 0 || u == "lux") return "light";
  if (n.indexOf("porte") >= 0 || n.indexOf("fenêtre") >= 0) return "door";
  if (n.indexOf("prise") >= 0) return "plug";
  if (n.indexOf("énergie") >= 0 || n.indexOf("energie") >= 0 ||
      n.indexOf("puissance") >= 0 || u == "w" || u == "kw" ||
      u == "wh" || u == "kwh") return "energy";
  return "";
}
static lv_obj_t* miraGlyphPart(lv_obj_t* parent, lv_coord_t x, lv_coord_t y,
                              lv_coord_t w, lv_coord_t h, bool outline,
                              lv_coord_t radius, lv_color_t accent) {
  lv_obj_t* item = lv_obj_create(parent);
  lv_obj_set_pos(item, x, y);
  lv_obj_set_size(item, w, h);
  lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(item, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_pad_all(item, 0, 0);
  lv_obj_set_style_radius(item, radius, 0);
  lv_obj_set_style_bg_opa(item, outline ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(item, accent, 0);
  lv_obj_set_style_border_width(item, outline ? 2 : 0, 0);
  if (outline) lv_obj_set_style_border_color(item, accent, 0);
  return item;
}
static lv_obj_t* miraCreateInfoGlyph(lv_obj_t* card, const String& kind,
                                    lv_coord_t side, lv_color_t accent) {
  lv_obj_t* root = lv_obj_create(card);
  lv_obj_set_size(root, side, side);
  lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(root, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(root, LV_OBJ_FLAG_IGNORE_LAYOUT);
  lv_obj_set_style_bg_opa(root, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(root, 0, 0);
  lv_obj_set_style_pad_all(root, 0, 0);
  const int d = side;
  const int stroke = d >= 42 ? 4 : 3;
  if (kind == "temperature") {
    miraGlyphPart(root, d*40/100, d/8, d/5, d*54/100, true, 7, accent);
    miraGlyphPart(root, d*31/100, d*58/100, d*38/100, d*38/100, false,
                  LV_RADIUS_CIRCLE, accent);
    miraGlyphPart(root, d*46/100, d/3, stroke, d*35/100, false, 2, accent);
  } else if (kind == "light") {
    miraGlyphPart(root, d/3, d/3, d/3, d/3, false, LV_RADIUS_CIRCLE, accent);
    miraGlyphPart(root, d/2-stroke/2, 0, stroke, d/5, false, 2, accent);
    miraGlyphPart(root, d/2-stroke/2, d*4/5, stroke, d/5, false, 2, accent);
    miraGlyphPart(root, 0, d/2-stroke/2, d/5, stroke, false, 2, accent);
    miraGlyphPart(root, d*4/5, d/2-stroke/2, d/5, stroke, false, 2, accent);
  } else if (kind == "pressure") {
    miraGlyphPart(root, d/7, d/6, d*5/7, d*5/7, true,
                  LV_RADIUS_CIRCLE, accent);
    miraGlyphPart(root, d*29/100, d*56/100, d*42/100, stroke,
                  false, 2, accent);
    miraGlyphPart(root, d*44/100, d*43/100, d/8, d/8,
                  false, LV_RADIUS_CIRCLE, accent);
  } else if (kind == "humidity") {
    miraGlyphPart(root, d*25/100, d*41/100, d/2, d/2,
                  false, LV_RADIUS_CIRCLE, accent);
    miraGlyphPart(root, d*41/100, d/7, d*18/100, d/2,
                  false, 4, accent);
  } else if (kind == "door") {
    miraGlyphPart(root, d/4, d/10, d/2, d*8/10, true, 2, accent);
    miraGlyphPart(root, d*59/100, d/2, stroke, stroke,
                  false, LV_RADIUS_CIRCLE, accent);
  } else {
    // Energy/plug: scalable plug silhouette.
    miraGlyphPart(root, d/4, d*33/100, d/2, d*4/10, false, 6, accent);
    miraGlyphPart(root, d*36/100, d*14/100, stroke, d/4, false, 2, accent);
    miraGlyphPart(root, d*59/100, d*14/100, stroke, d/4, false, 2, accent);
    miraGlyphPart(root, d/2-stroke/2, d*7/10, stroke, d/5,
                  false, 2, accent);
  }
  return root;
}

#if defined(MIRA_TARGET_WAVESHARE_43C)
LV_FONT_DECLARE(mira_sleep_montserrat_120);
LV_FONT_DECLARE(mira_home_montserrat_96);
#endif

static String roundRangeDisplayValue(int value, const String& unit) {
  String out = String(value);
  if (unit.startsWith("°")) {
    out += "°";
  } else if (unit.length()) {
    out += " ";
    out += unit;
  }
  return out;
}

static void makeBulbIcon(lv_obj_t* parent) {
  lv_obj_t* icon = lv_obj_create(parent);
  // Navigation only: match the visual footprint of Home and Pages.
  lv_obj_set_size(icon, MIRA_DISPLAY_WIDTH >= 800 ? 25 : 20,
                  MIRA_DISPLAY_WIDTH >= 800 ? 28 : 25);
  lv_obj_clear_flag(icon, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(icon, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_bg_opa(icon, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(icon, 0, 0);
  lv_obj_set_style_pad_all(icon, 0, 0);
  lv_obj_center(icon);

  lv_obj_t* head = lv_obj_create(icon);
  lv_obj_set_size(head, MIRA_DISPLAY_WIDTH >= 800 ? 16 : 14,
                  MIRA_DISPLAY_WIDTH >= 800 ? 16 : 14);
  lv_obj_set_pos(head, MIRA_DISPLAY_WIDTH >= 800 ? 5 : 3, 0);
  lv_obj_clear_flag(head, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_radius(head, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(head, lv_color_hex(0xDCEAF0), 0);
  lv_obj_set_style_bg_opa(head, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(head, 0, 0);

  lv_obj_t* neck = lv_obj_create(icon);
  lv_obj_set_size(neck, MIRA_DISPLAY_WIDTH >= 800 ? 10 : 8, 4);
  lv_obj_set_pos(neck, MIRA_DISPLAY_WIDTH >= 800 ? 8 : 6,
                 MIRA_DISPLAY_WIDTH >= 800 ? 17 : 15);
  lv_obj_clear_flag(neck, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_radius(neck, 2, 0);
  lv_obj_set_style_bg_color(neck, lv_color_hex(0xDCEAF0), 0);
  lv_obj_set_style_bg_opa(neck, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(neck, 0, 0);

  lv_obj_t* base = lv_obj_create(icon);
  lv_obj_set_size(base, MIRA_DISPLAY_WIDTH >= 800 ? 9 : 8, 4);
  lv_obj_set_pos(base, MIRA_DISPLAY_WIDTH >= 800 ? 8 : 6,
                 MIRA_DISPLAY_WIDTH >= 800 ? 22 : 20);
  lv_obj_clear_flag(base, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_radius(base, 2, 0);
  lv_obj_set_style_bg_color(base, lv_color_hex(0xDCEAF0), 0);
  lv_obj_set_style_bg_opa(base, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(base, 0, 0);
}

static void makeThermometerIcon(lv_obj_t* parent) {
  lv_obj_t* icon = lv_obj_create(parent);
  lv_obj_set_size(icon, MIRA_DISPLAY_WIDTH >= 800 ? 23 : 19,
                  MIRA_DISPLAY_WIDTH >= 800 ? 28 : 25);
  lv_obj_clear_flag(icon, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(icon, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_bg_opa(icon, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(icon, 0, 0);
  lv_obj_set_style_pad_all(icon, 0, 0);
  lv_obj_center(icon);

  lv_obj_t* stem = lv_obj_create(icon);
  lv_obj_set_size(stem, MIRA_DISPLAY_WIDTH >= 800 ? 8 : 7,
                  MIRA_DISPLAY_WIDTH >= 800 ? 19 : 17);
  lv_obj_set_pos(stem, MIRA_DISPLAY_WIDTH >= 800 ? 8 : 6, 0);
  lv_obj_clear_flag(stem, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_radius(stem, 4, 0);
  lv_obj_set_style_bg_opa(stem, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_color(stem, lv_color_hex(0xDCEAF0), 0);
  lv_obj_set_style_border_width(stem, 2, 0);

  lv_obj_t* bulb = lv_obj_create(icon);
  lv_obj_set_size(bulb, MIRA_DISPLAY_WIDTH >= 800 ? 14 : 12,
                  MIRA_DISPLAY_WIDTH >= 800 ? 14 : 12);
  lv_obj_set_pos(bulb, MIRA_DISPLAY_WIDTH >= 800 ? 5 : 4,
                 MIRA_DISPLAY_WIDTH >= 800 ? 15 : 13);
  lv_obj_clear_flag(bulb, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_radius(bulb, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(bulb, lv_color_hex(0xDCEAF0), 0);
  lv_obj_set_style_bg_opa(bulb, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(bulb, 0, 0);
}

MiraPanelUI::MiraPanelUI(MiraPanelServer& server, MiraDisplay& display)
  : _server(server), _display(display) {}

int MiraPanelUI::clampInt(int value, int minValue, int maxValue) {
  if (value < minValue) return minValue;
  if (value > maxValue) return maxValue;
  return value;
}

lv_color_t MiraPanelUI::colorBg() const {
  if (_theme == THEME_MIRA) return lv_color_hex(0x071421);
  if (_theme == THEME_GLASS) return lv_color_hex(0x071A28);
  return lv_color_hex(0x090D12);
}

lv_color_t MiraPanelUI::colorPanel() const {
  if (_theme == THEME_MIRA) return lv_color_hex(0x0E2032);
  if (_theme == THEME_GLASS) return lv_color_hex(0x103047);
  return lv_color_hex(0x141B22);
}

lv_color_t MiraPanelUI::colorPanel2() const {
  if (_theme == THEME_MIRA) return lv_color_hex(0x173149);
  if (_theme == THEME_GLASS) return lv_color_hex(0x17445D);
  return lv_color_hex(0x202A33);
}

lv_color_t MiraPanelUI::colorBorder() const {
  if (_theme == THEME_MIRA) return lv_color_hex(0x17354B);
  if (_theme == THEME_GLASS) return lv_color_hex(0x2B6882);
  return lv_color_hex(0x33414C);
}

lv_color_t MiraPanelUI::colorAccent() const {
  if (_theme == THEME_GLASS) return lv_color_hex(0x5BDEFF);
  if (_theme == THEME_MIRA) return lv_color_hex(0x2ABEFF);
  return lv_color_hex(0x31BDF2);
}

lv_color_t MiraPanelUI::colorAccent2() const {
  if (_theme == THEME_MIRA) return lv_color_hex(0x0A5C88);
  if (_theme == THEME_GLASS) return lv_color_hex(0x176885);
  return lv_color_hex(0x155A75);
}

lv_color_t MiraPanelUI::colorNav() const {
  if (_theme == THEME_MIRA) return lv_color_hex(0x06101B);
  if (_theme == THEME_GLASS) return lv_color_hex(0x06131F);
  return lv_color_hex(0x070B0F);
}

void MiraPanelUI::styleMainCard(lv_obj_t* card, bool dateTime) {
  if (!card) return;

  lv_obj_set_style_bg_color(card, colorPanel(), 0);
  lv_obj_set_style_bg_grad_color(card, colorPanel(), 0);
  lv_obj_set_style_bg_grad_dir(card, LV_GRAD_DIR_NONE, 0);
  lv_obj_set_style_bg_opa(card, _theme == THEME_GLASS ? LV_OPA_80 : LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(card, colorBorder(), 0);
  lv_obj_set_style_border_width(card, 1, 0);
  lv_obj_set_style_border_opa(card, _theme == THEME_MIRA ? LV_OPA_50 : LV_OPA_70, 0);
  lv_obj_set_style_radius(card, _theme == THEME_GLASS ? 16 : 14, 0);

  lv_obj_set_style_shadow_color(card, lv_color_hex(0x000000), 0);
  lv_obj_set_style_shadow_opa(card, _theme == THEME_MIRA ? LV_OPA_10 : LV_OPA_10, 0);
  lv_obj_set_style_shadow_width(card, _theme == THEME_MIRA ? 4 : 3, 0);
  lv_obj_set_style_shadow_ofs_y(card, 2, 0);

  if (dateTime) {
    lv_obj_set_style_border_color(card, colorAccent2(), 0);
    lv_obj_set_style_border_opa(card, _theme == THEME_MIRA ? LV_OPA_70 : LV_OPA_COVER, 0);
    if (_theme == THEME_MIRA) {
      lv_obj_set_style_bg_color(card, lv_color_hex(0x10283D), 0);
      lv_obj_set_style_bg_grad_color(card, lv_color_hex(0x0C1F31), 0);
      lv_obj_set_style_bg_grad_dir(card, LV_GRAD_DIR_VER, 0);
    }
  }
}
void MiraPanelUI::displayFlush(lv_disp_drv_t* disp, const lv_area_t* area, lv_color_t* color_p) {
  if (!_displayForLvgl) {
    lv_disp_flush_ready(disp);
    return;
  }

#if MIRA_RGB_DIRECT_FRAMEBUFFER
  // Waveshare direct-mode behavior: only the last invalid area switches the
  // panel framebuffer. color_p already belongs to one of the two panel FBs.
  if (lv_disp_flush_is_last(disp)) {
    _displayForLvgl->miraFlush(
      area->x1,
      area->y1,
      area->x2 - area->x1 + 1,
      area->y2 - area->y1 + 1,
      (const uint16_t*)&color_p->full
    );
  }
#else
  _displayForLvgl->miraFlush(
    area->x1,
    area->y1,
    area->x2 - area->x1 + 1,
    area->y2 - area->y1 + 1,
    (const uint16_t*)&color_p->full
  );
#endif

  lv_disp_flush_ready(disp);
}

void MiraPanelUI::touchRead(lv_indev_drv_t* indev, lv_indev_data_t* data) {
  (void)indev;
  uint16_t x = 0, y = 0;
  data->state = LV_INDEV_STATE_REL;

  if (_displayForLvgl && _displayForLvgl->miraGetTouch(&x, &y)) {
    bool wasSleeping = false;
    if (_uiForLvgl) {
      wasSleeping = _uiForLvgl->_sleeping;
      _uiForLvgl->noteActivity();
    }

    // Le premier toucher sert uniquement a reveiller l'ecran.
    if (wasSleeping) return;

    // La barre basse est reservee a la navigation Mira. On la traite
    // directement afin qu'aucun widget/page scrollable ne puisse intercepter
    // le toucher. Un seul changement est genere par appui.
    if (_uiForLvgl && y >= MIRA_CONTENT_HEIGHT) {
      // Keep navigation usable at all times, including after opening
      // or closing a thermostat dropdown. The popup opens UPWARDS and
      // stays in the content region. Its LVGL object may outlive closure,
      // so lv_dropdown_get_list() cannot be used as an open-state check.
      if (!_uiForLvgl->_navTouchLatched) {
        _uiForLvgl->_navTouchLatched = true;
        // Direct hardware touch routing for the three fixed favorite slots
        // and the always-present Pages button. Do not let a dropdown or
        // scrollable widget intercept the bottom strip.
        uint8_t slot = ((uint32_t)x * NAV_BUTTON_COUNT) / MIRA_DISPLAY_WIDTH;
        if (slot >= NAV_BUTTON_COUNT) slot = NAV_BUTTON_COUNT - 1;
        if (slot == FAVORITE_SLOTS) {
          _uiForLvgl->_pendingTogglePagePicker = true;
        } else if (_uiForLvgl->_favoritePages[slot] < PAGE_COUNT) {
          _uiForLvgl->_pendingNavPage =
              (int8_t)_uiForLvgl->_favoritePages[slot];
        }
      }
      return;
    }

    if (_uiForLvgl) _uiForLvgl->_navTouchLatched = false;
    data->state = LV_INDEV_STATE_PR;
    data->point.x = x;
    data->point.y = y;
    return;
  }

  if (_uiForLvgl) _uiForLvgl->_navTouchLatched = false;
}


void MiraPanelUI::setMqtt(MiraMqtt* mqtt) {
  _mqtt = mqtt;
}

String MiraPanelUI::normalizeMqttTopicName(const String& value) {
  String out = value;
  out.trim();
  out.replace(" ", "_");
  out.replace("/", "_");
  out.replace("#", "_");
  out.replace("+", "_");
  out.toLowerCase();
  return out;
}

String MiraPanelUI::mqttPayloadValue(const String& payload) {
  String value = payload;
  value.trim();

  // Le firmware accepte egalement un petit JSON {"value":...} afin de
  // faciliter les tests avec jMQTT/Node-RED, tout en gardant la valeur brute
  // comme format principal compatible avec l'esprit du firmware original.
  if (value.startsWith("{")) {
    DynamicJsonDocument doc(512);
    if (deserializeJson(doc, value) == DeserializationError::Ok) {
      if (doc.containsKey("value")) {
        if (doc["value"].is<const char*>()) return String(doc["value"].as<const char*>());
        String out;
        serializeJson(doc["value"], out);
        if (out.startsWith("\"") && out.endsWith("\"") && out.length() >= 2) {
          out = out.substring(1, out.length() - 1);
        }
        return out;
      }
    }
  }
  return value;
}

int MiraPanelUI::findBindingByMqttTopic(const String& topic) const {
  String wanted = topic;
  wanted.trim();
  String wantedNorm = normalizeMqttTopicName(wanted);

  for (uint8_t i = 0; i < _bindingCount; ++i) {
    const Binding& b = _bindings[i];
    if (b.name == wanted) return i;
    if (normalizeMqttTopicName(b.name) == wantedNorm) return i;
  }
  return -1;
}

void MiraPanelUI::publishMqttAction(const Binding& b, const String& payload) {
  if (!_mqtt || !_mqtt->enabled()) return;
  _mqtt->publishAction(b.name, payload);
}

void MiraPanelUI::handleMqttState(const String& topic, const String& rawPayload) {
  // Thermostat exposes 3 independent state topics. Always display-only.
  // Example: mira/<screen>/States/Thermostat salon__temperature
  for (uint8_t t = 0; t < _bindingCount; ++t) {
    Binding& th = _bindings[t];
    if (th.type != W_THERMOSTAT) continue;
    const String temp = th.name + "__temperature";
    const String setpoint = th.name + "__consigne";
    const String mode = th.name + "__mode";
    const String key = normalizeMqttTopicName(topic);
    if (key != normalizeMqttTopicName(temp) &&
        key != normalizeMqttTopicName(setpoint) &&
        key != normalizeMqttTopicName(mode)) continue;
    String value = mqttPayloadValue(rawPayload);
    value.trim();
    _suppressEvents = true;
    if (key == normalizeMqttTopicName(temp)) {
      _server.IndexSetInfo(th.thermostatTempId, value);
      String displayed = value.length() ? value : "--";
      if (displayed.indexOf("°") < 0) displayed += " °C";
      if (th.thermostatTemperatureLabel)
        lv_label_set_text(th.thermostatTemperatureLabel, displayed.c_str());
    } else if (key == normalizeMqttTopicName(setpoint)) {
      if (value.length()) {
        float n = roundf(value.toFloat() * 2.0f) * 0.5f;
        if (n < th.minValue) n = th.minValue;
        if (n > th.maxValue) n = th.maxValue;
        th.thermostatSetpoint = n;
        _server.IndexSetRange(th.id, n);
        if (th.control) lv_arc_set_value(th.control, (int)roundf(n * 2.0f));
        if (th.valueLabel)
          lv_label_set_text(th.valueLabel, (String(n, 1) + "°").c_str());
      }
    } else {
      for (uint8_t i = 0; i < th.listCount; ++i) {
        if (th.listLabels[i].equalsIgnoreCase(value) || value == String(i)) {
          th.selectedIndex = i;
          _server.IndexSetList(th.listIds[i]);
          if (th.thermostatModeControl) lv_dropdown_set_selected(th.thermostatModeControl, i);
          if (th.thermostatModeBadge) lv_label_set_text(th.thermostatModeBadge, th.listLabels[i].c_str());
          break;
        }
      }
    }
    _suppressEvents = false;
    return;
  }
  int idx = findBindingByMqttTopic(topic);
  if (idx < 0 || idx >= _bindingCount) {
    Serial.println("[MIRA][MQTT] State ignore, widget inconnu : " + topic);
    return;
  }

  Binding& b = _bindings[(uint8_t)idx];
  String payload = mqttPayloadValue(rawPayload);
  payload.trim();

  // IMPORTANT : un State MQTT ne declenche jamais IndexSetEvent().
  // On met seulement a jour l'etat interne + l'affichage, afin d'eviter
  // toute boucle ou action domotique au redemarrage de l'ecran.
  _suppressEvents = true;

  if (b.type == W_INFO) {
    _server.IndexSetInfo(b.id, payload);
    String txt = payload + (b.unit.length() ? " " + b.unit : "");
    if (b.valueLabel) lv_label_set_text(b.valueLabel, txt.c_str());
    miraMarkInfoArrival(b.infoLastSeenLabel);
    if (b.infoIconRoot) miraFitInfoValue(b.valueLabel, b.infoIconRoot);
  }
  else if (b.type == W_TOGGLE) {
    String lower = payload;
    lower.toLowerCase();
    bool on = (lower == "1" || lower == "on" || lower == "true" || lower == "oui" || lower == "actif");
    int stateId = on ? b.idOn : b.idOff;
    _server.IndexSetToggle(stateId);
    if (b.control) {
      if (on) lv_obj_add_state(b.control, LV_STATE_CHECKED);
      else lv_obj_clear_state(b.control, LV_STATE_CHECKED);
    }
    if (b.stateLabel) {
      lv_label_set_text(b.stateLabel, on ? "ON" : "OFF");
      lv_obj_set_style_text_color(b.stateLabel, on ? COL_GREEN : COL_MUTED, 0);
    }
  }
  else if (b.type == W_RANGE || b.type == W_ROUNDRANGE) {
    int value = clampInt(payload.toInt(), b.minValue, b.maxValue);
    b.currentValue = value;
    _server.IndexSetRange(b.id, value);
    if (b.type == W_RANGE && b.control) lv_slider_set_value(b.control, value, LV_ANIM_OFF);
    if (b.type == W_ROUNDRANGE && b.control) lv_arc_set_value(b.control, value);
    if (b.valueLabel) {
      if (b.type == W_ROUNDRANGE) {
        String txt = roundRangeDisplayValue(value, b.unit);
        lv_label_set_text(b.valueLabel, txt.c_str());
      } else {
        lv_label_set_text_fmt(b.valueLabel, "%d%s%s", value, b.unit.length() ? " " : "", b.unit.c_str());
      }
    }
  }
  else if (b.type == W_LIST || b.type == W_LISTBUTTON) {
    int selected = -1;
    bool numeric = payload.length() > 0;
    for (size_t i = 0; i < payload.length(); ++i) {
      if (!isDigit(payload[i]) && !(i == 0 && payload[i] == '-')) { numeric = false; break; }
    }
    if (numeric) {
      int n = payload.toInt();
      if (n >= 0 && n < b.listCount) selected = n;
    }
    if (selected < 0) {
      for (uint8_t i = 0; i < b.listCount; ++i) {
        if (b.listLabels[i].equalsIgnoreCase(payload)) { selected = i; break; }
      }
    }
    if (selected >= 0) {
      b.selectedIndex = (int8_t)selected;
      _server.IndexSetList(b.listIds[selected]);
      updateListVisual(b);
    }
  }
  else if (b.type == W_COLOR) {
    int r = 0, g = 0, bl = 0;
    bool ok = false;
    String c = payload;
    c.trim();
    if (c.startsWith("#") && c.length() == 7) {
      long rgb = strtol(c.substring(1).c_str(), nullptr, 16);
      r = (rgb >> 16) & 0xFF;
      g = (rgb >> 8) & 0xFF;
      bl = rgb & 0xFF;
      ok = true;
    } else {
      int p1 = c.indexOf(',');
      int p2 = c.indexOf(',', p1 + 1);
      if (p1 > 0 && p2 > p1) {
        r = clampInt(c.substring(0, p1).toInt(), 0, 255);
        g = clampInt(c.substring(p1 + 1, p2).toInt(), 0, 255);
        bl = clampInt(c.substring(p2 + 1).toInt(), 0, 255);
        ok = true;
      }
    }
    if (ok) {
      _server.IndexSetColor(b.id, r, g, bl);
      if (b.control) lv_colorwheel_set_rgb(b.control, lv_color_make(r, g, bl));
    }
  }

  _suppressEvents = false;
  noteActivity();
}

void MiraPanelUI::begin() {
#if defined(MIRA_TARGET_WAVESHARE_43C)
  Serial.printf("[MIRA][WAVESHARE] LVGL officiel %d.%d.%d\n",
                LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);
#endif

  _displayForLvgl = &_display;
  _uiForLvgl = this;

  _prefs.begin("miraui", false);
  _brightness = _prefs.getUChar("bright", 220);
  uint8_t savedTheme = _prefs.getUChar("theme", 0xFF);
  if (savedTheme == 0xFF) savedTheme = _prefs.getBool("dark", false) ? THEME_DARK : THEME_MIRA;
  _theme = savedTheme <= THEME_GLASS ? savedTheme : THEME_DARK;
  _returnHomeEnabled = _prefs.getBool("home", true);
  loadFavorites();
  _sleepEnabled = _prefs.getBool("sleep", true);
  _clockInSleep = _prefs.getBool("clock", true);

  if (_brightness < 30) _brightness = 30;

  _display.begin();
  refreshDisplayConfig(true);
  updateDisplaySchedule(true);
  applyAwakeBrightness();
  _display.setRotation(0);

#if defined(MIRA_TARGET_WAVESHARE_43C)
  // Important: unlike 0.6.25..0.6.33, Mira does not create or register the
  // Waveshare display driver here. The official Waveshare port does all of it.
  if (!_display.miraLvglBegin()) {
    Serial.println("[MIRA][WAVESHARE] ERREUR demarrage port LVGL officiel");
    return;
  }
  if (!_display.miraLvglLock(-1)) {
    Serial.println("[MIRA][WAVESHARE] ERREUR verrou LVGL initial");
    return;
  }
#else
  lv_init();
  lv_disp_draw_buf_init(&_drawBuffer, _buf1, _buf2, MIRA_DISPLAY_WIDTH * MIRA_DRAW_BUFFER_LINES);

  static lv_disp_drv_t dispDrv;
  lv_disp_drv_init(&dispDrv);
  dispDrv.hor_res = MIRA_DISPLAY_WIDTH;
  dispDrv.ver_res = MIRA_DISPLAY_HEIGHT;
  dispDrv.flush_cb = displayFlush;
  dispDrv.draw_buf = &_drawBuffer;
  lv_disp_drv_register(&dispDrv);
#endif

  static lv_indev_drv_t indevDrv;
  lv_indev_drv_init(&indevDrv);
  indevDrv.type = LV_INDEV_TYPE_POINTER;
  indevDrv.read_cb = touchRead;
  lv_indev_drv_register(&indevDrv);

  _screen = lv_obj_create(nullptr);
  lv_obj_clear_flag(_screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(_screen, 0, 0);
  lv_obj_set_style_pad_all(_screen, 0, 0);
  lv_scr_load(_screen);

  for (uint8_t i = 0; i < PAGE_COUNT; ++i) {
    _pages[i] = lv_obj_create(_screen);
    lv_obj_set_pos(_pages[i], 0, 0);
    lv_obj_set_size(_pages[i], MIRA_DISPLAY_WIDTH, MIRA_CONTENT_HEIGHT);
    lv_obj_set_style_bg_opa(_pages[i], LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(_pages[i], 0, 0);
    lv_obj_set_style_pad_all(_pages[i], 6, 0);
    lv_obj_set_style_pad_row(_pages[i], 6, 0);
    lv_obj_set_style_pad_column(_pages[i], 6, 0);
    if (i == 0) {
      lv_obj_set_style_pad_top(_pages[i], 8, 0);
      lv_obj_set_flex_flow(_pages[i], LV_FLEX_FLOW_ROW_WRAP);
      lv_obj_set_flex_align(_pages[i], LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    } else {
      if (i == 1 || i == 2) lv_obj_set_style_pad_top(_pages[i], 8, 0);
      if (MIRA_DISPLAY_WIDTH >= 800 && (i == 1 || i == 2)) {
        lv_obj_set_flex_flow(_pages[i], LV_FLEX_FLOW_ROW_WRAP);
        lv_obj_set_flex_align(_pages[i], LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
      } else {
        lv_obj_set_flex_flow(_pages[i], LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(_pages[i], LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
      }
    }
    lv_obj_set_scroll_dir(_pages[i], LV_DIR_VER);
    lv_obj_set_scrollbar_mode(_pages[i], LV_SCROLLBAR_MODE_OFF);
    if (i != 0) lv_obj_add_flag(_pages[i], LV_OBJ_FLAG_HIDDEN);
  }

  // Experimental navigation: original three contextual destinations plus
  // a fourth, always visible "Pages" entry. Existing page content is untouched.
  _nav = lv_obj_create(_screen);
  lv_obj_set_pos(_nav, 0, MIRA_CONTENT_HEIGHT);
  lv_obj_set_size(_nav, MIRA_DISPLAY_WIDTH, MIRA_NAV_HEIGHT);
  lv_obj_clear_flag(_nav, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(_nav, 1, 0);
  lv_obj_set_style_border_side(_nav, LV_BORDER_SIDE_TOP, 0);
  lv_obj_set_style_border_color(_nav, colorBorder(), 0);
  lv_obj_set_style_radius(_nav, 0, 0);
  lv_obj_set_style_pad_all(_nav, 0, 0);

  // Three persistent favorites (empty slots allowed) plus the Pages menu.
  // The slots remain in the same position on every page.
  for (uint8_t slot = 0; slot < FAVORITE_SLOTS; ++slot) {
    lv_obj_t* btn = lv_btn_create(_nav);
    _favoriteButtons[slot] = btn;
    lv_obj_set_size(btn, MIRA_DISPLAY_WIDTH / NAV_BUTTON_COUNT,
                    MIRA_NAV_HEIGHT - 2);
    lv_obj_set_pos(btn, 1 + slot * (MIRA_DISPLAY_WIDTH / NAV_BUTTON_COUNT), 1);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(btn, 2);
    lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_30, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(btn, colorAccent(), LV_STATE_PRESSED);
    lv_obj_set_style_radius(btn, 10, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_pad_all(btn, 0, 0);
    lv_obj_add_event_cb(btn, onNav, LV_EVENT_CLICKED, (void*)(uintptr_t)slot);
  }

  // The fourth nav SLOT is a separate button, not the Settings page icon.
  _navPagesButton = lv_btn_create(_nav);
  lv_obj_set_size(_navPagesButton, MIRA_DISPLAY_WIDTH / NAV_BUTTON_COUNT,
                  MIRA_NAV_HEIGHT - 2);
  lv_obj_set_style_bg_opa(_navPagesButton, LV_OPA_TRANSP, 0);
  lv_obj_set_style_bg_opa(_navPagesButton, LV_OPA_30, LV_STATE_PRESSED);
  lv_obj_set_style_bg_color(_navPagesButton, colorAccent(), LV_STATE_PRESSED);
  lv_obj_set_style_radius(_navPagesButton, 10, 0);
  lv_obj_set_style_border_width(_navPagesButton, 0, 0);
  lv_obj_set_style_shadow_width(_navPagesButton, 0, 0);
  lv_obj_set_style_pad_all(_navPagesButton, 0, 0);
  // Draw a clear 2x2 grid; no font icon dependencies on WT32.
  for (uint8_t dot = 0; dot < 4; ++dot) {
    lv_obj_t* square = lv_obj_create(_navPagesButton);
    const bool wideIcon = MIRA_DISPLAY_WIDTH >= 800;
    lv_obj_set_size(square, wideIcon ? 9 : 7, wideIcon ? 9 : 7);
    lv_obj_set_style_radius(square, 2, 0);
    lv_obj_set_style_pad_all(square, 0, 0);
    lv_obj_set_style_border_width(square, 0, 0);
    lv_obj_set_style_bg_color(square, colorAccent(), 0);
    lv_obj_set_style_bg_opa(square, LV_OPA_COVER, 0);
    lv_obj_clear_flag(square, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(square, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(square, LV_ALIGN_CENTER,
                 (dot % 2) * (wideIcon ? 14 : 11) - (wideIcon ? 7 : 5),
                 (dot / 2) * (wideIcon ? 14 : 11) - (wideIcon ? 7 : 5));
  }


  buildSettingsPage();
  buildEnergyPreviewPage();
  buildPagePicker();
  refreshFavoriteNav();

  // Run touch navigation on LVGL's task, without waiting for Arduino's loop.
  lv_timer_create(onNavigationTimer, 10, this);

  _sleepOverlay = lv_obj_create(_screen);
  lv_obj_set_pos(_sleepOverlay, 0, 0);
  lv_obj_set_size(_sleepOverlay, MIRA_DISPLAY_WIDTH, MIRA_DISPLAY_HEIGHT);
  lv_obj_clear_flag(_sleepOverlay, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(_sleepOverlay, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(_sleepOverlay, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(_sleepOverlay, 0, 0);
  lv_obj_set_style_radius(_sleepOverlay, 0, 0);

  _sleepClock = lv_label_create(_sleepOverlay);
  lv_label_set_text(_sleepClock, "--:--");
  lv_obj_set_style_text_color(_sleepClock, COL_MUTED, 0);
#if defined(MIRA_TARGET_WAVESHARE_43C)
  lv_obj_set_style_text_font(_sleepClock, &mira_sleep_montserrat_120, 0);
#else
  // WT32: unchanged original Montserrat 48, no stretching.
  lv_obj_set_style_text_font(_sleepClock, &lv_font_montserrat_48, 0);
#endif
  lv_obj_set_style_text_align(_sleepClock, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(_sleepClock, LV_ALIGN_CENTER, 0, -48);

  lv_obj_add_flag(_sleepOverlay, LV_OBJ_FLAG_HIDDEN);

  _lastActivity = millis();
  applyTheme();
  setNavActive(0);

#if defined(MIRA_TARGET_WAVESHARE_43C)
  // Do NOT call lv_refr_now() from the Arduino task here.
  // Waveshare's official RGB flush waits for a VSYNC notification addressed to
  // the dedicated LVGL task. Refreshing from this task would therefore deadlock
  // after the first visible frame. Unlock and let the official LVGL task render.
  lv_obj_invalidate(_screen);
  _display.miraLvglUnlock();
#endif
}

MiraPanelUI::EventContext* MiraPanelUI::makeContext(uint8_t bindingIndex, int8_t itemIndex) {
  if (_eventCtxCount >= MAX_EVENT_CONTEXTS) return nullptr;
  EventContext& c = _eventCtx[_eventCtxCount++];
  c.ui = this;
  c.bindingIndex = bindingIndex;
  c.itemIndex = itemIndex;
  return &c;
}

uint8_t MiraPanelUI::addBinding() {
  if (_bindingCount >= MAX_BINDINGS) return 0xFF;
  uint8_t idx = _bindingCount++;
  _bindings[idx] = Binding();
  return idx;
}

uint8_t MiraPanelUI::pageIndex(const String& location) const {
  if (location == "lumieres") return 1;
  if (location == "temperatures") return 2;
  return 0;
}

lv_obj_t* MiraPanelUI::makeCard(uint8_t page, lv_coord_t height) {
  // Dans le firmware original, les pages principales sont très plates :
  // pas de cartes visibles, juste les contrôles sur le fond de page.
  lv_obj_t* card = lv_obj_create(_pages[page]);
  lv_obj_set_width(card, MIRA_CARD_AUTO);
  lv_obj_set_height(card, height == LV_SIZE_CONTENT ? LV_SIZE_CONTENT : height);
  styleMainCard(card, false);
  lv_obj_set_style_pad_all(card, 9, 0);
  lv_obj_set_style_pad_row(card, 7, 0);
  return card;
}

void MiraPanelUI::applyFreeLayout(uint8_t idx) {
  if (idx >= _bindingCount) return;
  Binding& b = _bindings[idx];
  if (b.layout != "free" || !b.card) return;
  if (b.layoutX < 0 || b.layoutY < 0 || b.layoutW <= 0 || b.layoutH <= 0) return;

  const int32_t logicalW = MIRA_LAYOUT_LOGICAL_WIDTH;
  const int32_t logicalH = MIRA_LAYOUT_LOGICAL_HEIGHT;
  int32_t x = ((int32_t)b.layoutX * MIRA_DISPLAY_WIDTH) / logicalW;
  int32_t y = ((int32_t)b.layoutY * MIRA_CONTENT_HEIGHT) / logicalH;
  int32_t w = ((int32_t)b.layoutW * MIRA_DISPLAY_WIDTH) / logicalW;
  int32_t h = ((int32_t)b.layoutH * MIRA_CONTENT_HEIGHT) / logicalH;

  const int32_t minW = 40;
  const int32_t minH = 32;
  if (w < minW) w = minW;
  if (h < minH) h = minH;
  if (w > MIRA_DISPLAY_WIDTH) w = MIRA_DISPLAY_WIDTH;
  if (x < 0) x = 0;
  if (x + w > MIRA_DISPLAY_WIDTH) x = MIRA_DISPLAY_WIDTH - w;
  if (y < 0) y = 0;

  lv_obj_add_flag(b.card, LV_OBJ_FLAG_IGNORE_LAYOUT);
  lv_obj_set_pos(b.card, (lv_coord_t)x, (lv_coord_t)y);
  lv_obj_set_size(b.card, (lv_coord_t)w, (lv_coord_t)h);
  lv_obj_clear_flag(b.card, LV_OBJ_FLAG_SCROLLABLE);
}

lv_obj_t* MiraPanelUI::makeSettingsCard(lv_coord_t height) {
  lv_obj_t* card = lv_obj_create(_pages[3]);
  lv_obj_set_width(card, MIRA_DISPLAY_WIDTH >= 800 ? 640 : 304);
  lv_obj_set_height(card, height);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(card, colorPanel(), 0);
  lv_obj_set_style_border_color(card, colorBorder(), 0);
  lv_obj_set_style_border_width(card, 1, 0);
  lv_obj_set_style_radius(card, 11, 0);
  lv_obj_set_style_pad_all(card, 8, 0);
  lv_obj_set_style_pad_row(card, 4, 0);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
  return card;
}

lv_obj_t* MiraPanelUI::makeTransparentRow(lv_obj_t* parent, lv_coord_t height) {
  lv_obj_t* row = lv_obj_create(parent);
  lv_obj_set_width(row, MIRA_DISPLAY_WIDTH >= 800 ? lv_pct(100) : 284);
  lv_obj_set_height(row, height);
  lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(row, 0, 0);
  lv_obj_set_style_pad_all(row, 0, 0);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  return row;
}

void MiraPanelUI::makeNameLabel(lv_obj_t* parent, const String& text) {
  lv_obj_t* name = lv_label_create(parent);
  lv_label_set_text(name, text.c_str());
  lv_obj_set_style_text_color(name, COL_TEXT, 0);
  lv_obj_set_style_text_font(name, &mira_font_fr_14, 0);
}

void MiraPanelUI::clearDynamicPages() {
  for (uint8_t i = 0; i < 3; ++i) lv_obj_clean(_pages[i]);
  _bindingCount = 0;
  _homeStandardInfoTotal = 0;
  _homeStandardInfoCreated = 0;
  _eventCtxCount = 0;
  _dateTimeLabel = nullptr;
  _dateSubLabel = nullptr;
  _clockStatusLabel = nullptr;
  _clockStatusCache = "";
}

void MiraPanelUI::reloadFromMiraJson() {
  Serial.println("[MIRA][UI] Nouvelle programmation recue, reconstruction a chaud");
  _server.ResetActionsForLvgl();
  clearDynamicPages();
  buildFromMiraJson();
  applyTheme();
  showPage(0);
  Serial.println("[MIRA][UI] Interface reconstruite sans redemarrage");
}

void MiraPanelUI::buildFromMiraJson() {
  String json = _server.GetJsonLvgl();
  DynamicJsonDocument doc(16384);
  DeserializationError err = deserializeJson(doc, json);

  if (err || !doc["lvgl"].is<JsonArray>()) {
    lv_obj_t* msg = lv_label_create(_pages[0]);
    lv_label_set_text(msg, "Configuration Mira illisible");
    lv_obj_set_style_text_color(msg, COL_RED, 0);
    return;
  }

  JsonArray arr = doc["lvgl"].as<JsonArray>();
  if (arr.size() == 0) {
    lv_obj_t* card = makeCard(0, 120);
    lv_obj_t* msg = lv_label_create(card);
    if (_server.IsSetupMode()) {
      lv_label_set_text(msg, "Configuration initiale\n\nConnectez-vous au Wi-Fi\nMira Panel");
    } else {
      lv_label_set_text(msg, "Aucune interface programmée.\n\nDans Jeedom > Mira Panel :\nProgrammer un écran ESP32.");
    }
    lv_obj_set_style_text_color(msg, COL_TEXT, 0);
    lv_obj_set_style_text_align(msg, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(msg, &mira_font_fr_14, 0);
    lv_obj_center(msg);
    return;
  }

  _homeStandardInfoTotal = 0;
  _homeStandardInfoCreated = 0;
  for (JsonObject obj : arr) {
    String type = obj["type"] | "";
    String location = obj["emplacement"] | "accueil";
    String style = obj["style"] | "standard";
    String layout = obj["layout"] | "auto";
    type.trim();
    type.toLowerCase();
    style.trim();
    style.toLowerCase();
    if (!style.length()) style = "standard";
    layout.trim();
    layout.toLowerCase();
    if (layout != "tile" && layout != "wide" && layout != "hero" && layout != "free") layout = "auto";
    if (type == "info" && pageIndex(location) == 0 && style == "standard" && layout == "auto") {
      ++_homeStandardInfoTotal;
    }
  }

  for (JsonObject obj : arr) registerAndCreate(obj);
}

void MiraPanelUI::registerAndCreate(JsonObject obj) {
  if (_bindingCount >= MAX_BINDINGS) return;

  String type = obj["type"] | "";
  String name = obj["name"] | "";
  String location = obj["emplacement"] | "accueil";
  String unit = obj["unit"] | "";
  String sMin = obj["min"] | "";
  String sMax = obj["max"] | "";
  String sValue = obj["value"] | "";
  String widgetStyle = obj["style"] | "standard";
  widgetStyle.trim();
  widgetStyle.toLowerCase();
  if (!widgetStyle.length()) widgetStyle = "standard";
  String widgetLabelMode = obj["labelMode"] | "auto";
  widgetLabelMode.toLowerCase();
  if (widgetLabelMode != "text" && widgetLabelMode != "icon" &&
      widgetLabelMode != "both") widgetLabelMode = "auto";
  String widgetIcon = obj["icon"] | "auto";
  String widgetDisplayName = obj["displayName"] | "";
  widgetIcon.toLowerCase();
  String widgetLayout = obj["layout"] | "auto";
  widgetLayout.trim();
  widgetLayout.toLowerCase();
  if (widgetLayout != "tile" && widgetLayout != "wide" && widgetLayout != "hero" && widgetLayout != "free") widgetLayout = "auto";

  uint8_t idx = addBinding();
  if (idx == 0xFF) return;
  Binding& b = _bindings[idx];
  b.name = name;
  b.location = location;
  b.unit = unit;
  b.style = widgetStyle;
  b.layout = widgetLayout;
  b.labelMode = widgetLabelMode;
  b.icon = widgetIcon;
  b.displayName = widgetDisplayName;
  if (b.layout == "free") {
    b.layoutX = obj["lx"] | -1;
    b.layoutY = obj["ly"] | -1;
    b.layoutW = obj["lw"] | -1;
    b.layoutH = obj["lh"] | -1;
    if (b.layoutX < 0 || b.layoutY < 0 || b.layoutW <= 0 || b.layoutH <= 0) b.layout = "auto";
  }

  if (type == "info") {
    b.type = W_INFO;
    b.id = _server.IndexAddInfo(name, unit);
    createInfo(idx);
  }
  else if (type == "button") {
    b.type = W_BUTTON;
    b.id = _server.IndexAddButton(name);
    createButton(idx);
  }
  else if (type == "toggle") {
    b.type = W_TOGGLE;
    int* ids = _server.IndexAddToggle(name, "Off", "On", "Off");
    b.idOff = ids[0];
    b.idOn = ids[1];
    createToggle(idx);
  }
  else if (type == "range" || type == "roundrange") {
    b.type = (type == "range") ? W_RANGE : W_ROUNDRANGE;
    b.minValue = sMin.length() ? sMin.toInt() : 0;
    b.maxValue = sMax.length() ? sMax.toInt() : 100;
    if (b.maxValue <= b.minValue) b.maxValue = b.minValue + 100;
    b.currentValue = sValue.length() ? sValue.toInt() : b.minValue;
    b.currentValue = clampInt(b.currentValue, b.minValue, b.maxValue);
    b.id = _server.IndexAddRange(name, b.minValue, b.maxValue, b.currentValue);
    if (b.type == W_RANGE) createRange(idx);
    else createRoundRange(idx);
  }
  else if (type == "color") {
    b.type = W_COLOR;
    b.id = _server.IndexAddColor(name, 50, 120, 180);
    createColor(idx);
  }
  else if (type == "list" || type == "listbutton") {
    b.type = (type == "list") ? W_LIST : W_LISTBUTTON;

    String options = sValue;
    int start = 0;
    while (start <= options.length() && b.listCount < MAX_LIST_ITEMS) {
      int sep = options.indexOf('|', start);
      String item = (sep < 0) ? options.substring(start) : options.substring(start, sep);
      item.trim();
      if (item.length()) {
        int id = _server.IndexAddList(name, item, item);
        b.listIds[b.listCount] = id;
        b.listLabels[b.listCount] = item;
        b.listCount++;
      }
      if (sep < 0) break;
      start = sep + 1;
    }
    createList(idx, b.type == W_LISTBUTTON);
  }
  else if (type == "thermostat") {
    b.type = W_THERMOSTAT;
    b.unit = "°C";
    b.minValue = sMin.length() ? sMin.toInt() : 5;
    b.maxValue = sMax.length() ? sMax.toInt() : 30;
    b.minValue = clampInt(b.minValue, -20, 60);
    b.maxValue = clampInt(b.maxValue, b.minValue + 1, 80);
    b.thermostatSetpoint = sValue.length() ? sValue.toFloat() : 20.0f;
    b.thermostatSetpoint = roundf(b.thermostatSetpoint * 2.0f) * 0.5f;
    if (b.thermostatSetpoint < b.minValue) b.thermostatSetpoint = b.minValue;
    if (b.thermostatSetpoint > b.maxValue) b.thermostatSetpoint = b.maxValue;
    // Three native Mira IDs, but only ONE card/widget. The legacy Jeedom
    // /getindex machinery can reuse its info/range/list command mapping.
    b.thermostatTempId = _server.IndexAddInfo(name + "__temperature", "°C");
    b.id = _server.IndexAddRange(name + "__consigne", b.minValue, b.maxValue,
                                 (int)roundf(b.thermostatSetpoint));
    String options = String((const char*)(obj["modes"] | "Confort|Éco|Hors gel|Arrêt"));
    options.replace("\\n", "|");
    int start = 0;
    while (start <= options.length() && b.listCount < MAX_LIST_ITEMS) {
      const int sep = options.indexOf('|', start);
      String item = (sep < 0) ? options.substring(start) : options.substring(start, sep);
      item.trim();
      if (item.length()) {
        b.listIds[b.listCount] = _server.IndexAddList(name + "__mode", item, item);
        b.listLabels[b.listCount++] = item;
      }
      if (sep < 0) break;
      start = sep + 1;
    }
    if (b.listCount == 0) {
      b.listIds[0] = _server.IndexAddList(name + "__mode", "Confort", "Confort");
      b.listLabels[0] = "Confort";
      b.listCount = 1;
    }
    createThermostat(idx);
  }
  else if (type == "datetime") {
    b.type = W_DATETIME;
    createDateTime(idx);
  }
  else {
    b.type = W_UNSUPPORTED;
    createUnsupported(idx, type);
  }

  applyFreeLayout(idx);
  if (b.type == W_INFO) decorateInfo(idx);
}



// The displayed value may change length at any time (MQTT / Jeedom), e.g.
// "999 hPa" -> "1015.3 hPa". Fit the *actual glyph width* into the available
// label, including the unit, instead of assuming a font based on screen width.
// Reflow only widgets which have an icon. For each new value, choose a font
// that fits and place [icon][gap][value+unit] centered in the tile as a unit.
static void miraFitInfoValue(lv_obj_t* label, lv_obj_t* icon) {
  if (!label || !icon) return;
  lv_obj_t* card = lv_obj_get_parent(label);
  if (!card) return;
  const char* value = lv_label_get_text(label);
  if (!value) return;

  // LVGL child coordinates are relative to the parent's CONTENT box, not
  // always the parent width: account for card padding and borders.
  const lv_coord_t innerW = lv_obj_get_content_width(card);
  const lv_coord_t iconW = lv_obj_get_style_width(icon, LV_PART_MAIN);
  const lv_coord_t iconH = lv_obj_get_style_height(icon, LV_PART_MAIN);
  const lv_coord_t gap = (innerW >= 180) ? 10 : 6;
  const lv_coord_t margin = (innerW >= 180) ? 8 : 4;
  const lv_coord_t room = innerW - 2 * margin - iconW - gap - 6;
  if (room < 8) return;

  const lv_font_t* fonts[] = {
    &lv_font_montserrat_28, &lv_font_montserrat_20,
    &mira_font_fr_16, &mira_font_fr_12
  };
  const bool canBeLarge = innerW >= 200 &&
                          lv_obj_get_height(card) > 105;
  const size_t count = sizeof(fonts) / sizeof(fonts[0]);
  const lv_coord_t spacing =
      lv_obj_get_style_text_letter_space(label, LV_PART_MAIN);
  const lv_font_t* selected = fonts[count - 1];
  lv_coord_t textW = 0;
  bool fits = false;
  for (size_t i = canBeLarge ? 0 : 1; i < count; ++i) {
    const lv_coord_t measured =
        lv_txt_get_width(value, strlen(value), fonts[i],
                         spacing, LV_TEXT_FLAG_NONE);
    selected = fonts[i];
    textW = measured;
    if (measured <= room) { fits = true; break; }
  }

  // A long unexpected measurement is still clipped inside the tile.
  const lv_coord_t drawW = fits ? textW : room;
  const lv_coord_t groupW = iconW + gap + drawW;
  const lv_coord_t startX = margin + (innerW - 2 * margin - groupW) / 2;
  const lv_coord_t iconY = lv_obj_get_y_aligned(icon);
  const lv_coord_t textH = lv_font_get_line_height(selected);

  lv_obj_set_align(icon, LV_ALIGN_TOP_LEFT);
  lv_obj_set_pos(icon, startX, iconY);
  // An old TOP_MID alignment was the reason 0.6.45 overflowed.
  lv_obj_set_align(label, LV_ALIGN_TOP_LEFT);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_LEFT, 0);
  lv_obj_set_style_text_font(label, selected, 0);
  lv_label_set_long_mode(label, fits ? LV_LABEL_LONG_CLIP : LV_LABEL_LONG_DOT);
  lv_obj_set_width(label, drawW + 6);
  lv_obj_set_height(label, textH + 4);
  lv_obj_set_pos(label, startX + iconW + gap,
                 iconY + (iconH - textH) / 2);
}
void MiraPanelUI::decorateInfo(uint8_t idx) {
  Binding& b = _bindings[idx];
  if (!b.card || !b.valueLabel) return;
  // Default WT32 remains byte-for-byte equivalent in its visual behavior.
  // On large panels, auto chooses an icon when the sensor can be recognized.
  if (b.labelMode == "text" ||
      (b.labelMode == "auto" && MIRA_DISPLAY_WIDTH <= 320)) return;
  const String kind = miraResolveInfoIcon(b.icon, b.name, b.unit);
  if (kind.isEmpty()) return; // Unknown icon: keep name rather than an empty glyph.

  lv_obj_update_layout(b.card);
  const lv_coord_t w = lv_obj_get_width(b.card);
  lv_coord_t h = lv_obj_get_height(b.card);
  if (w < 90 || h < 35) return;
  const bool iconOnly = b.labelMode == "icon";
  const bool showTitle = !iconOnly;
  if (iconOnly && MIRA_DISPLAY_WIDTH >= 800 && b.layout != "free" &&
      h >= 115) {
    // Compact card height is meaningful, not simply a hidden label.
    h = 100;
    lv_obj_set_height(b.card, h);
  }
  const lv_coord_t pad = w >= 220 ? 16 : 8;
  lv_coord_t side = (lv_coord_t)min((int)(w / 5), (int)(h / (showTitle ? 2 : 1.7f)));
  if (side > 70) side = 70;
  if (side < 18) side = 18;
  const int left = pad;
  int y = (h - side) / 2;
  if (showTitle && h > 90) y += 8;

  b.infoIconRoot = miraCreateInfoGlyph(b.card, kind, side, colorAccent());
  lv_obj_set_pos(b.infoIconRoot, left, y);

  const lv_coord_t valueX = left + side + 10;
  // Previous width + x position could extend 4px BEYOND the card's edge.
  const lv_coord_t valueW = w - valueX - pad - 4;
  lv_obj_add_flag(b.valueLabel, LV_OBJ_FLAG_IGNORE_LAYOUT);
  // Original Info creation aligned this label TOP_MID. set_pos() alone does
  // NOT override that alignment, which caused both clipping and top overflow.
  lv_obj_set_align(b.valueLabel, LV_ALIGN_TOP_LEFT);
  lv_obj_set_style_pad_all(b.valueLabel, 0, 0);
  lv_obj_set_width(b.valueLabel, valueW > 10 ? valueW : 10);
  lv_obj_set_style_text_align(b.valueLabel, LV_TEXT_ALIGN_LEFT, 0);
  lv_obj_set_pos(b.valueLabel, valueX, y);
  miraFitInfoValue(b.valueLabel, b.infoIconRoot);
  if (b.infoNameLabel) {
    if (showTitle) {
      lv_obj_add_flag(b.infoNameLabel, LV_OBJ_FLAG_IGNORE_LAYOUT);
      lv_obj_set_width(b.infoNameLabel, w - 2 * pad);
      lv_obj_set_style_text_align(b.infoNameLabel, LV_TEXT_ALIGN_CENTER, 0);
      lv_obj_set_style_text_font(b.infoNameLabel,
          h > 105 ? &mira_font_fr_16 : &mira_font_fr_12, 0);
      lv_obj_set_pos(b.infoNameLabel, pad, h > 105 ? 10 : 3);
    } else {
      lv_obj_add_flag(b.infoNameLabel, LV_OBJ_FLAG_HIDDEN);
    }
  }
  if (b.infoLastSeenLabel) {
    lv_obj_add_flag(b.infoLastSeenLabel, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_width(b.infoLastSeenLabel, w - pad * 2);
    lv_obj_set_style_text_align(b.infoLastSeenLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(b.infoLastSeenLabel, pad, h - 21);
  }
}

void MiraPanelUI::createInfo(uint8_t idx) {
  Binding& b = _bindings[idx];
  const bool compact = b.style == "compact";
  const bool valueDominant = b.style == "value";
  const uint8_t page = pageIndex(b.location);
  const bool autoDashboardTile = page == 0 && b.style == "standard" && b.layout == "auto";
  const bool forcedTile = page == 0 && b.style == "standard" && b.layout == "tile";
  const bool forcedWide = b.layout == "wide";
  const bool forcedHero = b.layout == "hero";

  if (forcedWide) {
    b.card = makeCard(page, 56);
    lv_obj_set_width(b.card, MIRA_CARD_FULL);
    lv_obj_clear_flag(b.card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_left(b.card, 16, 0);
    lv_obj_set_style_pad_right(b.card, 16, 0);
    lv_obj_set_style_pad_top(b.card, 0, 0);
    lv_obj_set_style_pad_bottom(b.card, 0, 0);
    lv_obj_set_flex_flow(b.card, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(b.card, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    b.infoNameLabel = lv_label_create(b.card);
    lv_obj_t* name = b.infoNameLabel;
    lv_label_set_text(name, (b.displayName.length() ? b.displayName : b.name).c_str());
    lv_obj_set_width(name, MIRA_LABEL_WIDE_NAME);
    lv_label_set_long_mode(name, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_color(name, COL_MUTED, 0);
    lv_obj_set_style_text_font(name, &mira_font_fr_14, 0);

    b.valueLabel = lv_label_create(b.card);
    String txt = String("--") + (b.unit.length() ? " " + b.unit : "");
    lv_label_set_text(b.valueLabel, txt.c_str());
    lv_obj_set_width(b.valueLabel, MIRA_LABEL_WIDE_VALUE);
    lv_label_set_long_mode(b.valueLabel, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(b.valueLabel, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_color(b.valueLabel, colorAccent(), 0);
    lv_obj_set_style_text_font(b.valueLabel, &lv_font_montserrat_20, 0);
    return;
  }

  if (autoDashboardTile) {
    ++_homeStandardInfoCreated;
    const bool lastOddTile = MIRA_DISPLAY_WIDTH <= 320 && (_homeStandardInfoTotal & 1U) &&
                             (_homeStandardInfoCreated == _homeStandardInfoTotal);

    if (lastOddTile) {
      b.card = makeCard(page, 56);
      lv_obj_set_width(b.card, MIRA_CARD_FULL);
        lv_obj_clear_flag(b.card, LV_OBJ_FLAG_SCROLLABLE);
      lv_obj_set_style_pad_left(b.card, 16, 0);
      lv_obj_set_style_pad_right(b.card, 16, 0);
      lv_obj_set_style_pad_top(b.card, 0, 0);
      lv_obj_set_style_pad_bottom(b.card, 0, 0);
      lv_obj_set_flex_flow(b.card, LV_FLEX_FLOW_ROW);
      lv_obj_set_flex_align(b.card, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

      b.infoNameLabel = lv_label_create(b.card);
    lv_obj_t* name = b.infoNameLabel;
      lv_label_set_text(name, (b.displayName.length() ? b.displayName : b.name).c_str());
      lv_obj_set_width(name, MIRA_LABEL_WIDE_NAME);
      lv_label_set_long_mode(name, LV_LABEL_LONG_CLIP);
      lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_LEFT, 0);
      lv_obj_set_style_text_color(name, COL_MUTED, 0);
      lv_obj_set_style_text_font(name, &mira_font_fr_14, 0);

      b.valueLabel = lv_label_create(b.card);
      String txt = String("--") + (b.unit.length() ? " " + b.unit : "");
      lv_label_set_text(b.valueLabel, txt.c_str());
      lv_obj_set_width(b.valueLabel, MIRA_LABEL_WIDE_VALUE);
      lv_label_set_long_mode(b.valueLabel, LV_LABEL_LONG_CLIP);
      lv_obj_set_style_text_align(b.valueLabel, LV_TEXT_ALIGN_RIGHT, 0);
      lv_obj_set_style_text_color(b.valueLabel, colorAccent(), 0);
      lv_obj_set_style_text_font(b.valueLabel, &lv_font_montserrat_20, 0);
      return;
    }
  }

  if (autoDashboardTile || forcedTile) {
    const bool adaptive = MIRA_DISPLAY_WIDTH >= 800 && autoDashboardTile;
    lv_coord_t tileW = MIRA_CARD_TILE;
    if (adaptive) {
      const uint8_t total = _homeStandardInfoTotal;
      const uint8_t lastRow = total % 4 ? total % 4 : 4;
      const bool finalRow = _homeStandardInfoCreated > total - lastRow;
      uint8_t columns = finalRow ? lastRow : 4;
      if (columns < 2) columns = 2; // one tile is 390px, not 786px
      tileW = (MIRA_CARD_FULL - (columns - 1) * 6) / columns;
    }
    b.card = makeCard(page, adaptive ? 142 : (MIRA_DISPLAY_WIDTH >= 800 ? 78 : 66));
    lv_obj_set_width(b.card, tileW);
    lv_obj_clear_flag(b.card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(b.card, 4, 0);

    b.valueLabel = lv_label_create(b.card);
    String txt = String("--") + (b.unit.length() ? " " + b.unit : "");
    lv_label_set_text(b.valueLabel, txt.c_str());
    lv_obj_set_width(b.valueLabel, adaptive ? tileW - 14 : MIRA_LABEL_TILE);
    lv_label_set_long_mode(b.valueLabel, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(b.valueLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(b.valueLabel, colorAccent(), 0);
    lv_obj_set_style_text_font(b.valueLabel,
      adaptive ? &lv_font_montserrat_28 : &lv_font_montserrat_20, 0);
    lv_obj_align(b.valueLabel, LV_ALIGN_TOP_MID, 0, adaptive ? 48 : 7);

    b.infoNameLabel = lv_label_create(b.card);
    lv_obj_t* name = b.infoNameLabel;
    lv_label_set_text(name, (b.displayName.length() ? b.displayName : b.name).c_str());
    lv_obj_set_width(name, adaptive ? tileW - 18 : MIRA_LABEL_TILE);
    lv_label_set_long_mode(name, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(name, COL_MUTED, 0);
    lv_obj_set_style_text_font(name, adaptive ? &mira_font_fr_16 : &mira_font_fr_12, 0);
    lv_obj_align(name, adaptive ? LV_ALIGN_TOP_MID : LV_ALIGN_BOTTOM_MID, 0, adaptive ? 12 : -6);
    if (adaptive) {
      b.infoLastSeenLabel = lv_label_create(b.card);
      lv_label_set_text(b.infoLastSeenLabel, "En attente des données");
      lv_obj_set_width(b.infoLastSeenLabel, tileW - 18);
      lv_label_set_long_mode(b.infoLastSeenLabel, LV_LABEL_LONG_CLIP);
      lv_obj_set_style_text_align(b.infoLastSeenLabel, LV_TEXT_ALIGN_CENTER, 0);
      lv_obj_set_style_text_color(b.infoLastSeenLabel, COL_MUTED, 0);
      lv_obj_set_style_text_font(b.infoLastSeenLabel, &mira_font_fr_12, 0);
      lv_obj_align(b.infoLastSeenLabel, LV_ALIGN_BOTTOM_MID, 0, -10);
    }
    return;
  }

  if (forcedHero || valueDominant) {
    b.card = makeCard(page, forcedHero ? 84 : 72);
    lv_obj_clear_flag(b.card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(b.card, 4, 0);

    b.valueLabel = lv_label_create(b.card);
    String txt = String("--") + (b.unit.length() ? " " + b.unit : "");
    lv_label_set_text(b.valueLabel, txt.c_str());
    lv_obj_set_width(b.valueLabel, MIRA_LABEL_AUTO);
    lv_label_set_long_mode(b.valueLabel, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(b.valueLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(b.valueLabel, colorAccent(), 0);
    lv_obj_set_style_text_font(b.valueLabel, &lv_font_montserrat_28, 0);
    lv_obj_align(b.valueLabel, LV_ALIGN_TOP_MID, 0, forcedHero ? 4 : 2);

    b.infoNameLabel = lv_label_create(b.card);
    lv_obj_t* name = b.infoNameLabel;
    lv_label_set_text(name, (b.displayName.length() ? b.displayName : b.name).c_str());
    lv_obj_set_width(name, MIRA_LABEL_AUTO);
    lv_label_set_long_mode(name, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(name, COL_MUTED, 0);
    lv_obj_set_style_text_font(name, &mira_font_fr_12, 0);
    lv_obj_align(name, LV_ALIGN_BOTTOM_MID, 0, -4);
    return;
  }

  b.card = makeCard(page, compact ? 30 : 36);
  lv_obj_clear_flag(b.card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_pad_left(b.card, 4, 0);
  lv_obj_set_style_pad_right(b.card, 4, 0);
  lv_obj_set_style_pad_top(b.card, 0, 0);
  lv_obj_set_style_pad_bottom(b.card, 0, 0);
  lv_obj_set_style_pad_column(b.card, compact ? 4 : 6, 0);
  lv_obj_set_flex_flow(b.card, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(b.card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  String displayName = (b.displayName.length() ? b.displayName : b.name);
  if (displayName.length() && !displayName.endsWith(":")) displayName += ":";

  b.infoNameLabel = lv_label_create(b.card);
  lv_obj_t* name = b.infoNameLabel;
  lv_label_set_text(name, displayName.c_str());
  lv_label_set_long_mode(name, LV_LABEL_LONG_CLIP);
  lv_obj_set_style_text_color(name, COL_TEXT, 0);
  lv_obj_set_style_text_font(name, compact ? &mira_font_fr_12 : &mira_font_fr_14, 0);

  b.valueLabel = lv_label_create(b.card);
  String txt = String("--") + (b.unit.length() ? " " + b.unit : "");
  lv_label_set_text(b.valueLabel, txt.c_str());
  lv_label_set_long_mode(b.valueLabel, LV_LABEL_LONG_CLIP);
  lv_obj_set_style_text_color(b.valueLabel, colorAccent(), 0);
  lv_obj_set_style_text_font(b.valueLabel, compact ? &lv_font_montserrat_14 : &lv_font_montserrat_16, 0);
}
void MiraPanelUI::createButton(uint8_t idx) {
  Binding& b = _bindings[idx];
  const bool compact = b.style == "compact";
  b.card = makeCard(pageIndex(b.location), compact ? 44 : 54);
  lv_obj_clear_flag(b.card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(b.card, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(b.card, 0, 0);
  lv_obj_set_style_shadow_width(b.card, 0, 0);
  lv_obj_set_style_pad_all(b.card, 0, 0);

  b.control = lv_btn_create(b.card);
  lv_obj_set_size(b.control, MIRA_DISPLAY_WIDTH >= 800 ? (compact ? 344 : 360) : (compact ? 230 : 288), compact ? 34 : 44);
  lv_obj_center(b.control);
  lv_obj_set_style_bg_color(b.control, colorPanel2(), 0);
  lv_obj_set_style_bg_opa(b.control, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(b.control, colorAccent2(), 0);
  lv_obj_set_style_border_opa(b.control, LV_OPA_70, 0);
  lv_obj_set_style_border_width(b.control, 1, 0);
  lv_obj_set_style_bg_color(b.control, colorAccent(), LV_STATE_PRESSED);
  lv_obj_set_style_border_color(b.control, lv_color_hex(0x7DEEFF), LV_STATE_PRESSED);
  lv_obj_set_style_border_width(b.control, 1, LV_STATE_PRESSED);
  lv_obj_set_style_radius(b.control, 13, 0);
  lv_obj_set_style_shadow_color(b.control, lv_color_hex(0x000000), 0);
  lv_obj_set_style_shadow_opa(b.control, LV_OPA_20, 0);
  lv_obj_set_style_shadow_width(b.control, 4, 0);
  lv_obj_set_style_shadow_ofs_y(b.control, 2, 0);
  EventContext* ctx = makeContext(idx);
  lv_obj_add_event_cb(b.control, onButton, LV_EVENT_CLICKED, ctx);
  lv_obj_t* lbl = lv_label_create(b.control);
  lv_label_set_text(lbl, (b.displayName.length() ? b.displayName : b.name).c_str());
  lv_obj_set_style_text_color(lbl, COL_TEXT, 0);
  lv_obj_set_style_text_font(lbl, compact ? &mira_font_fr_14 : &mira_font_fr_16, 0);
  lv_obj_center(lbl);
}
void MiraPanelUI::createToggle(uint8_t idx) {
  Binding& b = _bindings[idx];
  const bool compact = b.style == "compact";
  b.card = makeCard(pageIndex(b.location), compact ? 44 : 54);
  lv_obj_set_style_pad_left(b.card, compact ? 14 : 16, 0);
  lv_obj_set_style_pad_right(b.card, compact ? 14 : 16, 0);
  lv_obj_set_flex_flow(b.card, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(b.card, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t* name = lv_label_create(b.card);
  lv_label_set_text(name, (b.displayName.length() ? b.displayName : b.name).c_str());
  lv_obj_set_style_text_color(name, COL_TEXT, 0);
  lv_obj_set_style_text_font(name, compact ? &mira_font_fr_14 : &mira_font_fr_16, 0);

  b.stateLabel = nullptr;
  b.control = lv_switch_create(b.card);
  lv_obj_set_size(b.control, compact ? 46 : 54, compact ? 23 : 28);
  lv_obj_set_ext_click_area(b.control, 6);
  lv_obj_set_style_bg_color(b.control, colorPanel2(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(b.control, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_color(b.control, colorAccent(), LV_PART_INDICATOR | LV_STATE_CHECKED);
  lv_obj_set_style_bg_color(b.control, lv_color_hex(0xF0F8FC), LV_PART_KNOB);
  lv_obj_set_style_border_color(b.control, colorBorder(), LV_PART_MAIN);
  lv_obj_set_style_border_width(b.control, 1, LV_PART_MAIN);
  lv_obj_set_style_anim_time(b.control, 160, 0);
  lv_obj_set_style_shadow_color(b.control, colorAccent(), LV_PART_KNOB | LV_STATE_CHECKED);
  lv_obj_set_style_shadow_opa(b.control, LV_OPA_20, LV_PART_KNOB | LV_STATE_CHECKED);
  lv_obj_set_style_shadow_width(b.control, 5, LV_PART_KNOB | LV_STATE_CHECKED);
  EventContext* ctx = makeContext(idx);
  lv_obj_add_event_cb(b.control, onToggle, LV_EVENT_VALUE_CHANGED, ctx);
}
void MiraPanelUI::createRange(uint8_t idx) {
  Binding& b = _bindings[idx];
  const bool compact = b.style == "compact";
  b.card = makeCard(pageIndex(b.location), compact ? 64 : 80);
  lv_obj_clear_flag(b.card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(b.card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(b.card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(b.card, compact ? 4 : 5, 0);

  lv_obj_t* top = makeTransparentRow(b.card, compact ? 22 : 24);
  lv_obj_t* name = lv_label_create(top);
  lv_label_set_text(name, (b.displayName.length() ? b.displayName : b.name).c_str());
  lv_obj_set_style_text_color(name, COL_TEXT, 0);
  lv_obj_set_style_text_font(name, compact ? &mira_font_fr_14 : &mira_font_fr_16, 0);

  b.valueLabel = lv_label_create(top);
  lv_label_set_text_fmt(b.valueLabel, "%d%s%s", b.currentValue, b.unit.length() ? " " : "", b.unit.c_str());
  lv_obj_set_style_text_color(b.valueLabel, colorAccent(), 0);
  lv_obj_set_style_text_font(b.valueLabel, compact ? &lv_font_montserrat_14 : &lv_font_montserrat_16, 0);

  b.control = lv_slider_create(b.card);
  lv_obj_set_size(b.control, MIRA_DISPLAY_WIDTH >= 800 ? 350 : (compact ? 230 : 258), compact ? 12 : 14);
  lv_obj_set_ext_click_area(b.control, 8);
  lv_obj_set_style_bg_color(b.control, colorPanel2(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(b.control, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(b.control, LV_RADIUS_CIRCLE, LV_PART_MAIN);
  lv_obj_set_style_radius(b.control, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
  lv_slider_set_range(b.control, b.minValue, b.maxValue);
  lv_slider_set_value(b.control, b.currentValue, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(b.control, colorAccent(), LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(b.control, lv_color_hex(0xEAF7FF), LV_PART_KNOB);
  lv_obj_set_style_border_color(b.control, colorAccent(), LV_PART_KNOB);
  lv_obj_set_style_border_width(b.control, 2, LV_PART_KNOB);
  lv_obj_set_style_shadow_color(b.control, colorAccent(), LV_PART_KNOB);
  lv_obj_set_style_shadow_opa(b.control, LV_OPA_20, LV_PART_KNOB);
  lv_obj_set_style_shadow_width(b.control, 5, LV_PART_KNOB);
  lv_obj_set_style_border_color(b.control, lv_color_hex(0x7DEEFF), LV_PART_KNOB | LV_STATE_PRESSED);
  lv_obj_set_style_border_width(b.control, 3, LV_PART_KNOB | LV_STATE_PRESSED);
  EventContext* ctx = makeContext(idx);
  lv_obj_add_event_cb(b.control, onRange, LV_EVENT_ALL, ctx);
}
void MiraPanelUI::createRoundRange(uint8_t idx) {
  Binding& b = _bindings[idx];
  const bool compact = b.style == "compact";
  const int cardH = compact ? 194 : 238;
  const int plate = compact ? 154 : 196;
  const int arc = compact ? 142 : 182;
  const int innerSize = compact ? 80 : 104;
  const int plateY = compact ? 18 : 18;
  const int arcY = compact ? 24 : 25;
  const int innerY = compact ? 55 : 64;

  b.card = makeCard(pageIndex(b.location), cardH);

  lv_obj_t* name = lv_label_create(b.card);
  lv_label_set_text(name, (b.displayName.length() ? b.displayName : b.name).c_str());
  lv_obj_set_style_text_color(name, COL_MUTED, 0);
  lv_obj_set_style_text_font(name, compact ? &mira_font_fr_14 : &mira_font_fr_16, 0);
  lv_obj_align(name, LV_ALIGN_TOP_MID, 0, 0);

  lv_obj_t* dialPlate = lv_obj_create(b.card);
  lv_obj_set_size(dialPlate, plate, plate);
  lv_obj_set_style_radius(dialPlate, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(dialPlate, colorPanel2(), 0);
  lv_obj_set_style_bg_opa(dialPlate, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(dialPlate, colorBorder(), 0);
  lv_obj_set_style_border_width(dialPlate, 1, 0);
  lv_obj_set_style_pad_all(dialPlate, 0, 0);
  lv_obj_clear_flag(dialPlate, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(dialPlate, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_align(dialPlate, LV_ALIGN_TOP_MID, 0, plateY);

  b.control = lv_arc_create(b.card);
  lv_obj_set_size(b.control, arc, arc);
  lv_arc_set_range(b.control, b.minValue * 2, b.maxValue * 2);
  lv_arc_set_value(b.control, b.currentValue * 2);
  lv_arc_set_bg_angles(b.control, 135, 45);
  lv_obj_align(b.control, LV_ALIGN_TOP_MID, 0, arcY);
  lv_obj_set_style_arc_width(b.control, compact ? 12 : 15, LV_PART_MAIN);
  lv_obj_set_style_arc_width(b.control, compact ? 12 : 15, LV_PART_INDICATOR);
  lv_obj_set_style_arc_width(b.control, compact ? 15 : 18, LV_PART_INDICATOR | LV_STATE_PRESSED);
  lv_obj_set_style_arc_color(b.control, colorPanel2(), LV_PART_MAIN);
  lv_obj_set_style_arc_color(b.control, colorAccent(), LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(b.control, LV_OPA_TRANSP, LV_PART_KNOB);
  lv_obj_set_style_border_width(b.control, 0, LV_PART_KNOB);
  lv_obj_set_style_pad_all(b.control, 0, LV_PART_KNOB);

  lv_obj_t* inner = lv_obj_create(b.card);
  lv_obj_set_size(inner, innerSize, innerSize);
  lv_obj_set_style_radius(inner, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(inner, colorBg(), 0);
  lv_obj_set_style_bg_opa(inner, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(inner, colorBorder(), 0);
  lv_obj_set_style_border_width(inner, 2, 0);
  lv_obj_clear_flag(inner, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_align(inner, LV_ALIGN_TOP_MID, 0, innerY);

  b.valueLabel = lv_label_create(inner);
  String roundValue = roundRangeDisplayValue(b.currentValue, b.unit);
  lv_label_set_text(b.valueLabel, roundValue.c_str());
  lv_obj_set_style_text_color(b.valueLabel, COL_TEXT, 0);
  lv_obj_set_style_text_font(b.valueLabel, compact ? &lv_font_montserrat_20 : &lv_font_montserrat_28, 0);
  lv_obj_center(b.valueLabel);

  EventContext* ctx = makeContext(idx);
  lv_obj_add_event_cb(b.control, onRoundRange, LV_EVENT_ALL, ctx);
}
// One native thermostat card, regardless of page. The three controls inside
// are NOT three flex children: they are grouped inside this single card.
// Horizontal on Waveshare 4.3C, vertical on WT32 SC01 Plus.
void MiraPanelUI::createThermostat(uint8_t idx) {
  Binding& b = _bindings[idx];
  const bool wide = MIRA_DISPLAY_WIDTH >= 800;
  const lv_coord_t cardW = MIRA_CARD_FULL;
  // Use the whole content viewport while leaving margin above Mira's nav.
  const lv_coord_t cardH = MIRA_CONTENT_HEIGHT - 16;
  b.card = makeCard(pageIndex(b.location), cardH);
  lv_obj_set_width(b.card, cardW);
  lv_obj_set_layout(b.card, 0);  // No child layout; supported by both LVGL 8 targets
  lv_obj_clear_flag(b.card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_pad_all(b.card, 0, 0);

  // Header: configured display name + last known heating mode.
  lv_obj_t* header = lv_label_create(b.card);
  String title = b.displayName.length() ? b.displayName : b.name;
  lv_label_set_text(header, title.c_str());
  lv_obj_set_style_text_color(header, COL_TEXT, 0);
  lv_obj_set_style_text_font(header, &mira_font_fr_16, 0);
  lv_obj_set_width(header, wide ? 300 : cardW - 126);
  lv_label_set_long_mode(header, LV_LABEL_LONG_CLIP);
  lv_obj_set_style_text_align(header, LV_TEXT_ALIGN_LEFT, 0);
  lv_obj_set_pos(header, 14, 12);

  {
    b.thermostatModeBadge = lv_label_create(b.card);
    lv_label_set_text(b.thermostatModeBadge, b.listLabels[0].c_str());
    lv_obj_set_style_text_color(b.thermostatModeBadge, colorAccent(), 0);
    lv_obj_set_style_text_font(b.thermostatModeBadge,
                               wide ? &mira_font_fr_14 : &mira_font_fr_12, 0);
    lv_obj_set_width(b.thermostatModeBadge, wide ? 170 : 105);
    lv_label_set_long_mode(b.thermostatModeBadge, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(b.thermostatModeBadge, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(b.thermostatModeBadge, cardW - (wide ? 185 : 115), 13);
  }

  // Reuse the EXACT RoundRange base design, scaled proportionally to the
  // existing thermostat dial sizes. No changes to the card geometry or the
  // other thermostat controls (temperature, mode, navigation, Jeedom).
  // RoundRange reference (normal): plate=196, arc=182, center=104;
  // main arc=15 and pressed arc=18.
  const lv_coord_t arcSize = wide ? 296 : 214;
  const lv_coord_t arcX = wide ? 27 : (cardW - arcSize) / 2;
  const lv_coord_t arcY = wide ? 63 : 41;
  const lv_coord_t plateSize = (arcSize * 196 + 91) / 182;
  const lv_coord_t innerSize = (arcSize * 104 + 91) / 182;
  const lv_coord_t arcWidth = (arcSize * 15 + 91) / 182;
  const lv_coord_t pressedWidth = (arcSize * 18 + 91) / 182;

  lv_obj_t* dialPlate = lv_obj_create(b.card);
  lv_obj_set_size(dialPlate, plateSize, plateSize);
  lv_obj_set_pos(dialPlate,
                 arcX - (plateSize - arcSize) / 2,
                 arcY - (plateSize - arcSize) / 2);
  lv_obj_set_style_radius(dialPlate, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(dialPlate, colorPanel2(), 0);
  lv_obj_set_style_bg_opa(dialPlate, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(dialPlate, colorBorder(), 0);
  lv_obj_set_style_border_width(dialPlate, 1, 0);
  lv_obj_set_style_pad_all(dialPlate, 0, 0);
  lv_obj_clear_flag(dialPlate, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(dialPlate, LV_OBJ_FLAG_CLICKABLE);

  b.control = lv_arc_create(b.card);
  lv_obj_set_size(b.control, arcSize, arcSize);
  lv_obj_set_pos(b.control, arcX, arcY);
  lv_arc_set_range(b.control, b.minValue * 2, b.maxValue * 2);
  lv_arc_set_value(b.control, (int)roundf(b.thermostatSetpoint * 2.0f));
  lv_arc_set_bg_angles(b.control, 135, 45);
  lv_obj_set_style_arc_width(b.control, arcWidth, LV_PART_MAIN);
  lv_obj_set_style_arc_width(b.control, arcWidth, LV_PART_INDICATOR);
  lv_obj_set_style_arc_width(b.control, pressedWidth, LV_PART_INDICATOR | LV_STATE_PRESSED);
  lv_obj_set_style_arc_color(b.control, colorPanel2(), LV_PART_MAIN);
  lv_obj_set_style_arc_color(b.control, colorAccent(), LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(b.control, LV_OPA_TRANSP, LV_PART_KNOB);
  lv_obj_set_style_border_width(b.control, 0, LV_PART_KNOB);
  lv_obj_set_style_pad_all(b.control, 0, LV_PART_KNOB);
  lv_obj_set_ext_click_area(b.control, 10);

  // The RoundRange inner medallion MUST be proportional to the outer arc.
  // Previous inner=132/116 made the large Waveshare/WT32 dials resemble a
  // vinyl record; base RoundRange uses 104/182 of the arc diameter.
  lv_obj_t* inner = lv_obj_create(b.card);
  lv_obj_set_size(inner, innerSize, innerSize);
  lv_obj_set_pos(inner, arcX + (arcSize - innerSize) / 2,
                 arcY + (arcSize - innerSize) / 2);
  lv_obj_set_style_radius(inner, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(inner, colorBg(), 0);
  lv_obj_set_style_bg_opa(inner, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(inner, colorBorder(), 0);
  lv_obj_set_style_border_width(inner, 2, 0);
  lv_obj_set_style_pad_all(inner, 0, 0);
  lv_obj_clear_flag(inner, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(inner, LV_OBJ_FLAG_CLICKABLE);

  // Labels belong to the actual RoundRange center, not the whole card;
  // unlike the previous implementation the caption cannot cross the rim.
  b.valueLabel = lv_label_create(inner);
  lv_label_set_text(b.valueLabel, (String(b.thermostatSetpoint, 1) + "°").c_str());
  lv_obj_set_width(b.valueLabel, innerSize - 12);
  lv_obj_set_style_text_align(b.valueLabel, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(b.valueLabel, COL_TEXT, 0);
  lv_obj_set_style_text_font(b.valueLabel,
                            wide ? &lv_font_montserrat_40 : &lv_font_montserrat_28, 0);
  lv_obj_align(b.valueLabel, LV_ALIGN_CENTER, 0, -16);

  lv_obj_t* hint = lv_label_create(inner);
  lv_label_set_text(hint, "CONSIGNE");
  lv_obj_set_width(hint, innerSize - 12);
  lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(hint, COL_MUTED, 0);
  lv_obj_set_style_text_font(hint, &mira_font_fr_14, 0);
  lv_obj_align(hint, LV_ALIGN_CENTER, 0, 26);
  EventContext* rangeCtx = makeContext(idx);
  lv_obj_add_event_cb(b.control, onThermostatRange, LV_EVENT_ALL, rangeCtx);

  // Current temperature: read-only. Jeedom /updatenr writes the Info ID;
  // it must NEVER send a new heating command.
  lv_obj_t* tempHeading = lv_label_create(b.card);
  lv_label_set_text(tempHeading, "Température actuelle");
  lv_obj_set_style_text_color(tempHeading, COL_MUTED, 0);
  lv_obj_set_style_text_font(tempHeading, &mira_font_fr_14, 0);
  lv_obj_set_width(tempHeading, wide ? 360 : cardW - 24);
  lv_obj_set_style_text_align(tempHeading, wide ? LV_TEXT_ALIGN_LEFT : LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_pos(tempHeading, wide ? 389 : 12, wide ? 122 : 282);

  b.thermostatTemperatureLabel = lv_label_create(b.card);
  lv_label_set_text(b.thermostatTemperatureLabel, "-- °C");
  lv_obj_set_style_text_color(b.thermostatTemperatureLabel, COL_TEXT, 0);
  lv_obj_set_style_text_font(b.thermostatTemperatureLabel,
                            wide ? &lv_font_montserrat_40 : &lv_font_montserrat_28, 0);
  lv_obj_set_width(b.thermostatTemperatureLabel, wide ? 350 : cardW - 24);
  lv_obj_set_style_text_align(b.thermostatTemperatureLabel,
                              wide ? LV_TEXT_ALIGN_LEFT : LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_pos(b.thermostatTemperatureLabel, wide ? 389 : 12, wide ? 156 : 299);

  if (wide) {
    lv_obj_t* separator = lv_obj_create(b.card);
    lv_obj_set_size(separator, cardW - 408, 1);
    lv_obj_set_pos(separator, 386, 237);
    lv_obj_set_style_bg_color(separator, colorBorder(), 0);
    lv_obj_set_style_bg_opa(separator, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(separator, 0, 0);
    lv_obj_clear_flag(separator, LV_OBJ_FLAG_CLICKABLE);
  }

  lv_obj_t* modeHeading = lv_label_create(b.card);
  lv_label_set_text(modeHeading, "Mode");
  lv_obj_set_style_text_color(modeHeading, COL_MUTED, 0);
  lv_obj_set_style_text_font(modeHeading, &mira_font_fr_14, 0);
  lv_obj_set_width(modeHeading, wide ? 360 : cardW - 24);
  lv_obj_set_style_text_align(modeHeading, wide ? LV_TEXT_ALIGN_LEFT : LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_pos(modeHeading, wide ? 389 : 12, wide ? 265 : 337);

  b.thermostatModeControl = lv_dropdown_create(b.card);
  String modes;
  for (uint8_t i = 0; i < b.listCount; ++i) {
    if (i) modes += "\n";
    modes += b.listLabels[i];
  }
  lv_dropdown_set_options(b.thermostatModeControl, modes.c_str());
  lv_dropdown_set_selected(b.thermostatModeControl, 0);
  lv_obj_set_size(b.thermostatModeControl, wide ? cardW - 407 : cardW - 28, 43);
  lv_obj_set_pos(b.thermostatModeControl, wide ? 387 : 14, wide ? 294 : 356);
  // Open the menu towards the card, not across the physical navigation bar.
  lv_dropdown_set_dir(b.thermostatModeControl, LV_DIR_TOP);
  lv_obj_set_style_bg_color(b.thermostatModeControl, colorPanel2(), 0);
  lv_obj_set_style_text_color(b.thermostatModeControl, COL_TEXT, 0);
  lv_obj_set_style_text_font(b.thermostatModeControl, &mira_font_fr_16, 0);
  lv_obj_set_style_border_width(b.thermostatModeControl, 1, 0);
  lv_obj_set_style_border_color(b.thermostatModeControl, colorBorder(), 0);
  lv_obj_set_style_radius(b.thermostatModeControl, 9, 0);
  EventContext* modeCtx = makeContext(idx);
  lv_obj_add_event_cb(b.thermostatModeControl, onThermostatMode,
                      LV_EVENT_VALUE_CHANGED, modeCtx);
  lv_obj_add_event_cb(b.thermostatModeControl, onThermostatDropdownOpen,
                      LV_EVENT_CLICKED, modeCtx);
}

void MiraPanelUI::createList(uint8_t idx, bool asButtons) {
  Binding& b = _bindings[idx];
  const bool compact = b.style == "compact";
  const bool segmented = asButtons && b.style == "segmented";

  if (b.listCount == 0) {
    createUnsupported(idx, "liste vide");
    return;
  }

  b.card = makeCard(pageIndex(b.location), asButtons ? LV_SIZE_CONTENT : (compact ? 68 : 82));
  if (!asButtons) {
    lv_obj_clear_flag(b.card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(b.card, LV_SCROLLBAR_MODE_OFF);
  }
  lv_obj_set_flex_flow(b.card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(b.card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(b.card, (compact || segmented) ? 3 : 5, 0);

  lv_obj_t* listName = lv_label_create(b.card);
  lv_label_set_text(listName, (b.displayName.length() ? b.displayName : b.name).c_str());
  lv_obj_set_width(listName, MIRA_DISPLAY_WIDTH >= 800 ? 364 : 280);
  lv_label_set_long_mode(listName, LV_LABEL_LONG_CLIP);
  lv_obj_set_style_text_align(listName, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(listName, segmented ? COL_MUTED : COL_TEXT, 0);
  lv_obj_set_style_text_font(listName, (compact || segmented) ? &mira_font_fr_14 : &mira_font_fr_16, 0);

  if (!asButtons) {
    b.control = lv_dropdown_create(b.card);
    lv_obj_set_width(b.control, MIRA_DISPLAY_WIDTH >= 800 ? 350 : (compact ? 236 : 268));
    lv_obj_set_style_text_font(b.control, compact ? &mira_font_fr_12 : &mira_font_fr_14, 0);
    lv_obj_set_style_text_color(b.control, COL_TEXT, 0);
    lv_obj_set_style_bg_color(b.control, colorPanel2(), 0);
    lv_obj_set_style_bg_opa(b.control, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(b.control, colorBorder(), 0);
    lv_obj_set_style_border_width(b.control, 1, 0);
    lv_obj_set_style_radius(b.control, 10, 0);
    lv_obj_set_style_pad_left(b.control, 10, 0);
    lv_obj_set_style_pad_right(b.control, 10, 0);

    String opts;
    for (uint8_t i = 0; i < b.listCount; ++i) {
      if (i) opts += "\n";
      opts += b.listLabels[i];
    }
    lv_dropdown_set_options(b.control, opts.c_str());
    lv_dropdown_set_selected(b.control, 0);
    EventContext* ctx = makeContext(idx);
    lv_obj_add_event_cb(b.control, onList, LV_EVENT_VALUE_CHANGED, ctx);
    return;
  }

  lv_obj_t* wrap = lv_obj_create(b.card);
  lv_obj_set_width(wrap, MIRA_DISPLAY_WIDTH >= 800 ? 364 : 280);
  lv_obj_set_height(wrap, LV_SIZE_CONTENT);
  lv_obj_clear_flag(wrap, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(wrap, segmented ? 1 : 0, 0);
  lv_obj_set_style_border_color(wrap, colorBorder(), 0);
  lv_obj_set_style_radius(wrap, segmented ? 12 : 0, 0);
  lv_obj_set_style_bg_color(wrap, colorPanel2(), 0);
  lv_obj_set_style_bg_opa(wrap, segmented ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
  lv_obj_set_style_pad_all(wrap, segmented ? 3 : 0, 0);
  lv_obj_set_style_pad_row(wrap, segmented ? 3 : (compact ? 4 : 5), 0);
  lv_obj_set_style_pad_column(wrap, segmented ? 3 : (compact ? 4 : 5), 0);
  lv_obj_set_flex_flow(wrap, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_flex_align(wrap, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  for (uint8_t i = 0; i < b.listCount; ++i) {
    lv_obj_t* btn = lv_btn_create(wrap);
    b.listButtons[i] = btn;

    if (segmented) {
      const int btnWidth = MIRA_DISPLAY_WIDTH >= 800 ? (b.listCount <= 2 ? 170 : 110) : (b.listCount <= 2 ? 134 : 88);
      lv_obj_set_size(btn, btnWidth, 32);
      lv_obj_set_style_radius(btn, 9, 0);
      lv_obj_set_style_border_width(btn, 0, 0);
      lv_obj_set_style_shadow_width(btn, 0, 0);
      lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_STATE_PRESSED);
      lv_obj_set_style_border_color(btn, lv_color_hex(0xFFC56B), LV_STATE_PRESSED);
      lv_obj_set_style_border_width(btn, 1, LV_STATE_PRESSED);
      lv_obj_set_style_bg_color(btn, colorPanel2(), 0);
      lv_obj_set_style_bg_color(btn, colorAccent(), LV_STATE_PRESSED);
    } else {
      lv_obj_set_size(btn, MIRA_DISPLAY_WIDTH >= 800 ? (compact ? 110 : 170) : (compact ? 88 : 132), compact ? 34 : 38);
      lv_obj_set_style_bg_color(btn, colorAccent(), LV_STATE_PRESSED);
      lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_STATE_PRESSED);
      lv_obj_set_style_border_color(btn, lv_color_hex(0xFFC56B), LV_STATE_PRESSED);
      lv_obj_set_style_border_width(btn, 1, LV_STATE_PRESSED);
    }

    EventContext* ctx = makeContext(idx, i);
    lv_obj_add_event_cb(btn, onListButton, LV_EVENT_CLICKED, ctx);

    lv_obj_t* lbl = lv_label_create(btn);
    lv_label_set_text(lbl, b.listLabels[i].c_str());
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_color(lbl, COL_TEXT, 0);
    lv_obj_set_style_text_font(lbl, (compact || segmented) ? &mira_font_fr_14 : &mira_font_fr_16, 0);
    lv_obj_center(lbl);
  }

  updateListVisual(b);
}
void MiraPanelUI::createColor(uint8_t idx) {
  Binding& b = _bindings[idx];
  b.card = makeCard(pageIndex(b.location), 196);
  makeNameLabel(b.card, (b.displayName.length() ? b.displayName : b.name));
  b.control = lv_colorwheel_create(b.card, true);
  lv_obj_set_size(b.control, 150, 150);
  lv_obj_set_style_arc_width(b.control, 14, LV_PART_MAIN);
  lv_obj_set_style_border_width(b.control, 2, LV_PART_KNOB);
  lv_obj_set_style_border_color(b.control, colorAccent(), LV_PART_KNOB);
  lv_colorwheel_set_mode(b.control, LV_COLORWHEEL_MODE_HUE);
  lv_colorwheel_set_mode_fixed(b.control, true);
  lv_obj_align(b.control, LV_ALIGN_CENTER, 0, 10);
  lv_colorwheel_set_rgb(b.control, lv_color_make(50, 120, 180));
  EventContext* ctx = makeContext(idx);
  lv_obj_add_event_cb(b.control, onColor, LV_EVENT_ALL, ctx);
}

void MiraPanelUI::createDateTime(uint8_t idx) {
  Binding& b = _bindings[idx];

  // Dashboard clock = header, not another card. This keeps the visual hierarchy
  // while recovering roughly one widget row of useful screen height.
  b.card = makeCard(pageIndex(b.location), 82);
  lv_obj_set_width(b.card, MIRA_CARD_FULL);
  lv_obj_clear_flag(b.card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(b.card, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(b.card, 0, 0);
  lv_obj_set_style_shadow_width(b.card, 0, 0);
  lv_obj_set_style_pad_all(b.card, 0, 0);

  if (MIRA_DISPLAY_WIDTH >= 800 && b.layout != "free") {
    // Same Montserrat typography as WT32 HOME, natively rasterized at 96px
    // for Waveshare 800px; no image enlargement and no seven-segment face.
    // Keep the existing Waveshare-wide dashboard date and connection status.
    lv_obj_set_height(b.card, 158);
    _dateTimeLabel = lv_label_create(b.card);
    b.valueLabel = _dateTimeLabel;
    lv_label_set_text(_dateTimeLabel, "--:--");
    lv_obj_set_align(_dateTimeLabel, LV_ALIGN_TOP_LEFT);
    lv_obj_set_width(_dateTimeLabel, 370);
    lv_label_set_long_mode(_dateTimeLabel, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_color(_dateTimeLabel, lv_color_white(), 0);
#if defined(MIRA_TARGET_WAVESHARE_43C)
    lv_obj_set_style_text_font(_dateTimeLabel, &mira_home_montserrat_96, 0);
#else
    lv_obj_set_style_text_font(_dateTimeLabel, &lv_font_montserrat_40, 0);
#endif
    lv_obj_set_style_text_align(_dateTimeLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(_dateTimeLabel, 14, 0);

    _dateSubLabel = lv_label_create(b.card);
    lv_label_set_text(_dateSubLabel, "-- -- ----");
    lv_obj_set_align(_dateSubLabel, LV_ALIGN_TOP_LEFT);
    lv_obj_set_width(_dateSubLabel, 370);
    lv_obj_set_style_text_color(_dateSubLabel, COL_MUTED, 0);
    lv_obj_set_style_text_font(_dateSubLabel, &mira_font_fr_16, 0);
    lv_obj_set_style_text_align(_dateSubLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(_dateSubLabel, 14, 125);

    _clockStatusLabel = lv_label_create(b.card);
    lv_obj_set_width(_clockStatusLabel, 355);
    lv_obj_set_style_text_font(_clockStatusLabel, &mira_font_fr_16, 0);
    lv_obj_set_style_text_color(_clockStatusLabel, COL_MUTED, 0);
    lv_label_set_text(_clockStatusLabel, "MIRA PANEL\nConnexion...\nMode jour");
    lv_obj_set_pos(_clockStatusLabel, 422, 36);
    return;
  }
  b.valueLabel = lv_label_create(b.card);
  lv_label_set_text(b.valueLabel, "--:--");
  lv_obj_set_width(b.valueLabel, MIRA_CARD_FULL);
  lv_obj_set_style_text_color(b.valueLabel, COL_TEXT, 0);
  lv_obj_set_style_text_font(b.valueLabel, &lv_font_montserrat_40, 0);
  lv_obj_set_style_text_color(b.valueLabel, lv_color_white(), 0);
  lv_obj_set_style_text_align(b.valueLabel, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(b.valueLabel, LV_ALIGN_TOP_MID, 0, -2);

  _dateSubLabel = lv_label_create(b.card);
  lv_label_set_text(_dateSubLabel, "-- -- ----");
  lv_obj_set_width(_dateSubLabel, MIRA_DISPLAY_WIDTH >= 800 ? 780 : 300);
  lv_obj_set_style_text_color(_dateSubLabel, COL_MUTED, 0);
  lv_obj_set_style_text_font(_dateSubLabel, &mira_font_fr_14, 0);
  lv_obj_set_style_text_align(_dateSubLabel, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(_dateSubLabel, LV_ALIGN_TOP_MID, 0, 47);

  if (!_dateTimeLabel) _dateTimeLabel = b.valueLabel;
}
void MiraPanelUI::createUnsupported(uint8_t idx, const String& originalType) {
  Binding& b = _bindings[idx];
  b.card = makeCard(pageIndex(b.location), 62);
  lv_obj_t* lbl = lv_label_create(b.card);
  String txt = b.name.length() ? b.name : "Élément";
  txt += "\n[" + originalType + " : pas encore géré]";
  lv_label_set_text(lbl, txt.c_str());
  lv_obj_set_style_text_color(lbl, COL_MUTED, 0);
  lv_obj_set_style_text_font(lbl, &mira_font_fr_14, 0);
  lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_center(lbl);
}


void MiraPanelUI::buildEnergyPreviewPage() {
  // The fifth page is navigation-only. Never register actions or emit
  // placeholder readings to Jeedom; the next iteration can add real widgets.
  lv_obj_t* page = _pages[4];
  lv_obj_clean(page);
  lv_obj_t* card = lv_obj_create(page);
  const bool wide = MIRA_DISPLAY_WIDTH >= 800;
  lv_obj_set_size(card, wide ? 650 : 304, wide ? 318 : 340);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(card, colorPanel(), 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(card, colorBorder(), 0);
  lv_obj_set_style_border_width(card, 1, 0);
  lv_obj_set_style_radius(card, 14, 0);
  lv_obj_set_style_pad_all(card, 0, 0);

  lv_obj_t* title = lv_label_create(card);
  lv_label_set_text(title, "Énergie");
  lv_obj_set_style_text_color(title, COL_TEXT, 0);
  lv_obj_set_style_text_font(title, &mira_font_fr_16, 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 24);

  lv_obj_t* notice = lv_label_create(card);
  lv_label_set_text(notice, "PAGE DE DÉMONSTRATION");
  lv_obj_set_style_text_color(notice, colorAccent(), 0);
  lv_obj_set_style_text_font(notice, &mira_font_fr_12, 0);
  lv_obj_align(notice, LV_ALIGN_TOP_MID, 0, 56);

  lv_obj_t* explanation = lv_label_create(card);
  lv_obj_set_width(explanation, wide ? 570 : 256);
  lv_label_set_long_mode(explanation, LV_LABEL_LONG_WRAP);
  lv_label_set_text(explanation,
      "Une quatrième catégorie à tester, sans commandes Jeedom.\n"
      "Pour revenir, utilise la barre du bas ou le menu Pages.");
  lv_obj_set_style_text_font(explanation, &mira_font_fr_14, 0);
  lv_obj_set_style_text_color(explanation, COL_MUTED, 0);
  lv_obj_set_style_text_align(explanation, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(explanation, LV_ALIGN_TOP_MID, 0, 91);

  lv_obj_t* divider = lv_obj_create(card);
  lv_obj_set_size(divider, wide ? 480 : 245, 1);
  lv_obj_set_style_bg_color(divider, colorBorder(), 0);
  lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(divider, 0, 0);
  lv_obj_clear_flag(divider, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_align(divider, LV_ALIGN_TOP_MID, 0, wide ? 166 : 187);

  const char* future[3] = {"Production solaire", "Consommation", "Batteries"};
  for (uint8_t i = 0; i < 3; ++i) {
    lv_obj_t* item = lv_label_create(card);
    lv_label_set_text(item, future[i]);
    lv_obj_set_width(item, wide ? 500 : 256);
    lv_obj_set_style_text_color(item, COL_MUTED, 0);
    lv_obj_set_style_text_font(item, &mira_font_fr_14, 0);
    lv_obj_set_style_text_align(item, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(item, LV_ALIGN_TOP_MID, 0, (wide ? 190 : 212) + i * 31);
  }
}

void MiraPanelUI::buildPagePicker() {
  // Full-size content overlay, beneath the always-visible nav and sleep layer.
  // It does not change the geometry or LVGL tree of the existing widgets.
  _pagePicker = lv_obj_create(_screen);
  lv_obj_set_size(_pagePicker, MIRA_DISPLAY_WIDTH, MIRA_CONTENT_HEIGHT);
  lv_obj_set_pos(_pagePicker, 0, 0);
  lv_obj_set_style_pad_all(_pagePicker, 0, 0);
  lv_obj_set_style_border_width(_pagePicker, 0, 0);
  lv_obj_set_style_radius(_pagePicker, 0, 0);
  lv_obj_set_style_bg_color(_pagePicker, colorBg(), 0);
  lv_obj_set_style_bg_opa(_pagePicker, LV_OPA_90, 0);
  lv_obj_clear_flag(_pagePicker, LV_OBJ_FLAG_SCROLLABLE);

  _pagePickerCard = lv_obj_create(_pagePicker);
  const bool wide = MIRA_DISPLAY_WIDTH >= 800;
  lv_obj_set_size(_pagePickerCard, wide ? 460 : 298, 348);
  lv_obj_set_style_radius(_pagePickerCard, 16, 0);
  lv_obj_set_style_bg_color(_pagePickerCard, colorPanel2(), 0);
  lv_obj_set_style_bg_opa(_pagePickerCard, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(_pagePickerCard, colorBorder(), 0);
  lv_obj_set_style_border_width(_pagePickerCard, 1, 0);
  lv_obj_set_style_pad_all(_pagePickerCard, 0, 0);
  lv_obj_clear_flag(_pagePickerCard, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_center(_pagePickerCard);

  lv_obj_t* heading = lv_label_create(_pagePickerCard);
  lv_label_set_text(heading, "Toutes les pages");
  lv_obj_set_style_text_color(heading, COL_TEXT, 0);
  lv_obj_set_style_text_font(heading, &mira_font_fr_16, 0);
  lv_obj_align(heading, LV_ALIGN_TOP_MID, 0, 13);

  const char* names[PAGE_COUNT] = {
    "Accueil", "Lumières", "Températures", "Paramètres", "Énergie (test)"
  };
  const uint8_t order[PAGE_COUNT] = {0, 1, 2, 4, 3};
  for (uint8_t row = 0; row < PAGE_COUNT; ++row) {
    uint8_t page = order[row];
    lv_obj_t* btn = lv_btn_create(_pagePickerCard);
    _pagePickerButtons[page] = btn;
    lv_obj_set_size(btn, wide ? 420 : 268, 47);
    lv_obj_set_pos(btn, wide ? 20 : 15, 46 + row * 58);
    lv_obj_set_style_radius(btn, 9, 0);
    lv_obj_set_style_bg_color(btn, colorPanel(), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, colorBorder(), 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_add_event_cb(btn, onPagePick, LV_EVENT_CLICKED,
                        (void*)(uintptr_t)page);

    lv_obj_t* label = lv_label_create(btn);
    lv_label_set_text(label, names[page]);
    lv_obj_set_style_text_color(label, COL_TEXT, 0);
    lv_obj_set_style_text_font(label, &mira_font_fr_16, 0);
    lv_obj_set_width(label, wide ? 335 : 181);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(label);


  }
  lv_obj_add_flag(_pagePicker, LV_OBJ_FLAG_HIDDEN);
}

void MiraPanelUI::setPagePickerVisible(bool visible) {
  if (!_pagePicker) return;
  if (visible) lv_obj_clear_flag(_pagePicker, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(_pagePicker, LV_OBJ_FLAG_HIDDEN);
  if (_navPagesButton) {
    lv_obj_set_style_bg_color(_navPagesButton, colorAccent2(), 0);
    // Pages is always a blue navigation anchor, open or closed.
    lv_obj_set_style_bg_opa(_navPagesButton, LV_OPA_70, 0);
  }
  noteActivity();
}

void MiraPanelUI::buildSettingsPage() {
  lv_obj_clean(_pages[3]);

  // V0.3.1 : la page Params utilise toute la hauteur gagnée en supprimant
  // le bandeau. Les cartes sont en colonne pour éviter les chevauchements.
  _settingsCards[0] = makeSettingsCard(40);
  lv_obj_t* title = lv_label_create(_settingsCards[0]);
  lv_label_set_text(title, "Paramètres");
  lv_obj_set_width(title, MIRA_DISPLAY_WIDTH >= 800 ? 620 : 286);
  lv_label_set_long_mode(title, LV_LABEL_LONG_CLIP);
  lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(title, COL_TEXT, 0);
  lv_obj_set_style_text_font(title, &mira_font_fr_16, 0);
  lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

  _settingsCards[1] = makeSettingsCard(130);
  lv_obj_t* h1 = lv_label_create(_settingsCards[1]);
  lv_label_set_text(h1, "Affichage");
  lv_obj_set_style_text_color(h1, colorAccent(), 0);
  lv_obj_set_style_text_font(h1, &mira_font_fr_14, 0);

  lv_obj_t* brightRow = makeTransparentRow(_settingsCards[1], 28);
  lv_obj_t* brightLbl = lv_label_create(brightRow);
  lv_label_set_text(brightLbl, "Luminosité écran");
  lv_obj_set_style_text_color(brightLbl, COL_TEXT, 0);
  lv_obj_set_style_text_font(brightLbl, &mira_font_fr_12, 0);
  _brightnessValue = lv_label_create(brightRow);
  lv_label_set_text_fmt(_brightnessValue, "%u", _brightness);
  lv_obj_set_style_text_color(_brightnessValue, colorAccent(), 0);
  lv_obj_set_style_text_font(_brightnessValue, &mira_font_fr_12, 0);

  _brightnessSlider = lv_slider_create(_settingsCards[1]);
  lv_obj_set_size(_brightnessSlider, MIRA_DISPLAY_WIDTH >= 800 ? 600 : 280, 14);
  lv_slider_set_range(_brightnessSlider, 30, 255);
  lv_slider_set_value(_brightnessSlider, _brightness, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(_brightnessSlider, COL_ACCENT2, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(_brightnessSlider, COL_ACCENT, LV_PART_KNOB);
  lv_obj_add_event_cb(_brightnessSlider, onBrightness, LV_EVENT_VALUE_CHANGED, this);

  lv_obj_t* themeRow = makeTransparentRow(_settingsCards[1], 36);
  lv_obj_t* themeLbl = lv_label_create(themeRow);
  lv_label_set_text(themeLbl, "Thème");
  lv_obj_set_style_text_color(themeLbl, COL_TEXT, 0);
  lv_obj_set_style_text_font(themeLbl, &mira_font_fr_12, 0);
  _themeDropdown = lv_dropdown_create(themeRow);
  lv_obj_set_width(_themeDropdown, MIRA_DISPLAY_WIDTH >= 800 ? 205 : 126);
  lv_dropdown_set_options(_themeDropdown, "Mira\nSombre\nGlass");
  lv_dropdown_set_selected(_themeDropdown, _theme);
  lv_obj_set_style_text_font(_themeDropdown, &mira_font_fr_12, 0);
  lv_obj_set_style_text_color(_themeDropdown, COL_TEXT, 0);
  lv_obj_set_style_bg_color(_themeDropdown, colorPanel2(), 0);
  lv_obj_set_style_border_color(_themeDropdown, colorBorder(), 0);
  lv_obj_set_style_border_width(_themeDropdown, 1, 0);
  lv_obj_set_style_radius(_themeDropdown, 9, 0);
  lv_obj_add_event_cb(_themeDropdown, onTheme, LV_EVENT_VALUE_CHANGED, this);

  _settingsCards[2] = makeSettingsCard(72);
  lv_obj_t* h2 = lv_label_create(_settingsCards[2]);
  lv_label_set_text(h2, "Navigation");
  lv_obj_set_style_text_color(h2, colorAccent(), 0);
  lv_obj_set_style_text_font(h2, &mira_font_fr_14, 0);

  lv_obj_t* homeRow = makeTransparentRow(_settingsCards[2], 30);
  lv_obj_t* homeLbl = lv_label_create(homeRow);
  lv_label_set_text(homeLbl, "Retour accueil : 60 s");
  lv_obj_set_style_text_color(homeLbl, COL_TEXT, 0);
  lv_obj_set_style_text_font(homeLbl, &mira_font_fr_12, 0);
  _returnHomeSwitch = lv_switch_create(homeRow);
  lv_obj_set_size(_returnHomeSwitch, 48, 25);
  if (_returnHomeEnabled) lv_obj_add_state(_returnHomeSwitch, LV_STATE_CHECKED);
  lv_obj_set_style_bg_color(_returnHomeSwitch, COL_GREEN, LV_PART_INDICATOR | LV_STATE_CHECKED);
  lv_obj_add_event_cb(_returnHomeSwitch, onReturnHome, LV_EVENT_VALUE_CHANGED, this);

  _settingsCards[3] = makeSettingsCard(104);
  lv_obj_t* h3 = lv_label_create(_settingsCards[3]);
  lv_label_set_text(h3, "Veille");
  lv_obj_set_style_text_color(h3, colorAccent(), 0);
  lv_obj_set_style_text_font(h3, &mira_font_fr_14, 0);

  lv_obj_t* sleepRow = makeTransparentRow(_settingsCards[3], 30);
  lv_obj_t* sleepLbl = lv_label_create(sleepRow);
  lv_label_set_text(sleepLbl, "Mise en veille : 120 s");
  lv_obj_set_style_text_color(sleepLbl, COL_TEXT, 0);
  lv_obj_set_style_text_font(sleepLbl, &mira_font_fr_12, 0);
  _sleepSwitch = lv_switch_create(sleepRow);
  lv_obj_set_size(_sleepSwitch, 48, 25);
  if (_sleepEnabled) lv_obj_add_state(_sleepSwitch, LV_STATE_CHECKED);
  lv_obj_set_style_bg_color(_sleepSwitch, COL_GREEN, LV_PART_INDICATOR | LV_STATE_CHECKED);
  lv_obj_add_event_cb(_sleepSwitch, onSleepMode, LV_EVENT_VALUE_CHANGED, this);

  lv_obj_t* clockRow = makeTransparentRow(_settingsCards[3], 30);
  lv_obj_t* clockLbl = lv_label_create(clockRow);
  lv_label_set_text(clockLbl, "Horloge en veille");
  lv_obj_set_style_text_color(clockLbl, COL_TEXT, 0);
  lv_obj_set_style_text_font(clockLbl, &mira_font_fr_12, 0);
  _clockSleepSwitch = lv_switch_create(clockRow);
  lv_obj_set_size(_clockSleepSwitch, 48, 25);
  if (_clockInSleep) lv_obj_add_state(_clockSleepSwitch, LV_STATE_CHECKED);
  lv_obj_set_style_bg_color(_clockSleepSwitch, COL_GREEN, LV_PART_INDICATOR | LV_STATE_CHECKED);
  lv_obj_add_event_cb(_clockSleepSwitch, onClockSleep, LV_EVENT_VALUE_CHANGED, this);
}

void MiraPanelUI::applyTheme() {
  if (!_screen) return;

  const lv_color_t grad = (_theme == THEME_MIRA) ? lv_color_hex(0x0A2134) :
                          (_theme == THEME_GLASS) ? lv_color_hex(0x0B293D) : colorBg();

  lv_obj_set_style_bg_color(_screen, colorBg(), 0);
  lv_obj_set_style_bg_grad_color(_screen, grad, 0);
  lv_obj_set_style_bg_grad_dir(_screen, LV_GRAD_DIR_VER, 0);
  lv_obj_set_style_bg_opa(_screen, LV_OPA_COVER, 0);

  if (_nav) {
    lv_obj_set_style_bg_color(_nav, colorNav(), 0);
    lv_obj_set_style_bg_opa(_nav, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(_nav, colorBorder(), 0);
    lv_obj_set_style_border_opa(_nav, _theme == THEME_MIRA ? LV_OPA_50 : LV_OPA_70, 0);
  }

  for (uint8_t i = 0; i < PAGE_COUNT; ++i) {
    if (_pages[i]) {
      lv_obj_set_style_bg_color(_pages[i], colorBg(), 0);
      lv_obj_set_style_bg_grad_color(_pages[i], grad, 0);
      lv_obj_set_style_bg_grad_dir(_pages[i], LV_GRAD_DIR_VER, 0);
    }
  }

  for (uint8_t slot = 0; slot < FAVORITE_SLOTS; ++slot) {
    if (_favoriteButtons[slot]) {
      lv_obj_set_style_bg_color(_favoriteButtons[slot], colorAccent(), LV_STATE_PRESSED);
    }
  }
  if (_navPagesButton) {
    lv_obj_set_style_bg_color(_navPagesButton, colorAccent(), LV_STATE_PRESSED);
  }

  for (uint8_t i = 0; i < _bindingCount; ++i) {
    Binding& b = _bindings[i];
    if (b.card) {
      styleMainCard(b.card, b.type == W_DATETIME);
      if (b.type == W_DATETIME) {
        lv_obj_set_style_bg_opa(b.card, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(b.card, 0, 0);
        lv_obj_set_style_shadow_width(b.card, 0, 0);
      }
    }

    if (b.type == W_INFO && b.valueLabel) {
      lv_obj_set_style_text_color(b.valueLabel, colorAccent(), 0);
    }
    else if (b.type == W_BUTTON && b.control) {
      if (b.card) {
        lv_obj_set_style_bg_opa(b.card, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(b.card, 0, 0);
        lv_obj_set_style_shadow_width(b.card, 0, 0);
      }
      lv_obj_set_style_bg_color(b.control, colorPanel2(), 0);
      lv_obj_set_style_border_color(b.control, colorAccent2(), 0);
      lv_obj_set_style_border_opa(b.control, LV_OPA_70, 0);
      lv_obj_set_style_bg_color(b.control, colorAccent(), LV_STATE_PRESSED);
    }
    else if (b.type == W_TOGGLE && b.control) {
      lv_obj_set_style_bg_color(b.control, colorPanel2(), LV_PART_MAIN);
      lv_obj_set_style_border_color(b.control, colorBorder(), LV_PART_MAIN);
      lv_obj_set_style_bg_color(b.control, colorAccent(), LV_PART_INDICATOR | LV_STATE_CHECKED);
      lv_obj_set_style_bg_color(b.control, lv_color_hex(0xF0F8FC), LV_PART_KNOB);
    }
    else if (b.type == W_RANGE && b.control) {
      lv_obj_set_style_bg_color(b.control, colorPanel2(), LV_PART_MAIN);
      lv_obj_set_style_bg_color(b.control, colorAccent(), LV_PART_INDICATOR);
      lv_obj_set_style_bg_color(b.control, lv_color_hex(0xEAF7FF), LV_PART_KNOB);
      lv_obj_set_style_border_color(b.control, colorAccent(), LV_PART_KNOB);
      if (b.valueLabel) lv_obj_set_style_text_color(b.valueLabel, colorAccent(), 0);
    }
    else if (b.type == W_ROUNDRANGE && b.control) {
      lv_obj_set_style_arc_color(b.control, colorPanel2(), LV_PART_MAIN);
      lv_obj_set_style_arc_color(b.control, colorAccent(), LV_PART_INDICATOR);
    }
    else if (b.type == W_LIST && b.control) {
      lv_obj_set_style_bg_color(b.control, colorPanel2(), 0);
      lv_obj_set_style_border_color(b.control, colorBorder(), 0);
    }
    else if (b.type == W_LISTBUTTON) {
      updateListVisual(b);
    }
  }

  for (uint8_t i = 0; i < 4; ++i) {
    if (_settingsCards[i]) {
      lv_obj_set_style_bg_color(_settingsCards[i], colorPanel(), 0);
      lv_obj_set_style_bg_opa(_settingsCards[i], _theme == THEME_GLASS ? LV_OPA_80 : LV_OPA_COVER, 0);
      lv_obj_set_style_border_color(_settingsCards[i], colorBorder(), 0);
      lv_obj_set_style_border_opa(_settingsCards[i], _theme == THEME_MIRA ? LV_OPA_50 : LV_OPA_70, 0);
      lv_obj_set_style_radius(_settingsCards[i], _theme == THEME_GLASS ? 16 : 14, 0);
    }
  }

  if (_brightnessSlider) {
    lv_obj_set_style_bg_color(_brightnessSlider, colorPanel2(), LV_PART_MAIN);
    lv_obj_set_style_bg_color(_brightnessSlider, colorAccent(), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(_brightnessSlider, lv_color_hex(0xEAF7FF), LV_PART_KNOB);
    lv_obj_set_style_border_color(_brightnessSlider, colorAccent(), LV_PART_KNOB);
  }
  if (_brightnessValue) lv_obj_set_style_text_color(_brightnessValue, colorAccent(), 0);
  if (_themeDropdown) {
    lv_obj_set_style_bg_color(_themeDropdown, colorPanel2(), 0);
    lv_obj_set_style_border_color(_themeDropdown, colorBorder(), 0);
  }

  setNavActive(_currentPage);
}
void MiraPanelUI::loadFavorites() {
  // Independent NVS slots: removing a favorite leaves its physical position
  // empty until another page is assigned to it.
  for (uint8_t slot = 0; slot < FAVORITE_SLOTS; ++slot) {
    char key[8];
    snprintf(key, sizeof(key), "fav%u", slot);
    uint8_t page = _prefs.getUChar(key, slot);  // first install: Accueil, Lumières, Climat
    if (page != NO_FAVORITE && page >= PAGE_COUNT) page = NO_FAVORITE;
    if (page != NO_FAVORITE) {
      for (uint8_t previous = 0; previous < slot; ++previous) {
        if (_favoritePages[previous] == page) {
          page = NO_FAVORITE;
          break;
        }
      }
    }
    _favoritePages[slot] = page;
  }
}

void MiraPanelUI::pollFavorites() {
  // Jeedom persists the three favorite slots through /navfavorites.
  // Reload them once a second, on the LVGL-owning thread; no cross-task
  // modifications of UI objects, and no effect on widget programmation.
  if ((uint32_t)(millis() - _lastFavoritesPollMs) < 1000UL) return;
  _lastFavoritesPollMs = millis();
  uint8_t incoming[FAVORITE_SLOTS];
  bool valid = true;
  for (uint8_t slot = 0; slot < FAVORITE_SLOTS; ++slot) {
    char key[8];
    snprintf(key, sizeof(key), "fav%u", slot);
    incoming[slot] = _prefs.getUChar(key, slot);
    if (incoming[slot] != NO_FAVORITE && incoming[slot] >= PAGE_COUNT)
      valid = false;
    for (uint8_t other = 0; other < slot; ++other)
      if (incoming[slot] != NO_FAVORITE && incoming[slot] == incoming[other])
        valid = false;
  }
  if (!valid) return;
  bool changed = false;
  for (uint8_t slot = 0; slot < FAVORITE_SLOTS; ++slot) {
    if (_favoritePages[slot] != incoming[slot]) {
      _favoritePages[slot] = incoming[slot];
      changed = true;
    }
  }
  if (changed) {
    refreshFavoriteNav();
    setNavActive(_currentPage);
    Serial.println("[MIRA][NAV] Favoris Jeedom actualises sans redemarrage");
  }
}

void MiraPanelUI::refreshFavoriteNav() {
  // Icons-only strip, centered vertically; the last button (Pages) is fixed.
  // All changes happen on the UI thread after polling the persisted settings.
  for (uint8_t slot = 0; slot < FAVORITE_SLOTS; ++slot) {
    lv_obj_t* btn = _favoriteButtons[slot];
    if (!btn) continue;
    lv_obj_clean(btn);
    const uint8_t page = _favoritePages[slot];
    if (page == 1 || page == 2) {
      if (page == 1) makeBulbIcon(btn);
      else makeThermometerIcon(btn);
      continue; // the original pictograms are already centered
    }
    if (page == 4) {
      static const lv_point_t BOLT_SMALL[7] = {
        {8, 1}, {3, 11}, {8, 11}, {5, 20},
        {14, 7}, {10, 7}, {11, 1}
      };
      static const lv_point_t BOLT_WIDE[7] = {
        {11, 1}, {4, 13}, {10, 13}, {7, 25},
        {20, 8}, {13, 8}, {15, 1}
      };
      const bool wideIcon = MIRA_DISPLAY_WIDTH >= 800;
      lv_obj_t* bolt = lv_line_create(btn);
      lv_line_set_points(bolt, wideIcon ? BOLT_WIDE : BOLT_SMALL, 7);
      lv_obj_set_size(bolt, wideIcon ? 21 : 15, wideIcon ? 26 : 21);
      lv_obj_set_style_line_color(bolt, colorAccent(), LV_PART_MAIN);
      lv_obj_set_style_line_width(bolt, wideIcon ? 3 : 2, LV_PART_MAIN);
      lv_obj_set_style_line_rounded(bolt, true, LV_PART_MAIN);
      lv_obj_clear_flag(bolt, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_center(bolt);
      continue;
    }

    lv_obj_t* glyph = lv_label_create(btn);
    lv_label_set_text(glyph, page == 0 ? LV_SYMBOL_HOME :
                          page == 3 ? LV_SYMBOL_SETTINGS : LV_SYMBOL_PLUS);
    lv_obj_set_style_text_font(glyph, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(glyph,
        page == NO_FAVORITE ? COL_MUTED : colorAccent(), 0);
    lv_obj_center(glyph);
  }
}

void MiraPanelUI::setNavActive(uint8_t index) {
  if (index >= PAGE_COUNT) index = 0;
  for (uint8_t slot = 0; slot < FAVORITE_SLOTS; ++slot) {
    lv_obj_t* btn = _favoriteButtons[slot];
    if (!btn) continue;
    lv_obj_set_pos(btn, 1 + slot * (MIRA_DISPLAY_WIDTH / NAV_BUTTON_COUNT), 1);
    // Only Accueil gets a blue capsule. Other favorite slots stay flat,
    // even while their page is selected.
    lv_obj_set_style_bg_color(btn, colorAccent2(), 0);
    lv_obj_set_style_bg_opa(btn,
        _favoritePages[slot] == 0 ? LV_OPA_70 : LV_OPA_TRANSP, 0);
  }
  if (_navPagesButton) {
    lv_obj_set_pos(_navPagesButton,
                   1 + FAVORITE_SLOTS * (MIRA_DISPLAY_WIDTH / NAV_BUTTON_COUNT), 1);
    lv_obj_clear_flag(_navPagesButton, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_color(_navPagesButton, colorAccent2(), 0);
    lv_obj_set_style_bg_opa(_navPagesButton, LV_OPA_70, 0);
  }
  for (uint8_t i = 0; i < PAGE_COUNT; ++i) {
    if (_pagePickerButtons[i]) {
      lv_obj_set_style_bg_color(_pagePickerButtons[i],
        i == index ? colorAccent2() : colorPanel(), 0);
    }
  }
}

void MiraPanelUI::showPage(uint8_t index) {
  if (index >= PAGE_COUNT) return;
  for (uint8_t i = 0; i < PAGE_COUNT; ++i) {
    if (i == index) {
      lv_obj_clear_flag(_pages[i], LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(_pages[i], LV_OBJ_FLAG_HIDDEN);
    }
  }
  _currentPage = index;
  setPagePickerVisible(false);
  setNavActive(index);
  noteActivity();
}

// LVGL's timer task handles the touch immediately on both display targets.
void MiraPanelUI::onNavigationTimer(lv_timer_t* timer) {
  (void)timer;
  MiraPanelUI* ui = _uiForLvgl;
  if (!ui) return;
  if (ui->_pendingNavPage >= 0) {
    const uint8_t page = (uint8_t)ui->_pendingNavPage;
    ui->_pendingNavPage = -1;
    ui->showPage(page);
  }
  if (ui->_pendingTogglePagePicker) {
    ui->_pendingTogglePagePicker = false;
    ui->setPagePickerVisible(ui->_pagePicker &&
                            lv_obj_has_flag(ui->_pagePicker, LV_OBJ_FLAG_HIDDEN));
  }
}

void MiraPanelUI::onNav(lv_event_t* e) {
  if (!_uiForLvgl) return;
  uint8_t slot = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
  if (slot >= FAVORITE_SLOTS) return;
  uint8_t page = _uiForLvgl->_favoritePages[slot];
  if (page < PAGE_COUNT) _uiForLvgl->showPage(page);
}

void MiraPanelUI::onPagePick(lv_event_t* e) {
  if (!_uiForLvgl) return;
  uint8_t destination = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
  _uiForLvgl->showPage(destination);
}

void MiraPanelUI::onButton(lv_event_t* e) {
  EventContext* ctx = static_cast<EventContext*>(lv_event_get_user_data(e));
  if (!ctx || !ctx->ui) return;
  MiraPanelUI& ui = *ctx->ui;
  if (ui._suppressEvents || ctx->bindingIndex >= ui._bindingCount) return;
  ui.noteActivity();
  Binding& b = ui._bindings[ctx->bindingIndex];
  ui._server.IndexSetEvent(b.id);
  ui.publishMqttAction(b, "1");
}

void MiraPanelUI::onToggle(lv_event_t* e) {
  EventContext* ctx = static_cast<EventContext*>(lv_event_get_user_data(e));
  if (!ctx || !ctx->ui) return;
  MiraPanelUI& ui = *ctx->ui;
  if (ui._suppressEvents || ctx->bindingIndex >= ui._bindingCount) return;
  Binding& b = ui._bindings[ctx->bindingIndex];
  bool on = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
  int id = on ? b.idOn : b.idOff;
  if (b.stateLabel) {
    lv_label_set_text(b.stateLabel, on ? "ON" : "OFF");
    lv_obj_set_style_text_color(b.stateLabel, on ? COL_GREEN : COL_MUTED, 0);
  }
  ui.noteActivity();
  ui._server.IndexSetToggle(id);
  ui._server.IndexSetEvent(id);
  ui.publishMqttAction(b, on ? "1" : "0");
}

void MiraPanelUI::onRange(lv_event_t* e) {
  EventContext* ctx = static_cast<EventContext*>(lv_event_get_user_data(e));
  if (!ctx || !ctx->ui) return;
  MiraPanelUI& ui = *ctx->ui;
  if (ctx->bindingIndex >= ui._bindingCount) return;
  Binding& b = ui._bindings[ctx->bindingIndex];
  lv_event_code_t code = lv_event_get_code(e);
  int value = lv_slider_get_value(lv_event_get_target(e));
  b.currentValue = value;
  if (b.valueLabel) lv_label_set_text_fmt(b.valueLabel, "%d%s%s", value, b.unit.length() ? " " : "", b.unit.c_str());
  ui.noteActivity();
  if (ui._suppressEvents) return;

  const uint32_t now = millis();
  const bool finalSend = code == LV_EVENT_RELEASED;
  if (code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING || code == LV_EVENT_VALUE_CHANGED) {
    b.localInputUntilMs = now + 250;
  }
  if (finalSend) b.localInputUntilMs = now + 350;

  const bool liveSend = code == LV_EVENT_VALUE_CHANGED && value != b.lastLiveValue;
  if (liveSend) {
    b.lastLiveSendMs = now;
    b.lastLiveValue = value;
    ui._server.IndexSetRange(b.id, value);
    ui._server.IndexSetEvent(b.id);
    ui.publishMqttAction(b, String(value));
  }
  if (finalSend) {
    b.lastLiveSendMs = now;
    b.lastLiveValue = value;
    ui._server.IndexSetRange(b.id, value);
    ui._server.IndexSetEvent(b.id);
    ui.publishMqttAction(b, String(value));
  }
}

void MiraPanelUI::onRoundRange(lv_event_t* e) {
  EventContext* ctx = static_cast<EventContext*>(lv_event_get_user_data(e));
  if (!ctx || !ctx->ui) return;
  MiraPanelUI& ui = *ctx->ui;
  if (ctx->bindingIndex >= ui._bindingCount) return;
  Binding& b = ui._bindings[ctx->bindingIndex];
  lv_event_code_t code = lv_event_get_code(e);
  const int rawValue = lv_arc_get_value(lv_event_get_target(e));
  const float setpoint = rawValue * 0.5f;
  b.currentValue = (int)roundf(setpoint);
  if (b.valueLabel) {
    String txt = String(setpoint, 1);
    if (b.unit.length()) txt += " " + b.unit;
    lv_label_set_text(b.valueLabel, txt.c_str());
  }
  ui.noteActivity();
  if (ui._suppressEvents) return;

  const uint32_t now = millis();
  const bool finalSend = code == LV_EVENT_RELEASED;
  if (code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING || code == LV_EVENT_VALUE_CHANGED) {
    b.localInputUntilMs = now + 250;
  }
  if (finalSend) b.localInputUntilMs = now + 350;

  const bool liveSend = code == LV_EVENT_VALUE_CHANGED && rawValue != b.lastLiveValue;
  const String setpointText = (rawValue & 1) ? String(setpoint, 1) : String((int)setpoint);
  if (liveSend) {
    b.lastLiveSendMs = now;
    b.lastLiveValue = rawValue;
    ui._server.IndexSetRange(b.id, setpoint);
    ui._server.IndexSetEvent(b.id);
    ui.publishMqttAction(b, setpointText);
  }
  if (finalSend) {
    b.lastLiveSendMs = now;
    b.lastLiveValue = rawValue;
    ui._server.IndexSetRange(b.id, setpoint);
    ui._server.IndexSetEvent(b.id);
    ui.publishMqttAction(b, setpointText);
  }
}

// Only direct touch gestures produce Jeedom actions. Incoming Info/States
// update the controls without passing through this callback.
void MiraPanelUI::onThermostatRange(lv_event_t* e) {
  EventContext* ctx = static_cast<EventContext*>(lv_event_get_user_data(e));
  if (!ctx || !ctx->ui) return;
  MiraPanelUI& ui = *ctx->ui;
  if (ctx->bindingIndex >= ui._bindingCount) return;
  Binding& b = ui._bindings[ctx->bindingIndex];
  const lv_event_code_t code = lv_event_get_code(e);
  if (code != LV_EVENT_VALUE_CHANGED && code != LV_EVENT_RELEASED) return;
  if (ui._suppressEvents) return;
  const int halfDegrees = lv_arc_get_value(b.control);
  const float value = halfDegrees * 0.5f;
  b.thermostatSetpoint = value;
  b.currentValue = (int)roundf(value);
  if (b.valueLabel) {
    String displayed = String(value, 1) + "°";
    lv_label_set_text(b.valueLabel, displayed.c_str());
  }
  ui.noteActivity();
  const uint32_t now = millis();
  const bool finalSend = code == LV_EVENT_RELEASED;
  b.localInputUntilMs = now + (finalSend ? 350 : 250);
  // Cap network traffic while dragging; always send the final release.
  const bool changed = halfDegrees != b.lastLiveValue;
  const bool liveSend = code == LV_EVENT_VALUE_CHANGED && changed &&
                        (b.lastLiveSendMs == 0 ||
                         (uint32_t)(now - b.lastLiveSendMs) >= 130U);
  if (liveSend || finalSend) {
    b.lastLiveSendMs = now;
    b.lastLiveValue = halfDegrees;
    ui._server.IndexSetRange(b.id, value);
    ui._server.IndexSetEvent(b.id);
    if (ui._mqtt && ui._mqtt->enabled())
      ui._mqtt->publishAction(b.name + "__consigne", String(value, 1));
  }
}

// LVGL renders the opened list separately from the closed dropdown.
// Apply the French font and Mira palette to the actual popup every time.
void MiraPanelUI::onThermostatDropdownOpen(lv_event_t* e) {
  EventContext* ctx = static_cast<EventContext*>(lv_event_get_user_data(e));
  if (!ctx || !ctx->ui) return;
  MiraPanelUI& ui = *ctx->ui;
  if (ctx->bindingIndex >= ui._bindingCount) return;
  Binding& b = ui._bindings[ctx->bindingIndex];
  if (!b.thermostatModeControl) return;
  lv_obj_t* popup = lv_dropdown_get_list(b.thermostatModeControl);
  if (!popup) return;
  lv_obj_set_style_bg_opa(popup, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_color(popup, ui.colorPanel2(), LV_PART_MAIN);
  lv_obj_set_style_text_font(popup, &mira_font_fr_16, LV_PART_MAIN);
  lv_obj_set_style_text_color(popup, COL_TEXT, LV_PART_MAIN);
  lv_obj_set_style_border_color(popup, ui.colorBorder(), LV_PART_MAIN);
  lv_obj_set_style_border_width(popup, 1, LV_PART_MAIN);
  lv_obj_set_style_radius(popup, 9, LV_PART_MAIN);
  lv_obj_set_style_shadow_color(popup, ui.colorAccent(), LV_PART_MAIN);
  lv_obj_set_style_shadow_opa(popup, LV_OPA_20, LV_PART_MAIN);
  lv_obj_set_style_bg_color(popup, ui.colorAccent2(), LV_PART_SELECTED);
  lv_obj_set_style_text_color(popup, COL_TEXT, LV_PART_SELECTED);
  lv_obj_set_style_bg_color(popup, ui.colorAccent(), LV_PART_SELECTED | LV_STATE_CHECKED);
  lv_obj_set_style_text_color(popup, lv_color_hex(0xFFFFFF), LV_PART_SELECTED | LV_STATE_CHECKED);
  lv_obj_set_style_pad_top(popup, 5, LV_PART_MAIN);
  lv_obj_set_style_pad_bottom(popup, 5, LV_PART_MAIN);
}

void MiraPanelUI::onThermostatMode(lv_event_t* e) {
  EventContext* ctx = static_cast<EventContext*>(lv_event_get_user_data(e));
  if (!ctx || !ctx->ui) return;
  MiraPanelUI& ui = *ctx->ui;
  if (ui._suppressEvents || ctx->bindingIndex >= ui._bindingCount) return;
  Binding& b = ui._bindings[ctx->bindingIndex];
  const uint16_t selection = lv_dropdown_get_selected(b.thermostatModeControl);
  if (selection >= b.listCount) return;
  b.selectedIndex = (int8_t)selection;
  ui.noteActivity();
  ui._server.IndexSetList(b.listIds[selection]);
  ui._server.IndexSetEvent(b.listIds[selection]);
  if (b.thermostatModeBadge)
    lv_label_set_text(b.thermostatModeBadge, b.listLabels[selection].c_str());
  if (ui._mqtt && ui._mqtt->enabled())
    ui._mqtt->publishAction(b.name + "__mode", b.listLabels[selection]);
}

void MiraPanelUI::onList(lv_event_t* e) {
  EventContext* ctx = static_cast<EventContext*>(lv_event_get_user_data(e));
  if (!ctx || !ctx->ui) return;
  MiraPanelUI& ui = *ctx->ui;
  if (ui._suppressEvents || ctx->bindingIndex >= ui._bindingCount) return;
  Binding& b = ui._bindings[ctx->bindingIndex];
  uint16_t sel = lv_dropdown_get_selected(lv_event_get_target(e));
  if (sel >= b.listCount) return;
  b.selectedIndex = (int8_t)sel;
  int id = b.listIds[sel];
  ui.noteActivity();
  ui._server.IndexSetList(id);
  ui._server.IndexSetEvent(id);
  ui.publishMqttAction(b, b.listLabels[sel]);
}

void MiraPanelUI::onListButton(lv_event_t* e) {
  EventContext* ctx = static_cast<EventContext*>(lv_event_get_user_data(e));
  if (!ctx || !ctx->ui) return;
  MiraPanelUI& ui = *ctx->ui;
  if (ui._suppressEvents || ctx->bindingIndex >= ui._bindingCount) return;
  Binding& b = ui._bindings[ctx->bindingIndex];
  if (ctx->itemIndex < 0 || ctx->itemIndex >= b.listCount) return;
  b.selectedIndex = ctx->itemIndex;
  int id = b.listIds[(uint8_t)ctx->itemIndex];
  ui.noteActivity();
  ui._server.IndexSetList(id);
  ui._server.IndexSetEvent(id);
  ui.publishMqttAction(b, b.listLabels[(uint8_t)ctx->itemIndex]);
  ui.updateListVisual(b);
}

void MiraPanelUI::onColor(lv_event_t* e) {
  EventContext* ctx = static_cast<EventContext*>(lv_event_get_user_data(e));
  if (!ctx || !ctx->ui) return;
  MiraPanelUI& ui = *ctx->ui;
  if (ui._suppressEvents || ctx->bindingIndex >= ui._bindingCount) return;

  lv_event_code_t code = lv_event_get_code(e);
  if (code != LV_EVENT_VALUE_CHANGED && code != LV_EVENT_PRESSING && code != LV_EVENT_RELEASED) return;

  Binding& b = ui._bindings[ctx->bindingIndex];
  lv_color_t c = lv_colorwheel_get_rgb(lv_event_get_target(e));
  uint32_t rgb = lv_color_to32(c);
  int r  = (rgb >> 16) & 0xFF;
  int g  = (rgb >> 8)  & 0xFF;
  int bl = rgb & 0xFF;
  const uint32_t rgb24 = ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)bl;
  ui.noteActivity();

  const uint32_t now = millis();
  const bool finalSend = code == LV_EVENT_RELEASED;
  if (code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING || code == LV_EVENT_VALUE_CHANGED) {
    b.localInputUntilMs = now + 250;
  }
  if (finalSend) b.localInputUntilMs = now + 350;

  const bool liveSend = (code == LV_EVENT_VALUE_CHANGED || code == LV_EVENT_PRESSING) &&
                        rgb24 != b.lastLiveColor;
  char hex[8];
  snprintf(hex, sizeof(hex), "#%02X%02X%02X", r, g, bl);
  if (liveSend) {
    b.lastLiveSendMs = now;
    b.lastLiveColor = rgb24;
    ui._server.IndexSetColor(b.id, r, g, bl);
    ui._server.IndexSetEvent(b.id);
    ui.publishMqttAction(b, String(hex));
  }
  if (finalSend) {
    b.lastLiveSendMs = now;
    b.lastLiveColor = rgb24;
    ui._server.IndexSetColor(b.id, r, g, bl);
    ui._server.IndexSetEvent(b.id);
    ui.publishMqttAction(b, String(hex));
  }
}

void MiraPanelUI::onBrightness(lv_event_t* e) {
  MiraPanelUI* ui = static_cast<MiraPanelUI*>(lv_event_get_user_data(e));
  if (!ui) return;
  if (ui->_syncingDisplayControls) return;
  int value = lv_slider_get_value(lv_event_get_target(e));
  value = clampInt(value, 30, 255);
  ui->_brightness = (uint8_t)value;
  uint8_t pct = brightnessToPercent(ui->_brightness);
  ui->_manualBrightnessPct = pct;
  ui->_manualBrightnessOverride = true;
  ui->_display.setBrightness(ui->_brightness);
  if (ui->_brightnessValue) lv_label_set_text_fmt(ui->_brightnessValue, "%u%%", pct);
  ui->noteActivity();
}

void MiraPanelUI::onTheme(lv_event_t* e) {
  MiraPanelUI* ui = static_cast<MiraPanelUI*>(lv_event_get_user_data(e));
  if (!ui || ui->_suppressEvents) return;
  uint16_t selected = lv_dropdown_get_selected(lv_event_get_target(e));
  if (selected > THEME_GLASS) selected = THEME_MIRA;
  ui->_theme = (uint8_t)selected;
  ui->_prefs.putUChar("theme", ui->_theme);
  ui->applyTheme();
  ui->noteActivity();
}

void MiraPanelUI::onReturnHome(lv_event_t* e) {
  MiraPanelUI* ui = static_cast<MiraPanelUI*>(lv_event_get_user_data(e));
  if (!ui) return;
  ui->_returnHomeEnabled = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
  ui->_prefs.putBool("home", ui->_returnHomeEnabled);
  ui->noteActivity();
}

void MiraPanelUI::onSleepMode(lv_event_t* e) {
  MiraPanelUI* ui = static_cast<MiraPanelUI*>(lv_event_get_user_data(e));
  if (!ui) return;
  ui->_sleepEnabled = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
  ui->_prefs.putBool("sleep", ui->_sleepEnabled);
  if (!ui->_sleepEnabled) ui->wakeFromSleep();
  ui->noteActivity();
}

void MiraPanelUI::onClockSleep(lv_event_t* e) {
  MiraPanelUI* ui = static_cast<MiraPanelUI*>(lv_event_get_user_data(e));
  if (!ui) return;
  ui->_clockInSleep = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
  ui->_prefs.putBool("clock", ui->_clockInSleep);
  ui->noteActivity();
}

void MiraPanelUI::updateListVisual(Binding& b) {
  if (b.type == W_LIST && b.control) {
    _suppressEvents = true;
    lv_dropdown_set_selected(b.control, b.selectedIndex < 0 ? 0 : b.selectedIndex);
    _suppressEvents = false;
  }
  else if (b.type == W_LISTBUTTON) {
    for (uint8_t i = 0; i < b.listCount; ++i) {
      if (!b.listButtons[i]) continue;
      const bool miraSelected = i == (uint8_t)b.selectedIndex;
    lv_obj_set_style_bg_color(b.listButtons[i], miraSelected ? colorAccent2() : colorPanel2(), 0);
    lv_obj_set_style_border_color(b.listButtons[i], colorAccent(), 0);
    lv_obj_set_style_border_width(b.listButtons[i], miraSelected ? 1 : 0, 0);
    if (miraSelected) lv_obj_fade_in(b.listButtons[i], 90, 0);
    }
  }
}

void MiraPanelUI::handleMiraEvent(int eventId) {
  if (eventId < 0) return;
  for (uint8_t i = 0; i < _bindingCount; ++i) updateBindingFromServer(i, eventId);
}

void MiraPanelUI::handleParamEvent(int paramEvent) {
  if (paramEvent == MIRA_SAVE_LIST_LVGL) {
    // Le POST /savelistlvgl vient de terminer. On laisse quelques millisecondes
    // a la reponse HTTP pour partir avant de reconstruire toute l'interface.
    _reloadPending = true;
    _reloadAt = millis() + 120;
    Serial.println("[MIRA][UI] Sauvegarde LVGL detectee, reconstruction programmee");
  }
}

void MiraPanelUI::updateBindingFromServer(uint8_t idx, int eventId) {
  if (idx >= _bindingCount) return;
  Binding& localBinding = _bindings[idx];
  if (localBinding.localInputUntilMs != 0 && (int32_t)(millis() - localBinding.localInputUntilMs) < 0) return;

  Binding& b = _bindings[idx];

  if (b.type == W_THERMOSTAT) {
    _suppressEvents = true;
    if (eventId == b.thermostatTempId) {
      String value = _server.IndexGetInfo(b.thermostatTempId);
      value.trim();
      if (value.isEmpty()) value = "--";
      if (value.indexOf("°") < 0) value += " °C";
      if (b.thermostatTemperatureLabel)
        lv_label_set_text(b.thermostatTemperatureLabel, value.c_str());
    } else if (eventId == b.id) {
      float value = _server.IndexGetRange(b.id);
      if (value < b.minValue) value = b.minValue;
      if (value > b.maxValue) value = b.maxValue;
      value = roundf(value * 2.0f) * 0.5f;
      b.thermostatSetpoint = value;
      b.currentValue = (int)roundf(value);
      if (b.control) lv_arc_set_value(b.control, (int)roundf(value * 2.0f));
      if (b.valueLabel)
        lv_label_set_text(b.valueLabel, (String(value, 1) + "°").c_str());
    } else {
      for (uint8_t i = 0; i < b.listCount; ++i) {
        if (eventId == b.listIds[i]) {
          b.selectedIndex = (int8_t)i;
          if (b.thermostatModeControl) lv_dropdown_set_selected(b.thermostatModeControl, i);
          if (b.thermostatModeBadge) lv_label_set_text(b.thermostatModeBadge, b.listLabels[i].c_str());
          break;
        }
      }
    }
    _suppressEvents = false;
    return;
  }

  if (b.type == W_INFO && eventId == b.id) {
    String value = _server.IndexGetInfo(b.id);
    String txt = value + (b.unit.length() ? " " + b.unit : "");
    if (b.valueLabel) lv_label_set_text(b.valueLabel, txt.c_str());
    miraMarkInfoArrival(b.infoLastSeenLabel);
    if (b.infoIconRoot) miraFitInfoValue(b.valueLabel, b.infoIconRoot);
  }
  else if (b.type == W_TOGGLE && (eventId == b.idOff || eventId == b.idOn)) {
    bool on = (eventId == b.idOn);
    _suppressEvents = true;
    _server.IndexSetToggle(eventId);
    if (b.control) {
      if (on) lv_obj_add_state(b.control, LV_STATE_CHECKED);
      else lv_obj_clear_state(b.control, LV_STATE_CHECKED);
    }
    if (b.stateLabel) {
      lv_label_set_text(b.stateLabel, on ? "ON" : "OFF");
      lv_obj_set_style_text_color(b.stateLabel, on ? COL_GREEN : COL_MUTED, 0);
    }
    _suppressEvents = false;
  }
  else if ((b.type == W_RANGE || b.type == W_ROUNDRANGE) && eventId == b.id) {
    float serverValue = _server.IndexGetRange(b.id);
    if (serverValue < b.minValue) serverValue = b.minValue;
    if (serverValue > b.maxValue) serverValue = b.maxValue;
    _suppressEvents = true;
    if (b.type == W_RANGE) {
      int value = (int)roundf(serverValue);
      b.currentValue = value;
      if (b.control) lv_slider_set_value(b.control, value, LV_ANIM_OFF);
      if (b.valueLabel) lv_label_set_text_fmt(b.valueLabel, "%d%s%s", value, b.unit.length() ? " " : "", b.unit.c_str());
    } else {
      int rawValue = (int)roundf(serverValue * 2.0f);
      b.currentValue = (int)roundf(serverValue);
      if (b.control) lv_arc_set_value(b.control, rawValue);
      if (b.valueLabel) {
        String txt = String(rawValue * 0.5f, 1);
        if (b.unit.length()) txt += " " + b.unit;
        lv_label_set_text(b.valueLabel, txt.c_str());
      }
    }
    _suppressEvents = false;
  }
  else if ((b.type == W_LIST || b.type == W_LISTBUTTON)) {
    for (uint8_t n = 0; n < b.listCount; ++n) {
      if (eventId == b.listIds[n]) {
        b.selectedIndex = n;
        updateListVisual(b);
        break;
      }
    }
  }
  else if (b.type == W_COLOR && eventId == b.id) {
    int r = _server.IndexGetColor_R(b.id);
    int g = _server.IndexGetColor_G(b.id);
    int bl = _server.IndexGetColor_B(b.id);
    _suppressEvents = true;
    if (b.control) lv_colorwheel_set_rgb(b.control, lv_color_make(r, g, bl));
    _suppressEvents = false;
  }
}


uint8_t MiraPanelUI::percentToBrightness(uint8_t percent) {
  if (percent == 0) return 0;
  if (percent > 100) percent = 100;
  return (uint8_t)(30 + ((uint16_t)(percent - 1) * 225U) / 99U);
}

uint8_t MiraPanelUI::brightnessToPercent(uint8_t raw) {
  if (raw == 0) return 0;
  if (raw <= 30) return 1;
  return (uint8_t)(1 + ((uint16_t)(raw - 30) * 99U + 112U) / 225U);
}

int MiraPanelUI::parseClockMinutes(const String& value) {
  if (value.length() != 5 || value[2] != ':') return -1;
  if (!isDigit(value[0]) || !isDigit(value[1]) || !isDigit(value[3]) || !isDigit(value[4])) return -1;
  int h = value.substring(0, 2).toInt();
  int m = value.substring(3, 5).toInt();
  if (h < 0 || h > 23 || m < 0 || m > 59) return -1;
  return h * 60 + m;
}

String MiraPanelUI::normalizeClock(const String& value, const String& fallback) {
  String v = value;
  v.trim();
  v.replace("%3A", ":");
  v.replace("%3a", ":");
  if (v.length() == 4 && v.indexOf(':') < 0) v = v.substring(0, 2) + ":" + v.substring(2);
  return parseClockMinutes(v) >= 0 ? v : fallback;
}

bool MiraPanelUI::parseBoolPayload(const String& value, bool fallback) {
  String v = value;
  v.trim();
  v.toLowerCase();
  if (v == "1" || v == "on" || v == "true" || v == "oui") return true;
  if (v == "0" || v == "off" || v == "false" || v == "non") return false;
  return fallback;
}

uint8_t MiraPanelUI::displayActiveBrightness() const {
  if (_sleeping) return _clockInSleep ? _sleepBrightnessPct : 0;
  if (_manualBrightnessOverride) return _manualBrightnessPct;
  return (_nightScheduleEnabled && _nightActive) ? _nightBrightnessPct : _dayBrightnessPct;
}

void MiraPanelUI::refreshDisplayConfig(bool force) {
  const uint32_t now = millis();
  if (!force && (uint32_t)(now - _lastActivity) < 800UL) return;
  if (!force && (uint32_t)(now - _lastDisplayConfigCheck) < 1000UL) return;
  _lastDisplayConfigCheck = now;

  String dayText = _server.GetParamText("DISPLAY_Jour");
  String nightText = _server.GetParamText("DISPLAY_Nuit");
  String sleepText = _server.GetParamText("DISPLAY_Veille");
  String timeoutText = _server.GetParamText("DISPLAY_Delai");
  String startText = normalizeClock(_server.GetParamText("DISPLAY_DebutNuit"), "23:00");
  String endText = normalizeClock(_server.GetParamText("DISPLAY_FinNuit"), "07:00");
  bool nightAuto = _server.GetParamBool("DISPLAY_NuitAuto");
  bool clockSleep = _server.GetParamBool("DISPLAY_HorlogeVeille");

  int day = dayText.toInt();
  int night = nightText.toInt();
  int sleep = sleepText.toInt();
  long timeout = timeoutText.toInt();
  day = clampInt(day, 1, 100);
  night = clampInt(night, 1, 100);
  sleep = clampInt(sleep, 0, 100);
  if (timeout < 0) timeout = 0;
  if (timeout > 86400) timeout = 86400;

  String signature = String(day) + "|" + String(night) + "|" + String(sleep) + "|" + String(timeout) + "|" +
                     (nightAuto ? "1" : "0") + "|" + startText + "|" + endText + "|" + (clockSleep ? "1" : "0");
  if (!force && signature == _displayConfigSignature) return;

  bool changedAfterStartup = _displayConfigSignature.length() && signature != _displayConfigSignature;
  _displayConfigSignature = signature;
  _dayBrightnessPct = (uint8_t)day;
  _nightBrightnessPct = (uint8_t)night;
  _sleepBrightnessPct = (uint8_t)sleep;
  _sleepTimeoutSec = (uint32_t)timeout;
  _sleepEnabled = _sleepTimeoutSec > 0;
  _nightScheduleEnabled = nightAuto;
  _nightStart = startText;
  _nightEnd = endText;
  _clockInSleep = clockSleep;
  if (changedAfterStartup) _manualBrightnessOverride = false;

  _syncingDisplayControls = true;
  if (_sleepSwitch) {
    if (_sleepEnabled) lv_obj_add_state(_sleepSwitch, LV_STATE_CHECKED);
    else lv_obj_clear_state(_sleepSwitch, LV_STATE_CHECKED);
  }
  if (_clockSleepSwitch) {
    if (_clockInSleep) lv_obj_add_state(_clockSleepSwitch, LV_STATE_CHECKED);
    else lv_obj_clear_state(_clockSleepSwitch, LV_STATE_CHECKED);
  }
  _syncingDisplayControls = false;

  updateDisplaySchedule(true);
  if (_sleeping) applySleepBrightness();
  else applyAwakeBrightness();
  Serial.printf("[MIRA][DISPLAY] Config jour=%u%% nuit=%u%% veille=%u%% delai=%lus nuit_auto=%s %s-%s\n",
                _dayBrightnessPct, _nightBrightnessPct, _sleepBrightnessPct,
                (unsigned long)_sleepTimeoutSec, _nightScheduleEnabled ? "oui" : "non",
                _nightStart.c_str(), _nightEnd.c_str());
}

void MiraPanelUI::updateDisplaySchedule(bool force) {
  if (!_nightScheduleEnabled) {
    if (_nightActive || force) {
      bool wasNight = _nightActive;
      _nightActive = false;
      if (wasNight) _manualBrightnessOverride = false;
      if (!_sleeping) applyAwakeBrightness();
    }
    return;
  }

  const uint32_t now = millis();
  if (!force && (uint32_t)(now - _lastScheduleCheckMs) < 1000UL) return;
  _lastScheduleCheckMs = now;

  time_t epoch = time(nullptr);
  if (epoch < 1700000000) return;
  struct tm t;
  localtime_r(&epoch, &t);
  int minute = t.tm_hour * 60 + t.tm_min;
  if (!force && minute == _lastScheduleMinute) return;
  _lastScheduleMinute = minute;

  int start = parseClockMinutes(_nightStart);
  int end = parseClockMinutes(_nightEnd);
  if (start < 0 || end < 0 || start == end) return;
  bool night = (start < end) ? (minute >= start && minute < end)
                             : (minute >= start || minute < end);
  if (force || night != _nightActive) {
    bool changed = night != _nightActive;
    _nightActive = night;
    if (changed) _manualBrightnessOverride = false;
    if (!_sleeping) applyAwakeBrightness();
    if (changed) Serial.printf("[MIRA][DISPLAY] %02d:%02d -> mode %s (%s-%s)\n",
                               t.tm_hour, t.tm_min, night ? "nuit" : "jour",
                               _nightStart.c_str(), _nightEnd.c_str());
  }
}

void MiraPanelUI::applyAwakeBrightness() {
  uint8_t pct = displayActiveBrightness();
  _brightness = percentToBrightness(pct);
  _display.setBrightness(_brightness);
  if (_brightnessSlider && lv_slider_get_value(_brightnessSlider) != _brightness) {
    _syncingDisplayControls = true;
    lv_slider_set_value(_brightnessSlider, _brightness, LV_ANIM_OFF);
    _syncingDisplayControls = false;
  }
  if (_brightnessValue) lv_label_set_text_fmt(_brightnessValue, "%u%%", pct);
}

void MiraPanelUI::applySleepBrightness() {
  if (!_sleeping) return;
  if (!_clockInSleep) {
    _display.setBrightness(0);
    return;
  }
  _display.setBrightness(percentToBrightness(_sleepBrightnessPct));
}

void MiraPanelUI::handleMqttDisplayCommand(const String& key, const String& payload) {
  String k = key;
  String p = payload;
  k.trim(); k.toLowerCase();
  p.trim();

  if (k == "wake") {
    if (parseBoolPayload(p, p == "1")) wakeFromSleep();
    return;
  }
  if (k == "sleep") {
    if (parseBoolPayload(p, p == "1")) enterSleep();
    return;
  }
  if (k == "brightness") {
    String lower = p; lower.toLowerCase();
    if (lower == "auto") {
      _manualBrightnessOverride = false;
      updateDisplaySchedule(true);
      if (!_sleeping) applyAwakeBrightness();
      return;
    }
    int pct = clampInt(p.toInt(), 0, 100);
    _manualBrightnessPct = (uint8_t)pct;
    _manualBrightnessOverride = true;
    if (!_sleeping) applyAwakeBrightness();
    return;
  }

  if (k == "day") {
    int v = clampInt(p.toInt(), 1, 100);
    _server.UpdateParamText("DISPLAY_Jour", String(v));
  } else if (k == "night") {
    int v = clampInt(p.toInt(), 1, 100);
    _server.UpdateParamText("DISPLAY_Nuit", String(v));
  } else if (k == "sleep_brightness") {
    int v = clampInt(p.toInt(), 0, 100);
    _server.UpdateParamText("DISPLAY_Veille", String(v));
  } else if (k == "sleep_timeout") {
    long v = p.toInt(); if (v < 0) v = 0; if (v > 86400) v = 86400;
    _server.UpdateParamText("DISPLAY_Delai", String(v));
  } else if (k == "night_auto") {
    _server.UpdateParamBool("DISPLAY_NuitAuto", parseBoolPayload(p, _nightScheduleEnabled));
  } else if (k == "night_start") {
    String v = normalizeClock(p, _nightStart);
    String compact = v.substring(0, 2) + v.substring(3, 5);
    _server.UpdateParamText("DISPLAY_DebutNuit", compact);
  } else if (k == "night_end") {
    String v = normalizeClock(p, _nightEnd);
    String compact = v.substring(0, 2) + v.substring(3, 5);
    _server.UpdateParamText("DISPLAY_FinNuit", compact);
  } else if (k == "clock_sleep") {
    _server.UpdateParamBool("DISPLAY_HorlogeVeille", parseBoolPayload(p, _clockInSleep));
  } else {
    return;
  }
  refreshDisplayConfig(true);
}

void MiraPanelUI::startNtpIfPossible() {
  if (_ntpStarted || WiFi.status() != WL_CONNECTED) return;
  configTzTime("CET-1CEST,M3.5.0,M10.5.0/3", "pool.ntp.org", "time.nist.gov");
  _ntpStarted = true;
}

void MiraPanelUI::updateClock() {
  if (!_dateTimeLabel) return;
  if (millis() - _lastClockUpdate < 1000) return;
  _lastClockUpdate = millis();

  struct tm t;
  if (_clockStatusLabel) {
    const String wifi = WiFi.status() == WL_CONNECTED
      ? String("Wi-Fi  ") + String(WiFi.RSSI()) + " dBm"
      : String("Wi-Fi déconnecté");
    const String value = String("MIRA PANEL\n") + wifi + "\n" + (_nightActive ? "Mode nuit" : "Mode jour");
    if (value != _clockStatusCache) {
      _clockStatusCache = value;
      lv_label_set_text(_clockStatusLabel, value.c_str());
    }
  }
  if (!getLocalTime(&t, 10)) return;
  char timeBuf[8];
  strftime(timeBuf, sizeof(timeBuf), "%H:%M", &t);
  if (_dateTimeLabel) lv_label_set_text(_dateTimeLabel, timeBuf);

  if (_dateSubLabel) {
    static const char* jours[] = {"Dimanche", "Lundi", "Mardi", "Mercredi", "Jeudi", "Vendredi", "Samedi"};
    static const char* mois[] = {"Janvier", "Février", "Mars", "Avril", "Mai", "Juin", "Juillet", "Août", "Septembre", "Octobre", "Novembre", "Décembre"};
    char dateBuf[64];
    snprintf(dateBuf, sizeof(dateBuf), "%s, %02d %s %04d", jours[t.tm_wday], t.tm_mday, mois[t.tm_mon], t.tm_year + 1900);
    lv_label_set_text(_dateSubLabel, dateBuf);
  }
}

void MiraPanelUI::noteActivity() {
  _lastActivity = millis();
  if (_sleeping) wakeFromSleep();
}

void MiraPanelUI::enterSleep() {
  if (_sleeping) return;
  _sleeping = true;
  if (_clockInSleep) {
    applySleepBrightness();
    if (_sleepOverlay) lv_obj_clear_flag(_sleepOverlay, LV_OBJ_FLAG_HIDDEN);
    updateClock();
  } else {
    _display.setBrightness(0);
  }
  Serial.println("[MIRA][DISPLAY] Veille active");
}

void MiraPanelUI::wakeFromSleep() {
  if (!_sleeping) return;
  _sleeping = false;
  if (_sleepOverlay) lv_obj_add_flag(_sleepOverlay, LV_OBJ_FLAG_HIDDEN);
  applyAwakeBrightness();
  _lastActivity = millis();
  Serial.println("[MIRA][DISPLAY] Reveil");
}

void MiraPanelUI::updateSleepClock() {
  if (!_sleeping || !_clockInSleep || !_sleepClock) return;

  const uint32_t now = millis();

  // Mise a jour de l'heure chaque seconde. La date n'est volontairement
  // plus affichee : la veille officielle montre l'heure comme element dominant.
  if (now - _lastSleepClockUpdate >= 1000 || _lastSleepClockUpdate == 0) {
    _lastSleepClockUpdate = now;
    struct tm t;
    if (getLocalTime(&t, 10)) {
      char buf[8];
      strftime(buf, sizeof(buf), "%H:%M", &t);
      lv_label_set_text(_sleepClock, buf);
    }
  }

  // Anti-burn : toutes les 60 s, l'heure se decale de quelques pixels
  // autour du centre. Les neuf positions evitent de marquer toujours la meme zone.
  if (now - _lastSleepClockMove >= 60000UL) {
    static const int8_t offsetX[9] = {-12, 0, 12, -12, 0, 12, -12, 0, 12};
    static const int8_t offsetY[9] = { -8,-8, -8,   0, 0,  0,   8, 8,  8};

    _lastSleepClockMove = now;
    _sleepClockPosition = (_sleepClockPosition + 1) % 9;
    lv_obj_align(_sleepClock, LV_ALIGN_CENTER,
                 offsetX[_sleepClockPosition], -48 + offsetY[_sleepClockPosition]);
  }
}

void MiraPanelUI::loop() {
#if defined(MIRA_TARGET_WAVESHARE_43C)
  // Lower lock contention with the dedicated Waveshare LVGL task.
  // Clock/configuration maintenance does not need a 2 ms interval.
  static uint32_t lastMaintenanceMs = 0;
  const uint32_t now = millis();
  if ((uint32_t)(now - lastMaintenanceMs) < 40UL) return;
  if (!_display.miraLvglLock(5)) return;
  lastMaintenanceMs = now;
#endif

  pollFavorites();
  // Navigation is serviced by LVGL's 10 ms onNavigationTimer.

  startNtpIfPossible();
  updateClock();

  if (_reloadPending && (int32_t)(millis() - _reloadAt) >= 0) {
    _reloadPending = false;
    reloadFromMiraJson();
  }

  refreshDisplayConfig(false);
  updateDisplaySchedule(false);
  uint32_t idle = millis() - _lastActivity;
  if (!_sleeping && _returnHomeEnabled && _currentPage != 0 && idle >= 60000UL) {
    showPage(0);
    // showPage() remet l'activite a zero, donc la veille repart ensuite proprement.
  }

  idle = millis() - _lastActivity;
  if (!_sleeping && _sleepEnabled && _sleepTimeoutSec > 0 && idle >= _sleepTimeoutSec * 1000UL) enterSleep();
  if (_sleeping && _clockInSleep) updateSleepClock();

#if defined(MIRA_TARGET_WAVESHARE_43C)
  // The official Waveshare lvgl task owns lv_timer_handler().
  _display.miraLvglUnlock();
#else
  lv_timer_handler();
#endif
}
