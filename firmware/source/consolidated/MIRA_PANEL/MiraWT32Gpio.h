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
// GPIO changes are reported to Jeedom from the Arduino main loop, never
// from the AsyncWebServer callback or while handling a relay write.
static volatile uint32_t stateRevision = 1;
static volatile uint32_t changedAtMs = 0;
// Mapping by stable Jeedom command logical name (On_Salle, Off_Salle, etc.).
// kind 0 = LOW, 1 = HIGH, 2 = read-state info.
static constexpr uint8_t LINK_COUNT = 24;
static String linkName[LINK_COUNT];
static uint8_t linkPin[LINK_COUNT] = {0};
static uint8_t linkKind[LINK_COUNT] = {0};
static int findLink(const String& name) {
  for (uint8_t i=0; i<LINK_COUNT; ++i)
    if (linkName[i].length() && linkName[i] == name) return i;
  return -1;
}
static bool anyToggleLink(const String& widget) {
  return findLink(String("On_") + widget) >= 0 ||
         findLink(String("Off_") + widget) >= 0;
}


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
  Preferences links;
  if (links.begin("miralinks", true)) {
    for (uint8_t j=0; j<LINK_COUNT; ++j) {
      char nk[12], pk[12], kk[12];
      snprintf(nk, sizeof(nk), "name%u", unsigned(j));
      snprintf(pk, sizeof(pk), "pin%u", unsigned(j));
      snprintf(kk, sizeof(kk), "kind%u", unsigned(j));
      linkName[j] = links.getString(nk, "");
      linkPin[j] = links.getUChar(pk, 0);
      linkKind[j] = links.getUChar(kk, 0);
      if (findPin(linkPin[j]) < 0 || linkKind[j] > 2) linkName[j] = "";
    }
    links.end();
  }
  multiLoad();
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
    changedAtMs = millis();
    ++stateRevision;
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
  changedAtMs = millis();
  ++stateRevision;
  return true;
}
static bool physicalHigh(uint8_t i) {
  return i < COUNT && mode[i] == 4 && (logicalOn[i] != inverted[i]);
}
static bool writePhysical(uint8_t i, bool high) {
  return i < COUNT && write(i, high != inverted[i]);
}
static bool saveLink(String name, int gpio, uint8_t kind) {
  name.trim();
  if (!name.length() || name.length() > 62 || kind > 2) return false;
  int pinIndex = gpio == 0 ? -1 : findPin(gpio);
  if (gpio != 0 && (pinIndex < 0 || mode[pinIndex] != 4)) return false;
  int slot = findLink(name);
  if (gpio == 0 && slot < 0) return true;
  if (slot < 0) {
    for (int j=0; j<LINK_COUNT; ++j) {
      if (!linkName[j].length()) { slot=j; break; }
    }
  }
  if (slot < 0) return false;
  Preferences p;
  if (!p.begin("miralinks", false)) return false;
  char nk[12], pk[12], kk[12];
  snprintf(nk, sizeof(nk), "name%u", unsigned(slot));
  snprintf(pk, sizeof(pk), "pin%u", unsigned(slot));
  snprintf(kk, sizeof(kk), "kind%u", unsigned(slot));
  bool ok;
  if (gpio == 0) {
    p.remove(nk); p.remove(pk); p.remove(kk);
    ok = true;
  } else {
    ok = p.putString(nk, name) == name.length() &&
         p.putUChar(pk, uint8_t(gpio)) == 1 &&
         p.putUChar(kk, kind) == 1;
  }
  p.end();
  if (!ok) return false;
  linkName[slot] = gpio == 0 ? "" : name;
  linkPin[slot] = gpio == 0 ? 0 : gpio;
  linkKind[slot] = kind;
  Serial.printf("[MIRA][GPIO] liaison %s => GPIO%d kind=%u\n",
                name.c_str(), gpio, unsigned(kind));
  return true;
}
static bool actionMapped(const String& name) { return findLink(name) >= 0; }
static bool executeAction(const String& name) {
  int slot = findLink(name);
  if (slot < 0 || linkKind[slot] > 1) return false;
  int i = findPin(linkPin[slot]);
  return i >= 0 && writePhysical(uint8_t(i), linkKind[slot] == 1);
}
static bool validToggle(const String& name, int& pinIndex, bool& onHigh, bool& offHigh) {
  const int on = findLink(String("On_") + name);
  const int off = findLink(String("Off_") + name);
  if (on < 0 || off < 0) return false;
  if (linkPin[on] != linkPin[off] || linkKind[on] > 1 ||
      linkKind[off] > 1 || linkKind[on] == linkKind[off]) return false;
  pinIndex = findPin(linkPin[on]);
  if (pinIndex < 0 || mode[pinIndex] != 4) return false;
  onHigh = linkKind[on] == 1;
  offHigh = linkKind[off] == 1;
  return true;
}
static bool executeToggle(const String& name, bool on) {
  int idx = -1; bool onHigh=false, offHigh=false;
  if (!validToggle(name, idx, onHigh, offHigh)) return false;
  return writePhysical(uint8_t(idx), on ? onHigh : offHigh);
}
static bool toggleState(const String& name, bool& result) {
  int idx = -1; bool onHigh=false, offHigh=false;
  if (!validToggle(name, idx, onHigh, offHigh)) return false;
  result = physicalHigh(uint8_t(idx)) == onHigh;
  return true;
}


// Buttons with a dry contact between GPIO and GND (active LOW).
// Never execute an action when a pin is configured/restarted: only stable
// press-and-release transitions can generate a click gesture.
static constexpr uint8_t MULTI_COUNT = 6;
static String multiName[MULTI_COUNT];
static uint8_t multiPin[MULTI_COUNT] = {0};
static bool multiVisible[MULTI_COUNT] = {false};
static bool multiStarted[MULTI_COUNT] = {false};
static bool multiRawDown[MULTI_COUNT] = {false};
static bool multiStableDown[MULTI_COUNT] = {false};
static bool multiLongDone[MULTI_COUNT] = {false};
static uint8_t multiClickCount[MULTI_COUNT] = {0};
static uint32_t multiRawChanged[MULTI_COUNT] = {0};
static uint32_t multiPressStarted[MULTI_COUNT] = {0};
static uint32_t multiReleaseAt[MULTI_COUNT] = {0};
static int multiPendingSlot = -1;
static uint8_t multiPendingKind = 0; // 0=single,1=double,2=triple,3=long

static int findMulti(const String& name) {
  for (int i=0;i<MULTI_COUNT;i++) if (multiName[i].length() && multiName[i]==name) return i;
  return -1;
}
static bool isMultiVisible(const String& name) {
  const int i=findMulti(name);
  return i>=0 && multiVisible[i];
}
static void multiLoad() {
  Preferences p;
  if (!p.begin("miramulti", true)) return;
  for (uint8_t i=0; i<MULTI_COUNT; i++) {
    char nk[12],pk[12],vk[12];
    snprintf(nk,sizeof(nk),"name%u",unsigned(i));
    snprintf(pk,sizeof(pk),"pin%u",unsigned(i));
    snprintf(vk,sizeof(vk),"vis%u",unsigned(i));
    multiName[i]=p.getString(nk,"");
    multiPin[i]=p.getUChar(pk,0);
    multiVisible[i]=p.getBool(vk,false);
    int idx=findPin(multiPin[i]);
    if(idx<0 || mode[idx]==0 || mode[idx]==4) multiPin[i]=0;
  }
  p.end();
}
static bool multiConfigure(String name, int gpio, bool visible, bool& needsReload) {
  needsReload=false;
  name.trim();
  if (!name.length() || name.length()>45) return false;
  int pinIndex = gpio ? findPin(gpio) : -1;
  if (gpio && (pinIndex < 0 || mode[pinIndex] == 4)) return false;
  for (uint8_t j=0; j<MULTI_COUNT; ++j)
    if (gpio && multiPin[j] == gpio && multiName[j].length() && multiName[j]!=name) return false;

  // A previously unused pin is automatically assigned input pull-up.
  if (pinIndex>=0 && mode[pinIndex]==0 &&
      !configure((uint8_t)pinIndex,2,false,0)) return false;

  int slot=findMulti(name);
  if (slot<0) for(uint8_t i=0;i<MULTI_COUNT;i++)
    if(!multiName[i].length()){slot=i;break;}
  if(slot<0) return false;
  const bool changed = multiVisible[slot]!=visible;
  const bool differentPin = multiPin[slot] != gpio;
  Preferences p;
  if(!p.begin("miramulti",false))return false;
  char nk[12],pk[12],vk[12];
  snprintf(nk,sizeof(nk),"name%u",unsigned(slot));
  snprintf(pk,sizeof(pk),"pin%u",unsigned(slot));
  snprintf(vk,sizeof(vk),"vis%u",unsigned(slot));
  bool ok = p.putString(nk,name)==name.length() &&
            p.putUChar(pk,(uint8_t)gpio)==1 &&
            p.putBool(vk,visible)==1;
  p.end();
  if(!ok)return false;
  multiName[slot]=name;
  multiPin[slot]=(uint8_t)gpio;
  multiVisible[slot]=visible;
  if (differentPin) {
    multiStarted[slot]=false; multiClickCount[slot]=0;
    multiLongDone[slot]=false;
  }
  needsReload=changed;
  return true;
}

static bool multiPoll(String& name, uint8_t& kind) {
  const uint32_t now=millis();
  // Consume a pending event, never send it from a web callback.
  if (multiPendingSlot >= 0) {
    const int slot=multiPendingSlot;
    multiPendingSlot=-1;
    if (multiPin[slot] && multiName[slot].length()) {
      name=multiName[slot]; kind=multiPendingKind; return true;
    }
  }
  for(uint8_t i=0;i<MULTI_COUNT;i++) {
    if (!multiPin[i] || !multiName[i].length()) continue;
    int idx=findPin(multiPin[i]);
    if(idx<0 || mode[idx]==0 || mode[idx]==4) continue;
    const bool down = digitalRead(multiPin[i])==LOW;
    if(!multiStarted[i]) {
      multiStarted[i]=true;
      multiRawDown[i]=down;
      multiStableDown[i]=down;
      multiRawChanged[i]=now;
      multiPressStarted[i]=now;
      multiClickCount[i]=0;
      multiLongDone[i]=false;
      continue;
    }
    if (down != multiRawDown[i]) {
      multiRawDown[i]=down;
      multiRawChanged[i]=now;
    }
    if (down != multiStableDown[i] && now-multiRawChanged[i]>=35) {
      multiStableDown[i]=down;
      if (down) { multiPressStarted[i]=now; multiLongDone[i]=false; }
      else {
        if (!multiLongDone[i]) {
          if (multiClickCount[i]<3) multiClickCount[i]++;
          multiReleaseAt[i]=now;
        } else multiClickCount[i]=0;
      }
    }
    if (multiStableDown[i] && !multiLongDone[i] &&
        now-multiPressStarted[i]>=900) {
      multiLongDone[i]=true;
      multiClickCount[i]=0; // never trigger a short click after long hold
      name=multiName[i];kind=3;return true;
    }
    if (!multiStableDown[i] && multiClickCount[i] &&
        now-multiReleaseAt[i]>=370) {
      kind=multiClickCount[i]-1; // 0,1,2
      multiClickCount[i]=0;
      name=multiName[i];return true;
    }
  }
  return false;
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
