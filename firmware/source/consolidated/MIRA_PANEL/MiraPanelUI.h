#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <lvgl.h>
#include "MiraPanelServer.h"
#include "MiraHardware.h"
#include "MiraDisplayProfile.h"

class MiraMqtt;

class MiraPanelUI {
public:
  MiraPanelUI(MiraPanelServer& server, MiraDisplay& display);

  void begin();
  void buildFromMiraJson();
  void loop();
  void handleMiraEvent(int eventId);
  void handleParamEvent(int paramEvent);
  void setMqtt(MiraMqtt* mqtt);
  void handleMqttState(const String& topic, const String& payload);

  // Mira 0.6.0 - affichage autonome + commandes systeme MQTT.
  void handleMqttDisplayCommand(const String& key, const String& payload);
  uint8_t displayDayBrightness() const { return _dayBrightnessPct; }
  uint8_t displayNightBrightness() const { return _nightBrightnessPct; }
  uint8_t displaySleepBrightness() const { return _sleepBrightnessPct; }
  uint32_t displaySleepTimeout() const { return _sleepTimeoutSec; }
  bool displayNightAuto() const { return _nightScheduleEnabled; }
  String displayNightStart() const { return _nightStart; }
  String displayNightEnd() const { return _nightEnd; }
  bool displayClockInSleep() const { return _clockInSleep; }
  bool displaySleeping() const { return _sleeping; }
  bool displayNightActive() const { return _nightActive; }
  uint8_t displayActiveBrightness() const;

private:
  static constexpr uint8_t MAX_BINDINGS = 40;
  static constexpr uint8_t MAX_LIST_ITEMS = 12;
  static constexpr uint8_t MAX_EVENT_CONTEXTS = 96;
  static constexpr uint8_t PAGE_COUNT = 5;  // 4 existing pages + demo Énergie
  static constexpr uint8_t FAVORITE_SLOTS = 3;
  static constexpr uint8_t NAV_BUTTON_COUNT = FAVORITE_SLOTS + 1;  // + Pages
  static constexpr uint8_t NO_FAVORITE = 0xFF;

  enum ThemeMode : uint8_t {
    THEME_MIRA = 0,
    THEME_DARK = 1,
    THEME_GLASS = 2
  };

  enum WidgetType : uint8_t {
    W_INFO,
    W_BUTTON,
    W_TOGGLE,
    W_RANGE,
    W_ROUNDRANGE,
    W_LIST,
    W_LISTBUTTON,
    W_COLOR,
    W_DATETIME,
    W_THERMOSTAT,
    W_UNSUPPORTED
  };

  struct Binding {
    WidgetType type = W_UNSUPPORTED;
    String name;
    String unit;
    String location;
    String style = "standard";
    String layout = "auto";
    String labelMode = "auto";
    String icon = "auto";
    String displayName = "";
    int layoutX = -1;
    int layoutY = -1;
    int layoutW = -1;
    int layoutH = -1;
    uint32_t lastLiveSendMs = 0;
    int lastLiveValue = -32768;
    uint32_t lastLiveColor = 0xFFFFFFFFUL;
    uint32_t localInputUntilMs = 0;

    int id = -1;
    int idOff = -1;
    int idOn = -1;
    int thermostatTempId = -1;
    float thermostatSetpoint = 20.0f;
    lv_obj_t* thermostatTemperatureLabel = nullptr;
    lv_obj_t* thermostatModeControl = nullptr;
    lv_obj_t* thermostatModeBadge = nullptr;

    int minValue = 0;
    int maxValue = 100;
    int currentValue = 0;

    uint8_t listCount = 0;
    int listIds[MAX_LIST_ITEMS] = {0};
    String listLabels[MAX_LIST_ITEMS];
    lv_obj_t* listButtons[MAX_LIST_ITEMS] = {nullptr};
    int8_t selectedIndex = 0;

    lv_obj_t* card = nullptr;
    lv_obj_t* control = nullptr;
    lv_obj_t* valueLabel = nullptr;
    lv_obj_t* infoNameLabel = nullptr;
    lv_obj_t* infoIconRoot = nullptr;
    lv_obj_t* infoLastSeenLabel = nullptr;
    lv_obj_t* stateLabel = nullptr;
  };

  struct EventContext {
    MiraPanelUI* ui = nullptr;
    uint8_t bindingIndex = 0;
    int8_t itemIndex = -1;
  };

  MiraPanelServer& _server;
  MiraDisplay& _display;
  Preferences _prefs;
  MiraMqtt* _mqtt = nullptr;

  Binding _bindings[MAX_BINDINGS];
  uint8_t _bindingCount = 0;
  uint8_t _homeStandardInfoTotal = 0;
  uint8_t _homeStandardInfoCreated = 0;
  EventContext _eventCtx[MAX_EVENT_CONTEXTS];
  uint8_t _eventCtxCount = 0;

  lv_obj_t* _screen = nullptr;
  lv_obj_t* _nav = nullptr;
  lv_obj_t* _pages[PAGE_COUNT] = {nullptr};
  uint8_t _favoritePages[FAVORITE_SLOTS] = {0, 1, 2};  // fixed slots, 0xFF = free
  lv_obj_t* _favoriteButtons[FAVORITE_SLOTS] = {nullptr};
  lv_obj_t* _navPagesButton = nullptr;
  lv_obj_t* _pagePicker = nullptr;
  lv_obj_t* _pagePickerCard = nullptr;
  lv_obj_t* _pagePickerButtons[PAGE_COUNT] = {nullptr};
  uint32_t _lastFavoritesPollMs = 0;
  lv_obj_t* _dateTimeLabel = nullptr;
  lv_obj_t* _dateSubLabel = nullptr;
  lv_obj_t* _clockStatusLabel = nullptr;
  String _clockStatusCache;

  lv_obj_t* _settingsCards[4] = {nullptr, nullptr, nullptr, nullptr};
  lv_obj_t* _brightnessSlider = nullptr;
  lv_obj_t* _brightnessValue = nullptr;
  lv_obj_t* _themeDropdown = nullptr;
  lv_obj_t* _returnHomeSwitch = nullptr;
  lv_obj_t* _sleepSwitch = nullptr;
  lv_obj_t* _clockSleepSwitch = nullptr;

  lv_obj_t* _sleepOverlay = nullptr;
  lv_obj_t* _sleepClock = nullptr;

  bool _suppressEvents = false;
  bool _ntpStarted = false;
  bool _reloadPending = false;
  uint8_t _theme = THEME_DARK;
  bool _returnHomeEnabled = true;
  uint8_t _dayBrightnessPct = 85;
  uint8_t _nightBrightnessPct = 20;
  uint8_t _sleepBrightnessPct = 8;
  uint32_t _sleepTimeoutSec = 120;
  bool _nightScheduleEnabled = false;
  bool _nightActive = false;
  bool _manualBrightnessOverride = false;
  bool _syncingDisplayControls = false;
  uint8_t _manualBrightnessPct = 85;
  String _nightStart = "23:00";
  String _nightEnd = "07:00";
  String _displayConfigSignature;
  uint32_t _lastDisplayConfigCheck = 0;
  int _lastScheduleMinute = -1;
  uint32_t _lastScheduleCheckMs = 0;
  bool _sleepEnabled = true;
  bool _clockInSleep = true;
  bool _sleeping = false;
  uint8_t _brightness = 220;
  uint8_t _currentPage = 0;
  int8_t _pendingNavPage = -1;
  bool _navTouchLatched = false;
  bool _pendingTogglePagePicker = false;
  uint32_t _lastClockUpdate = 0;
  uint32_t _lastSleepClockUpdate = 0;
  uint32_t _lastSleepClockMove = 0;
  uint8_t _sleepClockPosition = 4;
  uint32_t _lastActivity = 0;
  uint32_t _reloadAt = 0;

  static lv_disp_draw_buf_t _drawBuffer;
#if MIRA_RGB_DIRECT_FRAMEBUFFER
  static lv_color_t* _buf1;
  static lv_color_t* _buf2;
#else
  static lv_color_t _buf1[MIRA_DISPLAY_WIDTH * MIRA_DRAW_BUFFER_LINES];
  static lv_color_t _buf2[MIRA_DISPLAY_WIDTH * MIRA_DRAW_BUFFER_LINES];
#endif
  static MiraDisplay* _displayForLvgl;
  static MiraPanelUI* _uiForLvgl;

  static void displayFlush(lv_disp_drv_t* disp, const lv_area_t* area, lv_color_t* color_p);
  static void touchRead(lv_indev_drv_t* indev, lv_indev_data_t* data);

  static void onNavigationTimer(lv_timer_t* timer);
  static void onNav(lv_event_t* e);
  static void onPagePick(lv_event_t* e);
  static void onButton(lv_event_t* e);
  static void onToggle(lv_event_t* e);
  static void onRange(lv_event_t* e);
  static void onRoundRange(lv_event_t* e);
  static void onThermostatRange(lv_event_t* e);
  static void onThermostatMode(lv_event_t* e);
  static void onThermostatDropdownOpen(lv_event_t* e);
  static void onList(lv_event_t* e);
  static void onListButton(lv_event_t* e);
  static void onColor(lv_event_t* e);

  static void onBrightness(lv_event_t* e);
  static void onTheme(lv_event_t* e);
  static void onReturnHome(lv_event_t* e);
  static void onSleepMode(lv_event_t* e);
  static void onClockSleep(lv_event_t* e);

  EventContext* makeContext(uint8_t bindingIndex, int8_t itemIndex = -1);
  uint8_t addBinding();
  uint8_t pageIndex(const String& location) const;
  lv_obj_t* makeCard(uint8_t page, lv_coord_t height = LV_SIZE_CONTENT);
  void applyFreeLayout(uint8_t idx);
  lv_obj_t* makeSettingsCard(lv_coord_t height);
  lv_obj_t* makeTransparentRow(lv_obj_t* parent, lv_coord_t height);
  void makeNameLabel(lv_obj_t* parent, const String& text);
  void showPage(uint8_t index);
  void setNavActive(uint8_t index);
  void loadFavorites();
  void refreshFavoriteNav();
  void pollFavorites();
  void setPagePickerVisible(bool visible);
  void buildPagePicker();
  void buildEnergyPreviewPage();
  void applyTheme();
  void buildSettingsPage();
  void clearDynamicPages();
  void reloadFromMiraJson();

  void registerAndCreate(JsonObject obj);
  void createInfo(uint8_t idx);
  void decorateInfo(uint8_t idx);
  void createButton(uint8_t idx);
  void createToggle(uint8_t idx);
  void createRange(uint8_t idx);
  void createRoundRange(uint8_t idx);
  void createThermostat(uint8_t idx);
  void createList(uint8_t idx, bool asButtons);
  void createColor(uint8_t idx);
  void createDateTime(uint8_t idx);
  void createUnsupported(uint8_t idx, const String& originalType);

  void updateBindingFromServer(uint8_t idx, int eventId);
  void publishMqttAction(const Binding& b, const String& payload);
  int findBindingByMqttTopic(const String& topic) const;
  static String normalizeMqttTopicName(const String& value);
  static String mqttPayloadValue(const String& payload);
  void updateListVisual(Binding& b);
  void updateClock();
  void refreshDisplayConfig(bool force = false);
  void updateDisplaySchedule(bool force = false);
  void applyAwakeBrightness();
  void applySleepBrightness();
  static uint8_t percentToBrightness(uint8_t percent);
  static uint8_t brightnessToPercent(uint8_t raw);
  static int parseClockMinutes(const String& value);
  static bool parseBoolPayload(const String& value, bool fallback = false);
  static String normalizeClock(const String& value, const String& fallback);
  void startNtpIfPossible();
  void noteActivity();
  void enterSleep();
  void wakeFromSleep();
  void updateSleepClock();

  lv_color_t colorBg() const;
  lv_color_t colorPanel() const;
  lv_color_t colorPanel2() const;
  lv_color_t colorBorder() const;
  lv_color_t colorAccent() const;
  lv_color_t colorAccent2() const;
  lv_color_t colorNav() const;
  void styleMainCard(lv_obj_t* card, bool dateTime = false);

  static int clampInt(int value, int minValue, int maxValue);
};
