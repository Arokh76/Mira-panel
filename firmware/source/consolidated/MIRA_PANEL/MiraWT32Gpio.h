#pragma once
// WT32-SC01 Plus EXT (8 pins): 5V, GND, GPIO10,11,12,13,14,21.
// No Jeedom/MQTT coupling. Modes are persisted, output levels ARE NOT.
// A configured output always starts at logical OFF on every boot.
#include <Arduino.h>
#include <Preferences.h>
#include <ArduinoJson.h>

namespace MiraWT32GPIO {
static constexpr uint8_t COUNT = 6;
static constexpr uint8_t pins[COUNT] = {10, 11, 12, 13, 14, 21};
static uint8_t mode[COUNT] = {0};  // 0=off, 1=input, 2=pull-up, 3=pull-down, 4=output
static bool inverted[COUNT] = {false};
static bool initialized = false;

static int findPin(int gpio) {
  for (uint8_t i = 0; i < COUNT; ++i) if (pins[i] == gpio) return i;
  return -1;
}
static const char* modeName(uint8_t m) {
  switch (m) {
    case 1: return "input";
    case 2: return "pullup";
    case 3: return "pulldown";
    case 4: return "output";
    default: return "off";
  }
}
static int parseMode(const String& m) {
  if (m == "off") return 0;
  if (m == "input") return 1;
  if (m == "pullup") return 2;
  if (m == "pulldown") return 3;
  if (m == "output") return 4;
  return -1;
}
static void apply(uint8_t i) {
  if (i >= COUNT) return;
  const uint8_t pin = pins[i];
  // First set outputs to logical OFF; never restore HIGH/ON from NVS.
  if (mode[i] == 4) {
    digitalWrite(pin, inverted[i] ? HIGH : LOW);
    pinMode(pin, OUTPUT);
  } else {
    switch (mode[i]) {
      case 2: pinMode(pin, INPUT_PULLUP); break;
      case 3: pinMode(pin, INPUT_PULLDOWN); break;
      default: pinMode(pin, INPUT); break;
    }
  }
}
static void begin() {
  if (initialized) return;
  // Do not drive any EXT GPIO until explicitly configured by the user.
  for (uint8_t i = 0; i < COUNT; ++i) pinMode(pins[i], INPUT);
  Preferences prefs;
  if (prefs.begin("miragpio", true)) {
    for (uint8_t i = 0; i < COUNT; ++i) {
      char mk[8], ik[8];
      snprintf(mk, sizeof(mk), "mode%u", unsigned(i));
      snprintf(ik, sizeof(ik), "inv%u", unsigned(i));
      const uint8_t m = prefs.getUChar(mk, 0);
      mode[i] = m <= 4 ? m : 0;
      inverted[i] = prefs.getBool(ik, false);
    }
    prefs.end();
  }
  for (uint8_t i = 0; i < COUNT; ++i) apply(i);
  initialized = true;
  Serial.println("[MIRA][GPIO] WT32 EXT initialized (outputs default OFF)");
}
static bool configure(uint8_t i, uint8_t newMode, bool newInverted) {
  if (i >= COUNT || newMode > 4) return false;
  Preferences prefs;
  if (!prefs.begin("miragpio", false)) return false;
  char mk[8], ik[8];
  snprintf(mk, sizeof(mk), "mode%u", unsigned(i));
  snprintf(ik, sizeof(ik), "inv%u", unsigned(i));
  const bool saved = prefs.putUChar(mk, newMode) == 1 &&
                     prefs.putBool(ik, newInverted) == 1;
  prefs.end();
  if (!saved) return false;
  // Remove any old output drive before reconfiguring.
  if (mode[i] == 4) digitalWrite(pins[i], inverted[i] ? HIGH : LOW);
  pinMode(pins[i], INPUT);
  mode[i] = newMode;
  inverted[i] = newInverted;
  apply(i);
  return true;
}
static bool write(uint8_t i, bool on) {
  if (i >= COUNT || mode[i] != 4) return false;
  digitalWrite(pins[i], (on != inverted[i]) ? HIGH : LOW);
  return true;
}
static String stateJson() {
  StaticJsonDocument<1024> root;
  JsonArray arr = root.createNestedArray("pins");
  for (uint8_t i = 0; i < COUNT; ++i) {
    JsonObject p = arr.createNestedObject();
    p["gpio"] = pins[i];
    p["connector"] = String("EXT_IO") + (i + 1);
    p["mode"] = modeName(mode[i]);
    p["inverted"] = inverted[i];
    p["state"] = (mode[i] ? ((digitalRead(pins[i]) == HIGH) != inverted[i]) : false);
  }
  String out;
  serializeJson(root, out);
  return out;
}
}  // namespace MiraWT32GPIO
