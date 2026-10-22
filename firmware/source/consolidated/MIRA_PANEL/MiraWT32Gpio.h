#pragma once
// WT32-SC01 Plus EXT (8 pins): 5V, GND, GPIO10,11,12,13,14,21.
// Local-only GPIO controller. Configuration and optional last logical states use NVS.
// Output startup policies: 0=OFF (default), 1=restore, 2=ON.
// GPIO levels during ESP32 reset / early boot are NOT guaranteed by software.
#include <Arduino.h>
#include <Preferences.h>
#include <ArduinoJson.h>

namespace MiraWT32GPIO {
static constexpr uint8_t COUNT = 6;
static constexpr uint8_t pins[COUNT] = {10, 11, 12, 13, 14, 21};
static uint8_t mode[COUNT] = {0};  // 0=off, 1=input, 2=pull-up, 3=pull-down, 4=output
static bool inverted[COUNT] = {false};
static uint8_t bootPolicy[COUNT] = {0};  // 0=OFF, 1=restore last, 2=ON
static bool logicalOn[COUNT] = {false};
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
static const char* bootName(uint8_t policy) {
  switch (policy) {
    case 1: return "restore";
    case 2: return "on";
    default: return "off";
  }
}
static int parseBoot(const String& p) {
  if (p == "off") return 0;
  if (p == "restore") return 1;
  if (p == "on") return 2;
  return -1;
}
static int parseMode(const String& m) {
  if (m == "off") return 0;
  if (m == "input") return 1;
  if (m == "pullup") return 2;
  if (m == "pulldown") return 3;
  if (m == "output") return 4;
  return -1;
}
static void driveOutput(uint8_t i, bool on) {
  digitalWrite(pins[i], (on != inverted[i]) ? HIGH : LOW);
  logicalOn[i] = on;
}
static void apply(uint8_t i) {
  if (i >= COUNT) return;
  if (mode[i] == 4) {
    // Preload the requested output level before switching pin direction.
    driveOutput(i, logicalOn[i]);
    pinMode(pins[i], OUTPUT);
  } else {
    pinMode(pins[i], mode[i] == 2 ? INPUT_PULLUP :
                     (mode[i] == 3 ? INPUT_PULLDOWN : INPUT));
    logicalOn[i] = false;
  }
}
static void begin() {
  if (initialized) return;
  // Avoid driving any EXT GPIO while loading configuration.
  for (uint8_t i = 0; i < COUNT; ++i) pinMode(pins[i], INPUT);
  Preferences prefs;
  if (prefs.begin("miragpio", true)) {
    for (uint8_t i = 0; i < COUNT; ++i) {
      char mk[12], ik[12], bk[12], lk[12];
      snprintf(mk, sizeof(mk), "mode%u", unsigned(i));
      snprintf(ik, sizeof(ik), "inv%u", unsigned(i));
      snprintf(bk, sizeof(bk), "boot%u", unsigned(i));
      snprintf(lk, sizeof(lk), "last%u", unsigned(i));
      const uint8_t m = prefs.getUChar(mk, 0);
      const uint8_t b = prefs.getUChar(bk, 0);
      mode[i] = m <= 4 ? m : 0;
      inverted[i] = prefs.getBool(ik, false);
      bootPolicy[i] = b <= 2 ? b : 0;
      logicalOn[i] = mode[i] == 4 &&
          (bootPolicy[i] == 2 || (bootPolicy[i] == 1 && prefs.getBool(lk, false)));
    }
    prefs.end();
  }
  for (uint8_t i = 0; i < COUNT; ++i) apply(i);
  initialized = true;
  Serial.println("[MIRA][GPIO] WT32 EXT initialized with per-output startup policies");
}
static bool configure(uint8_t i, uint8_t newMode, bool newInverted, uint8_t newBoot) {
  if (i >= COUNT || newMode > 4 || newBoot > 2) return false;
  const bool pinChanged = mode[i] != newMode || inverted[i] != newInverted;
  // Editing the startup policy alone MUST NOT interrupt an energized relay.
  const bool futureOn = !pinChanged && newMode == 4 && logicalOn[i];
  Preferences prefs;
  if (!prefs.begin("miragpio", false)) return false;
  char mk[12], ik[12], bk[12], lk[12];
  snprintf(mk, sizeof(mk), "mode%u", unsigned(i));
  snprintf(ik, sizeof(ik), "inv%u", unsigned(i));
  snprintf(bk, sizeof(bk), "boot%u", unsigned(i));
  snprintf(lk, sizeof(lk), "last%u", unsigned(i));
  bool saved = true;
  if (mode[i] != newMode) saved = (prefs.putUChar(mk, newMode) == 1) && saved;
  if (inverted[i] != newInverted) saved = (prefs.putBool(ik, newInverted) == 1) && saved;
  if (bootPolicy[i] != newBoot) saved = (prefs.putUChar(bk, newBoot) == 1) && saved;
  // When restore is selected, snapshot the current logical state immediately.
  if (newMode == 4 && newBoot == 1)
    saved = (prefs.putBool(lk, futureOn) == 1) && saved;
  prefs.end();
  if (!saved) return false;
  if (pinChanged) {
    // Safely release the previous output before switching mode/polarity.
    if (mode[i] == 4) driveOutput(i, false);
    pinMode(pins[i], INPUT);
  }
  mode[i] = newMode;
  inverted[i] = newInverted;
  bootPolicy[i] = newBoot;
  if (pinChanged) {
    logicalOn[i] = false;
    apply(i); // Reconfiguration always starts at OFF; boot policy applies on reboot.
  }
  return true;
}
static bool write(uint8_t i, bool on) {
  if (i >= COUNT || !initialized || mode[i] != 4) return false;
  if (logicalOn[i] == on) return true;
  // Persist only transitions for outputs that actually need state restoration.
  if (bootPolicy[i] == 1) {
    Preferences prefs;
    if (prefs.begin("miragpio", false)) {
      char lk[12]; snprintf(lk, sizeof(lk), "last%u", unsigned(i));
      if (prefs.putBool(lk, on) != 1)
        Serial.printf("[MIRA][GPIO] NVS last-state save failed for GPIO%u\n", unsigned(pins[i]));
      prefs.end();
    } else {
      Serial.printf("[MIRA][GPIO] NVS open failed for GPIO%u\n", unsigned(pins[i]));
    }
  }
  driveOutput(i, on);
  return true;
}
static String stateJson() {
  StaticJsonDocument<1536> root;
  JsonArray arr = root.createNestedArray("pins");
  for (uint8_t i = 0; i < COUNT; ++i) {
    JsonObject p = arr.createNestedObject();
    p["gpio"] = pins[i];
    p["connector"] = String("EXT_IO") + (i + 1);
    p["mode"] = modeName(mode[i]);
    p["inverted"] = inverted[i];
    p["boot"] = bootName(bootPolicy[i]);
    p["state"] = mode[i] == 4 ? logicalOn[i] :
        (mode[i] != 0 && ((digitalRead(pins[i]) == HIGH) != inverted[i]));
  }
  String out;
  serializeJson(root, out);
  return out;
}
} // namespace MiraWT32GPIO
