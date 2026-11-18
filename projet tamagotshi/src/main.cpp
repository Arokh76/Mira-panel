// TAMAGOTCHI V2 - LilyGO T-Display V1.1 (ESP32 original, ST7789).
// Portrait 135x240, boutons: GPIO35 = suivant, GPIO0 = selection.
// Wi-Fi desactive ; etat conserve via NVS (Preferences) apres un redemarrage.
// Temps et evolution comptes uniquement pendant que l'ESP32 est allume.
#include <Arduino.h>
#include <Preferences.h>
#include <TFT_eSPI.h>

static TFT_eSPI tft;
static TFT_eSprite screen(&tft);
static Preferences prefs;

static const uint8_t PIN_NEXT = 35;
static const uint8_t PIN_OK = 0;
static const uint8_t PIN_BACKLIGHT = 4;
static const uint32_t MAGIC = 0x54414D32UL;
static const int W = 135;
static const int H = 240;

struct Pet {
  uint8_t faim;
  uint8_t joie;
  uint8_t energie;
  uint8_t proprete;
  uint8_t sante;
  uint32_t ageSec;
  bool dort;
};

static Pet pet = {80, 80, 82, 85, 90, 0, false};
static const char* MENU[] = {"MANGER", "JOUER", "DODO", "LAVER", "INFOS"};
static uint8_t menuIndex = 0;
static char notification[30] = "BONJOUR !";
static uint32_t notifUntil = 0;
static uint32_t lastSecond = 0;
static uint32_t lastDraw = 0;
static uint32_t lastSave = 0;

struct Button {
  uint8_t pin;
  bool raw = false;
  bool stable = false;
  uint32_t lastChange = 0;
  bool justPressed(uint32_t now) {
    const bool reading = (digitalRead(pin) == LOW);
    if (reading != raw) {
      raw = reading;
      lastChange = now;
    }
    if (now - lastChange >= 35 && reading != stable) {
      stable = reading;
      if (stable) return true;
    }
    return false;
  }
};

static Button nextButton = {PIN_NEXT};
static Button okButton = {PIN_OK};

static uint8_t bounded(int value) {
  if (value < 0) return 0;
  if (value > 100) return 100;
  return static_cast<uint8_t>(value);
}

static uint8_t stage() {
  if (pet.ageSec < 60) return 0;    // Oeuf : 1 minute (demo)
  if (pet.ageSec < 420) return 1;   // Bebe : jusqu'a 7 minutes
  return 2;                         // Enfant : ensuite
}

static const char* stageName() {
  switch (stage()) {
    case 0: return "OEUF";
    case 1: return "BEBE";
    default: return "ENFANT";
  }
}

static void showMessage(const char* msg, uint32_t duration = 1900) {
  snprintf(notification, sizeof(notification), "%s", msg);
  notifUntil = millis() + duration;
}

static void savePet() {
  prefs.putUChar("faim", pet.faim);
  prefs.putUChar("joie", pet.joie);
  prefs.putUChar("energie", pet.energie);
  prefs.putUChar("proprete", pet.proprete);
  prefs.putUChar("sante", pet.sante);
  prefs.putUInt("age", pet.ageSec);
  prefs.putBool("dort", pet.dort);
  prefs.putUInt("magic", MAGIC);  // En dernier, sert de validation du profil.
  lastSave = millis();
}

static void loadPet() {
  prefs.begin("tama_v2", false);
  if (prefs.getUInt("magic", 0) == MAGIC) {
    pet.faim = bounded(prefs.getUChar("faim", 80));
    pet.joie = bounded(prefs.getUChar("joie", 80));
    pet.energie = bounded(prefs.getUChar("energie", 82));
    pet.proprete = bounded(prefs.getUChar("proprete", 85));
    pet.sante = bounded(prefs.getUChar("sante", 90));
    pet.ageSec = prefs.getUInt("age", 0);
    pet.dort = prefs.getBool("dort", false);
  } else {
    savePet();
  }
}

static void doAction() {
  switch (menuIndex) {
    case 0:
      pet.dort = false;
      pet.faim = bounded(pet.faim + 26);
      pet.proprete = bounded(pet.proprete - 4);
      pet.sante = bounded(pet.sante + 2);
      showMessage("MIAM MIAM !");
      break;
    case 1:
      pet.dort = false;
      pet.joie = bounded(pet.joie + 27);
      pet.energie = bounded(pet.energie - 8);
      pet.faim = bounded(pet.faim - 3);
      showMessage("YOUPI !");
      break;
    case 2:
      pet.dort = !pet.dort;
      showMessage(pet.dort ? "ZZZ... DODO" : "REVEILLE !");
      break;
    case 3:
      pet.dort = false;
      pet.proprete = bounded(pet.proprete + 35);
      pet.joie = bounded(pet.joie + 4);
      showMessage("TOUT PROPRE !");
      break;
    case 4: {
      char txt[30];
      if (pet.ageSec < 60) {
        snprintf(txt, sizeof(txt), "AGE %lus", static_cast<unsigned long>(pet.ageSec));
      } else {
        snprintf(txt, sizeof(txt), "AGE %lum", static_cast<unsigned long>(pet.ageSec / 60));
      }
      showMessage(txt, 2500);
      return;
    }
  }
  savePet();
}

static void tickOneSecond() {
  const uint8_t oldStage = stage();
  ++pet.ageSec;
  if ((pet.ageSec % 42) == 0) pet.faim = bounded(pet.faim - 1);
  if ((pet.ageSec % 55) == 0) pet.joie = bounded(pet.joie - 1);
  if ((pet.ageSec % 75) == 0) pet.proprete = bounded(pet.proprete - 1);

  if (pet.dort) {
    if ((pet.ageSec % 7) == 0) pet.energie = bounded(pet.energie + 1);
    if (pet.energie == 100) {
      pet.dort = false;
      showMessage("BIEN DORMI !");
    }
  } else if ((pet.ageSec % 50) == 0) {
    pet.energie = bounded(pet.energie - 1);
  }

  if ((pet.ageSec % 60) == 0) {
    if (pet.faim < 15 || pet.proprete < 15 || pet.energie < 10) {
      pet.sante = bounded(pet.sante - 2);
    } else if (pet.faim > 65 && pet.proprete > 65 &&
               pet.energie > 50 && pet.joie > 55) {
      pet.sante = bounded(pet.sante + 1);
    }
  }

  if (stage() != oldStage) {
    showMessage(stage() == 1 ? "JE SUIS NE !" : "J'AI GRANDI !", 3500);
    savePet();
  }
}

static uint16_t valueColor(uint8_t value) {
  if (value <= 24) return TFT_RED;
  if (value <= 55) return TFT_ORANGE;
  return TFT_GREEN;
}

static void drawStat(int y, const char* label, uint8_t value) {
  screen.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  screen.drawString(label, 6, y - 1, 1);

  const int bx = 18;
  const int bw = 75;
  screen.drawRoundRect(bx, y, bw, 8, 2, TFT_DARKGREY);
  const int fill = (static_cast<int>(value) * (bw - 4)) / 100;
  if (fill > 0) screen.fillRect(bx + 2, y + 2, fill, 4, valueColor(value));

  char number[6];
  snprintf(number, sizeof(number), "%3u%%", static_cast<unsigned>(value));
  screen.setTextColor(TFT_WHITE, TFT_BLACK);
  screen.drawString(number, 99, y - 1, 1);
}

static void drawFace(int cx, int eyeY, int bob, bool happy, bool sad) {
  const bool blink = pet.dort || ((millis() % 3700) < 170);
  if (blink) {
    screen.drawFastHLine(cx - 18, eyeY + bob, 11, TFT_BLACK);
    screen.drawFastHLine(cx + 8, eyeY + bob, 11, TFT_BLACK);
  } else {
    screen.fillRoundRect(cx - 17, eyeY - 3 + bob, 8, 11, 3, TFT_BLACK);
    screen.fillRoundRect(cx + 10, eyeY - 3 + bob, 8, 11, 3, TFT_BLACK);
  }
  if (pet.dort) {
    screen.drawFastHLine(cx - 6, eyeY + 17 + bob, 12, TFT_BLACK);
  } else if (sad) {
    screen.drawLine(cx - 7, eyeY + 23 + bob, cx, eyeY + 18 + bob, TFT_BLACK);
    screen.drawLine(cx, eyeY + 18 + bob, cx + 7, eyeY + 23 + bob, TFT_BLACK);
  } else if (happy) {
    screen.drawLine(cx - 9, eyeY + 16 + bob, cx, eyeY + 23 + bob, TFT_BLACK);
    screen.drawLine(cx, eyeY + 23 + bob, cx + 9, eyeY + 16 + bob, TFT_BLACK);
  } else {
    screen.drawLine(cx - 7, eyeY + 20 + bob, cx + 7, eyeY + 20 + bob, TFT_BLACK);
  }
}

static void drawPet(uint32_t now) {
  const int cx = 67;
  const uint8_t evolution = stage();
  const bool happy = (pet.joie > 45 && pet.faim > 30 && pet.sante > 30);
  const bool sad = !happy;
  const int bob = (pet.dort || evolution == 0) ? 0 : ((now / 380) % 2 ? 0 : 3);
  const int wobble = (now / 540) % 2 ? -2 : 2;

  screen.drawFastHLine(24, 179, 87, TFT_DARKGREY);
  screen.drawFastHLine(cx - 19, 181, 38, TFT_DARKGREY);

  if (evolution == 0) {
    // Oeuf : une petite coquille qui oscille.
    screen.fillRoundRect(cx - 28 + wobble, 106, 56, 70, 26, TFT_WHITE);
    screen.fillRoundRect(cx - 22 + wobble, 112, 44, 59, 23, TFT_LIGHTGREY);
    screen.fillCircle(cx - 10 + wobble, 140, 4, TFT_BLACK);
    screen.fillCircle(cx + 10 + wobble, 140, 4, TFT_BLACK);
    screen.drawFastHLine(cx - 4 + wobble, 151, 8, TFT_BLACK);
    screen.drawFastHLine(cx - 17 + wobble, 166, 34, TFT_CYAN);
    screen.drawFastHLine(cx - 10 + wobble, 169, 20, TFT_CYAN);
  } else {
    const bool child = evolution == 2;
    const uint16_t mainColor = child ? TFT_GREEN : TFT_CYAN;
    const int bodyW = child ? 73 : 58;
    const int bodyH = child ? 65 : 58;
    const int x = cx - bodyW / 2;
    const int y = child ? 107 + bob : 115 + bob;

    if (child) {
      screen.fillRoundRect(cx - 29, y - 15, 19, 26, 6, mainColor);
      screen.fillRoundRect(cx + 10, y - 15, 19, 26, 6, mainColor);
    }
    screen.fillRoundRect(x, y, bodyW, bodyH, 23, mainColor);
    screen.fillRoundRect(cx - 30, y + bodyH - 6, 21, 10, 4, mainColor);
    screen.fillRoundRect(cx + 9, y + bodyH - 6, 21, 10, 4, mainColor);
    screen.fillCircle(cx - (child ? 26 : 21), y + 34, 5, TFT_PINK);
    screen.fillCircle(cx + (child ? 26 : 21), y + 34, 5, TFT_PINK);
    drawFace(cx, y + 23 - bob, bob, happy, sad);
  }

  if (pet.dort) {
    screen.setTextColor(TFT_WHITE, TFT_BLACK);
    screen.drawString("Zzz", 99, 101, 2);
  } else if (pet.sante < 20) {
    screen.setTextColor(TFT_RED, TFT_BLACK);
    screen.drawString("!", 105, 105, 2);
  }

  screen.setTextColor(TFT_WHITE, TFT_BLACK);
  if ((int32_t)(now - notifUntil) < 0) {
    screen.drawCentreString(notification, cx, 185, 1);
  } else {
    const char* state = pet.dort ? "IL DORT..." :
                        pet.sante < 25 ? "MALADE..." :
                        pet.faim < 20 ? "J'AI FAIM !" :
                        pet.joie < 25 ? "JE M'ENNUIE" :
                        "TOUT VA BIEN !";
    screen.drawCentreString(state, cx, 185, 1);
  }
}

static void render(uint32_t now) {
  screen.fillSprite(TFT_BLACK);
  screen.setTextColor(TFT_CYAN, TFT_BLACK);
  screen.drawString("TAMA V2", 6, 5, 2);
  char clock[16];
  const uint32_t minutes = pet.ageSec / 60;
  snprintf(clock, sizeof(clock), "%lu:%02lu",
           static_cast<unsigned long>(minutes),
           static_cast<unsigned long>(pet.ageSec % 60));
  screen.setTextColor(TFT_WHITE, TFT_BLACK);
  screen.drawRightString(clock, 129, 8, 1);

  screen.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  screen.drawCentreString(stageName(), 67, 22, 1);

  drawStat(37, "F", pet.faim);
  drawStat(48, "J", pet.joie);
  drawStat(59, "E", pet.energie);
  drawStat(70, "P", pet.proprete);
  drawStat(81, "S", pet.sante);
  screen.drawFastHLine(4, 94, W - 8, TFT_DARKGREY);

  drawPet(now);

  screen.drawRoundRect(3, 199, W - 6, 27, 6, TFT_CYAN);
  screen.setTextColor(TFT_CYAN, TFT_BLACK);
  screen.drawCentreString(MENU[menuIndex], 67, 203, 2);
  screen.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  screen.drawCentreString("35 SUIV.  0 OK", 67, 230, 1);
  screen.pushSprite(0, 0);
}

void setup() {
  pinMode(PIN_NEXT, INPUT);         // Pull-up externe sur T-Display original
  pinMode(PIN_OK, INPUT_PULLUP);
  pinMode(PIN_BACKLIGHT, OUTPUT);
  digitalWrite(PIN_BACKLIGHT, HIGH);

  tft.init();
  tft.setRotation(0);              // Portrait : 135 x 240
  tft.fillScreen(TFT_BLACK);
  screen.setColorDepth(16);
  if (screen.createSprite(W, H) == nullptr) {
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawCentreString("ERREUR MEMOIRE", W / 2, 110, 1);
    while (true) delay(1000);
  }
  screen.setTextDatum(TL_DATUM);
  loadPet();
  lastSecond = millis();
  lastSave = millis();
  showMessage("BONJOUR !", 2200);
}

void loop() {
  const uint32_t now = millis();
  if (nextButton.justPressed(now)) {
    menuIndex = static_cast<uint8_t>((menuIndex + 1) % 5);
  }
  if (okButton.justPressed(now)) {
    doAction();
  }

  while (static_cast<uint32_t>(now - lastSecond) >= 1000UL) {
    lastSecond += 1000;
    tickOneSecond();
  }
  if (static_cast<uint32_t>(now - lastSave) >= 60000UL) savePet();

  if (static_cast<uint32_t>(now - lastDraw) >= 125UL) {
    lastDraw = now;
    render(now);
  }
  delay(2);
}
