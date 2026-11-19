// Tamagotchi V5 — LilyGO T-Display V1.1 ORIGINAL (ESP32/ST7789, 135x240).
// Portrait, creature animee grand format, stats sur pages annexes, 2 mini-jeux.
 // GPIO35 : menu suivant / retour; GPIO0 : valider.
 // Stockage compatible V2 : namespace "tama_v2", magic 0x54414D32.
#include <Arduino.h>
#include <Preferences.h>
#include <TFT_eSPI.h>
#include <math.h>
#include <esp_sleep.h>
#include <driver/gpio.h>

static TFT_eSPI lcd;
static TFT_eSprite gfx(&lcd);
static Preferences prefs;
static const uint8_t PIN_NEXT = 35, PIN_OK = 0, PIN_LIGHT = 4;
static const uint8_t PIN_BAT_ENABLE = 14, PIN_BAT_ADC = 34;
// Seuils conservateurs pour LiPo 1S. Le % reste une estimation.
static const uint16_t BAT_SLEEP_MV = 3480, BAT_RESUME_MV = 3700;
static const uint32_t BAT_CHECK_MS = 30000, DISPLAY_IDLE_MS = 120000;
static const uint64_t BAT_RECHECK_US = 5ULL * 60ULL * 1000000ULL;
RTC_DATA_ATTR static bool batteryHibernating = false;
static uint16_t batteryMv = 0;  // 0 = mesure indisponible
static uint8_t batteryPercent = 0, consecutiveLow = 0;
static uint32_t lastBatteryCheck = 0, lastInteraction = 0;
static bool lcdReady = false, displayOff = false;
static const uint32_t MAGIC = 0x54414D32UL;
static const int SW = 135, SH = 240;
static const char* const MENU[] = {
  "MANGER", "ETOILE", "REFLEXE", "DODO", "BAIN", "SOINS", "CADEAU", "CALIN", "INFOS"
};
static const uint8_t MENU_COUNT = sizeof(MENU) / sizeof(MENU[0]);

struct Pet {
  uint8_t faim, joie, energie, proprete, sante;
  uint32_t ageSec;
  bool dort;
};
static Pet pet = {80,80,82,85,90,0,false};

struct Key {
  uint8_t pin = 0;
  bool raw = false;
  bool stable = false;
  uint32_t changedAt = 0;
  bool readPress(uint32_t now) {
    const bool r = (digitalRead(pin) == LOW);
    if (r != raw) { raw = r; changedAt = now; }
    if (r != stable && (uint32_t)(now - changedAt) >= 35) {
      stable = r;
      return stable;
    }
    return false;
  }
};
static Key nextKey, okKey;

enum Page : uint8_t { HOME, INFOS, SOUVENIRS, MINIGAME, REFLEXGAME };
enum Mood : uint8_t { HAPPY, NORMAL, SAD, SICK, ASLEEP };
enum Effect : uint8_t { FX_NONE, FX_FOOD, FX_CLEAN, FX_HEAL, FX_GIFT, FX_GROW, FX_WAKE, FX_PLAY, FX_HUG, FX_SURPRISE };
static Page page = HOME;
static Effect effect = FX_NONE;
static uint8_t selected = 0;
static uint16_t gameWins = 0, gamePlayed = 0;
static uint16_t reflexWins = 0, reflexPlayed = 0, surpriseCount = 0, hugCount = 0;
static uint8_t friendship = 50;
static uint32_t lastSurpriseAge = 0;
static uint8_t gameRound = 0, gameHits = 0;
static uint32_t gameStarted = 0, gamePauseUntil = 0;
static bool lastHit = false;
static uint32_t reflexReadyAt = 0, reflexPauseUntil = 0;
static uint8_t reflexRound = 0, reflexHits = 0;
static bool reflexLastHit = false;
static char speech[30] = "";
static uint32_t speechUntil = 0, effectUntil = 0;
static uint32_t lastFrame = 0, lastSecond = 0, lastSave = 0, lastGift = 0;

static uint8_t clip100(int v) {
  return static_cast<uint8_t>(v < 0 ? 0 : v > 100 ? 100 : v);
}
static uint8_t stage() {
  if (pet.ageSec < 60) return 0;        // OEUF 1 minute : test accelere
  if (pet.ageSec < 420) return 1;       // BEBE 7 minutes
  if (pet.ageSec < 1800) return 2;      // ENFANT 30 minutes
  return 3;                             // ADULTE
}
static const char* stageLabel() {
  switch (stage()) {
    case 0: return "OEUF";
    case 1: return "BEBE";
    case 2: return "ENFANT";
    default: return "ADULTE";
  }
}
static Mood mood() {
  if (pet.dort) return ASLEEP;
  if (pet.sante < 25) return SICK;
  if (pet.faim < 22 || pet.joie < 25 || pet.proprete < 17) return SAD;
  if (pet.joie > 68 && pet.sante > 65 && pet.faim > 40) return HAPPY;
  return NORMAL;
}
static void say(const char* msg, Effect fx=FX_NONE, uint32_t time=2000) {
  snprintf(speech, sizeof(speech), "%s", msg);
  const uint32_t now = millis();
  speechUntil = now + time;
  effect = fx;
  effectUntil = now + time;
}
static void save() {
  prefs.putUChar("faim", pet.faim);
  prefs.putUChar("joie", pet.joie);
  prefs.putUChar("energie", pet.energie);
  prefs.putUChar("proprete", pet.proprete);
  prefs.putUChar("sante", pet.sante);
  prefs.putUInt("age", pet.ageSec);
  prefs.putBool("dort", pet.dort);
  prefs.putUShort("wins", gameWins);
  prefs.putUShort("games", gamePlayed);
  prefs.putUShort("r_wins", reflexWins);
  prefs.putUShort("r_games", reflexPlayed);
  prefs.putUShort("events", surpriseCount);
  prefs.putUShort("cuddles", hugCount);
  prefs.putUChar("amitie", friendship);
  prefs.putUInt("eventage", lastSurpriseAge);
  prefs.putUInt("magic", MAGIC);
  lastSave = millis();
}
static void load() {
  prefs.begin("tama_v2", false);
  if (prefs.getUInt("magic", 0) == MAGIC) {
    pet.faim = clip100(prefs.getUChar("faim", 80));
    pet.joie = clip100(prefs.getUChar("joie", 80));
    pet.energie = clip100(prefs.getUChar("energie", 82));
    pet.proprete = clip100(prefs.getUChar("proprete", 85));
    pet.sante = clip100(prefs.getUChar("sante", 90));
    pet.ageSec = prefs.getUInt("age", 0);
    pet.dort = prefs.getBool("dort", false);
    gameWins = prefs.getUShort("wins", 0);
    gamePlayed = prefs.getUShort("games", 0);
    reflexWins = prefs.getUShort("r_wins", 0);
    reflexPlayed = prefs.getUShort("r_games", 0);
    surpriseCount = prefs.getUShort("events", 0);
    hugCount = prefs.getUShort("cuddles", 0);
    friendship = clip100(prefs.getUChar("amitie", 50));
    lastSurpriseAge = prefs.getUInt("eventage", pet.ageSec);
  } else {
    lastSurpriseAge = pet.ageSec;
    save();
  }
}
// Lecture du pont diviseur 1:2 de la version originale (GPIO14 HIGH -> GPIO34 ADC).
// Une valeur hors plage est refusee pour eviter un sommeil intempestif.
static uint16_t sampleBatteryMv() {
  digitalWrite(PIN_BAT_ENABLE, HIGH);
  delay(12); // stabilisation du pont diviseur
  (void)analogReadMilliVolts(PIN_BAT_ADC); // Premiere lecture jetee
  uint32_t total = 0;
  for (uint8_t n=0; n<12; n++) {
    total += analogReadMilliVolts(PIN_BAT_ADC);
    delay(2);
  }
  digitalWrite(PIN_BAT_ENABLE, LOW); // evite un courant de fuite permanent
  const uint32_t value = (total / 12UL) * 2UL;
  if (value < 2700 || value > 4400) return 0;
  return static_cast<uint16_t>(value);
}
static uint8_t estimateBattery(uint16_t mv) {
  if (!mv) return 0;
  static const uint16_t voltage[] = {3300,3450,3550,3650,3750,3850,3950,4050,4150,4200};
  static const uint8_t level[] = {0,4,10,20,35,55,73,87,96,100};
  if (mv <= voltage[0]) return 0;
  if (mv >= voltage[9]) return 100;
  for (uint8_t i=1; i<10; ++i) {
    if (mv <= voltage[i]) {
      return level[i-1] + ((uint32_t)(mv-voltage[i-1]) *
                               (level[i]-level[i-1])) /
                               (voltage[i]-voltage[i-1]);
    }
  }
  return 100;
}
static void refreshBattery() {
  batteryMv = sampleBatteryMv();
  batteryPercent = estimateBattery(batteryMv);
  lastBatteryCheck = millis();
}
static void shutDisplay() {
  if (!lcdReady || displayOff) return;
  digitalWrite(PIN_LIGHT, LOW);
  lcd.writecommand(TFT_DISPOFF);
  lcd.writecommand(TFT_SLPIN);
  displayOff = true;
}
static void wakeDisplay() {
  if (!lcdReady || !displayOff) return;
  lcd.writecommand(TFT_SLPOUT);
  delay(125);
  lcd.writecommand(TFT_DISPON);
  digitalWrite(PIN_LIGHT, HIGH);
  displayOff = false;
  lastFrame = 0;
}
// Deep-sleep = vrai redemarrage du MCU au reveil, donc sauvegarde NVS d'abord.
static void hibernateBattery() {
  if (prefs.isKey("magic")) save();
  batteryHibernating = true;
  shutDisplay();
  digitalWrite(PIN_LIGHT, LOW);
  digitalWrite(PIN_BAT_ENABLE, LOW);
  // Conserver le retroeclairage a LOW pendant deep sleep sur ESP32 original.
  gpio_hold_en(GPIO_NUM_4);
  gpio_deep_sleep_hold_en();
  esp_sleep_enable_timer_wakeup(BAT_RECHECK_US);
  // Le bouton GPIO35 dispose d'un pull-up externe sur la carte d'origine.
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_35, 0);
  delay(12);
  esp_deep_sleep_start();
}
static void displayBatteryIcon() {
  if (!batteryMv) return;
  const uint16_t color = batteryMv < 3600 ? TFT_ORANGE : TFT_GREEN;
  const int x=111, y=197;
  gfx.drawRect(x,y,17,9,color);
  gfx.fillRect(x+17,y+3,2,3,color);
  const int w = (batteryPercent*13)/100;
  if (w>0) gfx.fillRect(x+2,y+2,w,5,color);
  char pct[8];
  snprintf(pct,sizeof(pct),"%u%%",(unsigned)batteryPercent);
  gfx.setTextColor(color,TFT_BLACK);
  gfx.drawRightString(pct,106,197,1);
}

static void tickSecond() {
  const uint8_t oldStage = stage();
  ++pet.ageSec;
  if (pet.ageSec % 42 == 0) pet.faim = clip100(pet.faim-1);
  if (pet.ageSec % 55 == 0) pet.joie = clip100(pet.joie-1);
  if (pet.ageSec % 75 == 0) pet.proprete = clip100(pet.proprete-1);
  if (pet.dort) {
    if (pet.ageSec % 7 == 0) pet.energie = clip100(pet.energie+1);
    if (pet.energie >= 100) {
      pet.dort = false;
      say("BIEN DORMI !", FX_WAKE);
    }
  } else if (pet.ageSec % 50 == 0) {
    pet.energie = clip100(pet.energie-1);
  }
  if (pet.ageSec % 180 == 0 && friendship > 0) {
    friendship = clip100(friendship - 1);
  }
  if (pet.ageSec % 60 == 0) {
    if (pet.faim < 15 || pet.proprete < 15 || pet.energie < 10) {
      pet.sante = clip100(pet.sante-2);
    } else if (pet.faim > 65 && pet.proprete > 65 &&
               pet.energie > 50 && pet.joie > 55) {
      pet.sante = clip100(pet.sante+1);
    }
  }
  if (stage() != oldStage) {
    say(stage() == 1 ? "JE SUIS NE !" : "J'AI GRANDI !", FX_GROW, 3400);
    save();
  }
}
static void startGame() {
  page = MINIGAME;
  pet.dort = false;
  gameRound = 0;
  gameHits = 0;
  gameStarted = millis();
  gamePauseUntil = 0;
  lastHit = false;
}
static int starX(uint32_t now) {
  // Triangulaire sur 2 secondes : 18..116.
  const uint32_t p = ((now - gameStarted) % 2000UL);
  const int dist = static_cast<int>(p <= 1000 ? p : 2000UL-p);
  return 18 + (dist * 98) / 1000;
}
static void gamePress(uint32_t now) {
  if (static_cast<int32_t>(now - gamePauseUntil) < 0) return;
  const bool hit = abs(starX(now) - 67) <= 13;
  lastHit = hit;
  if (hit) ++gameHits;
  ++gameRound;
  gamePauseUntil = now + 520;
  if (gameRound >= 3) {
    pet.joie = clip100(pet.joie + 8 + 9 * gameHits);
    pet.energie = clip100(pet.energie - 7);
    pet.faim = clip100(pet.faim - 3);
    ++gamePlayed;
    if (gameHits == 3) ++gameWins;
    friendship = clip100(friendship + gameHits * 3);
    page = HOME;
    say(gameHits == 3 ? "CHAMPION !" : gameHits ? "BIEN JOUE !" : "ON REESSAIE ?", FX_PLAY, 2700);
    save();
  }
}
// Mini-jeu 2 : reactivite. Appuyer uniquement lorsque TOP! s'affiche.
static void startReflex(uint32_t now) {
  page = REFLEXGAME;
  pet.dort = false;
  reflexRound = 0;
  reflexHits = 0;
  reflexPauseUntil = 0;
  reflexReadyAt = now + random(1100, 2800);
  reflexLastHit = false;
}
static void reflexPress(uint32_t now) {
  if ((int32_t)(now - reflexPauseUntil) < 0) return;
  const bool hit = (int32_t)(now - reflexReadyAt) >= 0 &&
                   (uint32_t)(now - reflexReadyAt) <= 700;
  reflexLastHit = hit;
  if (hit) ++reflexHits;
  ++reflexRound;
  if (reflexRound == 3) {
    ++reflexPlayed;
    if (reflexHits == 3) ++reflexWins;
    pet.joie = clip100(pet.joie + 5 + 9 * reflexHits);
    pet.energie = clip100(pet.energie - 6);
    friendship = clip100(friendship + reflexHits * 3);
    page = HOME;
    say(reflexHits == 3 ? "SUPER REFLEXE !" : reflexHits ? "BIEN JOUE !" : "ON REESSAIE ?", FX_PLAY, 2700);
    save();
    return;
  }
  reflexPauseUntil = now + 800;
  reflexReadyAt = reflexPauseUntil + random(900, 2700);
}
// Rencontres non intrusives : une surprise environ toutes les 3 minutes.
static void trySurprise() {
  if (page != HOME || pet.dort || pet.ageSec < lastSurpriseAge ||
      pet.ageSec - lastSurpriseAge < 180) return;
  lastSurpriseAge = pet.ageSec;
  ++surpriseCount;
  switch(random(0, 4)) {
    case 0:
      pet.faim = clip100(pet.faim + 8);
      say("UN PETIT GOUTER !", FX_FOOD, 2600);
      break;
    case 1:
      pet.joie = clip100(pet.joie + 10);
      say("UN JOUET TROUVE !", FX_GIFT, 2600);
      break;
    case 2:
      friendship = clip100(friendship + 6);
      say("JE PENSE A TOI !", FX_HUG, 2600);
      break;
    default:
      pet.energie = clip100(pet.energie + 7);
      say("PETITE SIESTE !", FX_SURPRISE, 2600);
      break;
  }
  save();
}
static void chooseAction(uint32_t now) {
  switch(selected) {
    case 0:
      pet.dort = false;
      pet.faim = clip100(pet.faim + 26);
      pet.proprete = clip100(pet.proprete - 3);
      pet.sante = clip100(pet.sante + 2);
      say("MIAM MIAM !", FX_FOOD);
      break;
    case 1:
      startGame();
      return;
    case 2:
      startReflex(now);
      return;
    case 3:
      pet.dort = !pet.dort;
      say(pet.dort ? "BONNE NUIT..." : "BONJOUR !", pet.dort ? FX_NONE : FX_WAKE);
      break;
    case 4:
      pet.dort = false;
      pet.proprete = clip100(pet.proprete + 34);
      pet.joie = clip100(pet.joie + 4);
      say("TOUT PROPRE !", FX_CLEAN);
      break;
    case 5:
      pet.sante = clip100(pet.sante + 25);
      pet.joie = clip100(pet.joie - 2);
      say("CA VA MIEUX !", FX_HEAL);
      break;
    case 6:
      if ((int32_t)(now - lastGift) < 25000 && lastGift != 0) {
        say("PATIENCE !", FX_NONE, 1100);
        return;
      }
      lastGift = now;
      pet.joie = clip100(pet.joie + 18);
      friendship = clip100(friendship + 4);
      say("OH MERCI !", FX_GIFT);
      break;
    case 7:
      pet.dort = false;
      pet.joie = clip100(pet.joie + 12);
      friendship = clip100(friendship + 10);
      ++hugCount;
      say("GROS CALIN !", FX_HUG, 2500);
      break;
    case 8:
      page = INFOS;
      return;
  }
  save();
}

static void cross(int x, int y, uint16_t color) {
  gfx.drawFastHLine(x-3, y, 7, color);
  gfx.drawFastVLine(x, y-3, 7, color);
}
static void heart(int x, int y, uint16_t col) {
  gfx.fillCircle(x-4,y-3,5,col);
  gfx.fillCircle(x+4,y-3,5,col);
  gfx.fillTriangle(x-9,y-2,x+9,y-2,x,y+12,col);
}
static void drawFace(int cx, int cy, uint32_t now, Mood m) {
  const bool blink = (m == ASLEEP) || (now % 3800 < 155);
  const int look = ((now/4400)%3 == 0) ? -2 : ((now/4400)%3==1) ? 2 : 0;
  if (blink) {
    gfx.drawFastHLine(cx-24,cy,15,TFT_BLACK);
    gfx.drawFastHLine(cx+9,cy,15,TFT_BLACK);
  } else if (m == SICK) {
    gfx.drawLine(cx-23,cy-5,cx-12,cy+6,TFT_BLACK);
    gfx.drawLine(cx-23,cy+6,cx-12,cy-5,TFT_BLACK);
    gfx.drawLine(cx+12,cy-5,cx+23,cy+6,TFT_BLACK);
    gfx.drawLine(cx+12,cy+6,cx+23,cy-5,TFT_BLACK);
  } else {
    gfx.fillRoundRect(cx-23+look,cy-6,13,17,5,TFT_BLACK);
    gfx.fillRoundRect(cx+10+look,cy-6,13,17,5,TFT_BLACK);
    gfx.fillCircle(cx-18+look,cy-2,2,TFT_WHITE);
    gfx.fillCircle(cx+15+look,cy-2,2,TFT_WHITE);
  }
  if (m == ASLEEP) {
    gfx.drawFastHLine(cx-7,cy+26,14,TFT_BLACK);
  } else if (m == SAD || m == SICK) {
    gfx.drawLine(cx-11,cy+31,cx,cy+23,TFT_BLACK);
    gfx.drawLine(cx,cy+23,cx+11,cy+31,TFT_BLACK);
    if (m == SAD && (now % 2300 < 700)) {
      gfx.fillCircle(cx+24,cy+18,3,TFT_BLUE);
    }
  } else if (m == HAPPY) {
    gfx.drawLine(cx-12,cy+20,cx,cy+33,TFT_BLACK);
    gfx.drawLine(cx,cy+33,cx+12,cy+20,TFT_BLACK);
  } else {
    gfx.drawFastHLine(cx-9,cy+26,18,TFT_BLACK);
  }
}
static void drawEgg(uint32_t now) {
  int wobble = ((now / 230) % 2) ? -3 : 3;
  const int cx = 67+wobble;
  gfx.fillRoundRect(cx-42,53,84,132,40,TFT_WHITE);
  gfx.fillRoundRect(cx-36,59,72,120,36,TFT_LIGHTGREY);
  gfx.fillEllipse(cx-9,92,18,10,TFT_WHITE);
  // Petite forme visible sous la coquille
  gfx.fillCircle(cx-15,127,5,TFT_BLACK);
  gfx.fillCircle(cx+15,127,5,TFT_BLACK);
  gfx.drawFastHLine(cx-7,146,14,TFT_BLACK);
  if (pet.ageSec > 40 && (now / 350)%2==0) {
    gfx.drawLine(cx-12,65,cx-2,74,TFT_DARKGREY);
    gfx.drawLine(cx-2,74,cx+9,66,TFT_DARKGREY);
  }
}
static void drawCreature(uint32_t now) {
  const uint8_t st = stage();
  const Mood m = mood();
  const bool hop = (!pet.dort && (now/7200)%4==2);
  const int bob = pet.dort ? 5 : hop ? ((now/160)%2 ? -11 : -2) : ((now/340)%2 ? -2 : 2);
  const int wander = (st > 0 && !pet.dort && (now/7200)%4==1)
                   ? static_cast<int>(sinf(now/650.0f)*9.0f) : 0;
  int cx = 67 + wander;
  const bool dance = (!pet.dort && pet.joie >= 60 &&
                      (now / 12000) % 5 == 3);
  const bool wave = (!pet.dort && (((now/7200)%4==3) || dance));
  uint16_t body = st == 1 ? TFT_CYAN : st == 2 ? TFT_GREEN :
                  friendship >= 75 ? TFT_MAGENTA : TFT_SKYBLUE;
  if (m == SICK) body = TFT_LIGHTGREY;
  else if (st == 3 && m == SAD) body = TFT_ORANGE;

  if (st == 0) {
    drawEgg(now);
    return;
  }

  const int bx = st == 1 ? 29 : st == 2 ? 19 : 13;
  const int width = st == 1 ? 77 : st == 2 ? 97 : 108;
  const int by = (st == 1 ? 77 : st == 2 ? 61 : 47) + bob;
  const int height = st == 1 ? 102 : st == 2 ? 119 : 135;
  const int bodyx = cx - width/2;
  const int mouthY = by + (st == 1 ? 37 : 51);

  if (st >= 2) {
    // Petites antennes / oreilles evolutives
    gfx.fillRoundRect(cx-41,by-18,21,31,7,body);
    gfx.fillRoundRect(cx+20,by-18,21,31,7,body);
    gfx.fillCircle(cx-31,by-15,5,TFT_PINK);
    gfx.fillCircle(cx+31,by-15,5,TFT_PINK);
  }
  if (st == 3) {
    gfx.fillRoundRect(cx-15,by-25,30,25,9,TFT_YELLOW);
    gfx.fillCircle(cx,by-25,6,TFT_ORANGE);
  }

  // Jambes et bras avec mouvements.
  int legShift = (now/290)%2 ? 5 : -5;
  if (pet.dort) legShift = 0;
  gfx.fillRoundRect(cx-38+legShift,by+height-13,29,20,8,body);
  gfx.fillRoundRect(cx+9-legShift,by+height-13,29,20,8,body);
  gfx.fillRoundRect(cx-width/2-9,by+55 + (wave ? -17 : 0),23,45,10,body);
  gfx.fillRoundRect(cx+width/2-14,by+55 - (wave ? 17 : 0),23,45,10,body);
  gfx.fillRoundRect(bodyx,by,width,height,st == 1 ? 33 : 40,body);

  if (st >= 2) {
    gfx.fillRoundRect(cx-22,by+76,44,36,18,TFT_WHITE);
    gfx.fillRoundRect(cx-15,by+83,30,23,12,TFT_LIGHTGREY);
  }
  gfx.fillCircle(cx-31,by+68,7,TFT_PINK);
  gfx.fillCircle(cx+31,by+68,7,TFT_PINK);
  drawFace(cx,mouthY,now,m);

  if (wave) {
    cross(cx+width/2+10,by+41, TFT_YELLOW);
  }
  if (dance) {
    // Notes de musique : danse spontanee lorsque Tama est heureux.
    gfx.drawFastVLine(13,65,17,TFT_YELLOW);
    gfx.fillCircle(10,83,5,TFT_YELLOW);
    gfx.drawFastVLine(117,53,17,TFT_CYAN);
    gfx.fillCircle(114,72,5,TFT_CYAN);
    gfx.drawFastHLine(117,53,8,TFT_CYAN);
  }
  if (m == ASLEEP) {
    gfx.setTextColor(TFT_WHITE,TFT_BLACK);
    gfx.drawString("Z",cx+37,by+3,2);
    gfx.drawString("z",cx+47,by-9,1);
  } else if (pet.faim < 20 && now%2400 < 900) {
    gfx.drawCircle(cx+44,by+15,6,TFT_ORANGE);
  }
}
static void drawFX(uint32_t now) {
  if (effect == FX_NONE || static_cast<int32_t>(now-effectUntil) >= 0) return;
  const int drift = static_cast<int>((now/130)%45);
  switch (effect) {
    case FX_FOOD:
      gfx.fillCircle(104,98+drift/3,11,TFT_ORANGE);
      gfx.fillCircle(106,95+drift/3,3,TFT_YELLOW);
      break;
    case FX_CLEAN:
      for (int i=0;i<6;i++) {
        int x = 14 + (i*23)%111;
        int y = 158 - (drift + i*29)%112;
        gfx.drawCircle(x,y,4+(i%3)*3,TFT_CYAN);
      }
      break;
    case FX_HEAL:
      cross(22,81-drift, TFT_GREEN);
      cross(116,97-drift, TFT_GREEN);
      cross(66,49-drift/2, TFT_WHITE);
      break;
    case FX_GIFT:
    case FX_PLAY:
    case FX_HUG:
      heart(23,85-drift,TFT_PINK);
      heart(105,73-drift/2,TFT_RED);
      if (effect == FX_HUG) heart(67,43-drift/3,TFT_PINK);
      break;
    case FX_SURPRISE:
      cross(18,53+drift,TFT_YELLOW);
      cross(110,61+drift/2,TFT_CYAN);
      break;
    case FX_GROW:
    case FX_WAKE:
      cross(16,52+drift,TFT_YELLOW);
      cross(113,50+drift,TFT_CYAN);
      cross(80,35+drift,TFT_WHITE);
      break;
    default: break;
  }
}
static const char* idleSpeech() {
  if (pet.dort) return "Zzz...";
  if (pet.sante < 25) return "PAS EN FORME...";
  if (pet.faim < 22) return "J'AI FAIM !";
  if (pet.joie < 25) return "ON JOUE ?";
  if (pet.proprete < 19) return "UN BAIN ?";
  if (pet.energie < 15) return "SOMMEIL...";
  if (friendship >= 80 && (millis()/11000)%4 == 0) return "MON MEILLEUR AMI !";
  if ((millis()/11000)%4==0) return "COUCOU !";
  if ((millis()/11000)%4==1) return "...";
  if ((millis()/11000)%4==2) return "ON FAIT QUOI ?";
  return "JE SUIS BIEN !";
}
static void drawHome(uint32_t now) {
  gfx.fillSprite(TFT_BLACK);
  // Aucune barre d'etat en haut : une scene entierement reservee au pet.
  for (int i=0;i<4;i++) {
    int x = 12+i*34, y = 30 + (i%2)*22;
    if ((now/550+i)%4==0) gfx.drawPixel(x,y,TFT_DARKGREY);
  }
  gfx.drawFastHLine(12,189,111,TFT_DARKGREY);
  gfx.fillEllipse(67,191,38,3,TFT_DARKGREY);
  drawCreature(now);
  drawFX(now);

  const char* label = static_cast<int32_t>(now-speechUntil)<0 ? speech : idleSpeech();
  // Une bulle discrete, sans ecraser le personnage.
  gfx.fillRoundRect(8,5,119,23,9,TFT_DARKGREY);
  gfx.setTextColor(TFT_WHITE,TFT_DARKGREY);
  gfx.drawCentreString(label,67,12,1);

  displayBatteryIcon();
  // Menu seul : aucun numero de GPIO, on libere le bas de l'ecran.
  gfx.fillRoundRect(4,210,127,26,8,TFT_NAVY);
  gfx.drawRoundRect(4,210,127,26,8,TFT_CYAN);
  gfx.setTextColor(TFT_CYAN,TFT_NAVY);
  gfx.drawCentreString(MENU[selected],67,215,2);
}
static uint16_t colorFor(uint8_t val) {
  return val<25 ? TFT_RED : val<55 ? TFT_ORANGE : TFT_GREEN;
}
static void drawGauge(int y,const char* title,uint8_t value) {
  gfx.setTextColor(TFT_WHITE,TFT_BLACK);
  gfx.drawString(title,9,y,1);
  gfx.drawRoundRect(9,y+13,91,10,2,TFT_DARKGREY);
  int w = (static_cast<int>(value)*87)/100;
  if(w>0) gfx.fillRect(11,y+15,w,6,colorFor(value));
  char pct[9];
  snprintf(pct,sizeof(pct),"%u%%",(unsigned)value);
  gfx.drawRightString(pct,126,y+13,1);
}
static void drawInfos() {
  gfx.fillSprite(TFT_BLACK);
  gfx.drawRoundRect(2,2,131,236,8,TFT_CYAN);
  gfx.setTextColor(TFT_CYAN,TFT_BLACK);
  gfx.drawCentreString("INFOS",67,8,2);
  char age[36];
  snprintf(age,sizeof(age),"%s - %lum",stageLabel(),(unsigned long)(pet.ageSec/60));
  gfx.setTextColor(TFT_WHITE,TFT_BLACK);
  gfx.drawCentreString(age,67,30,1);
  drawGauge(49,"FAIM",pet.faim);
  drawGauge(77,"JOIE",pet.joie);
  drawGauge(105,"ENERGIE",pet.energie);
  drawGauge(133,"PROPRETE",pet.proprete);
  drawGauge(161,"SANTE",pet.sante);
  gfx.setTextColor(TFT_LIGHTGREY,TFT_BLACK);
  char score[45];
  snprintf(score,sizeof(score),"JEUX %u  PARFAITS %u",(unsigned)gamePlayed,(unsigned)gameWins);
  gfx.drawCentreString(score,67,191,1);
  char batt[32];
  if (batteryMv)
    snprintf(batt,sizeof(batt),"BAT %u%%  %u.%02uV",(unsigned)batteryPercent,
             (unsigned)(batteryMv/1000), (unsigned)((batteryMv%1000)/10));
  else
    snprintf(batt,sizeof(batt),"BAT : INDISPONIBLE");
  gfx.setTextColor(batteryMv && batteryMv<3600 ? TFT_ORANGE : TFT_LIGHTGREY,TFT_BLACK);
  gfx.drawCentreString(batt,67,208,1);
  gfx.drawCentreString("SUIVANT      RETOUR",67,225,1);
}
static void drawSouvenirs() {
  gfx.fillSprite(TFT_BLACK);
  gfx.drawRoundRect(2,2,131,236,8,TFT_MAGENTA);
  gfx.setTextColor(TFT_MAGENTA,TFT_BLACK);
  gfx.drawCentreString("SOUVENIRS",67,9,2);
  gfx.setTextColor(TFT_WHITE,TFT_BLACK);
  gfx.drawCentreString("AMITIE",67,43,2);
  gfx.drawRoundRect(11,66,113,15,5,TFT_DARKGREY);
  gfx.fillRoundRect(14,69,friendship * 107 / 100,9,3,TFT_PINK);
  char line[46];
  snprintf(line,sizeof(line),"%u / 100",(unsigned)friendship);
  gfx.drawCentreString(line,67,86,1);
  snprintf(line,sizeof(line),"CALINS : %u",(unsigned)hugCount);
  gfx.drawCentreString(line,67,116,1);
  snprintf(line,sizeof(line),"SURPRISES : %u",(unsigned)surpriseCount);
  gfx.drawCentreString(line,67,134,1);
  snprintf(line,sizeof(line),"ETOILE : %u/%u",(unsigned)gameWins,(unsigned)gamePlayed);
  gfx.drawCentreString(line,67,152,1);
  snprintf(line,sizeof(line),"REFLEXE : %u/%u",(unsigned)reflexWins,(unsigned)reflexPlayed);
  gfx.drawCentreString(line,67,170,1);
  gfx.setTextColor(TFT_YELLOW,TFT_BLACK);
  gfx.drawCentreString(friendship>=80?"AMIS INSEPARABLES":friendship>=50?"BONS COPAINS":"ON SE DECOUVRE",67,201,1);
  gfx.setTextColor(TFT_LIGHTGREY,TFT_BLACK);
  gfx.drawCentreString("SUIVANT      RETOUR",67,221,1);
}
static void drawStar(int x,int y,uint16_t color) {
  gfx.fillTriangle(x,y-12,x-5,y-3,x+5,y-3,color);
  gfx.fillTriangle(x-11,y-2,x+11,y-2,x,y+10,color);
}
static void drawGame(uint32_t now) {
  gfx.fillSprite(TFT_BLACK);
  gfx.drawRoundRect(3,3,129,234,8,TFT_CYAN);
  gfx.setTextColor(TFT_CYAN,TFT_BLACK);
  gfx.drawCentreString("ATTRAPE",67,17,2);
  gfx.drawCentreString("L'ETOILE !",67,40,2);
  char tries[35];
  snprintf(tries,sizeof(tries),"ESSAI %u / 3",(unsigned)(gameRound+1));
  gfx.setTextColor(TFT_WHITE,TFT_BLACK);
  gfx.drawCentreString(tries,67,70,1);
  gfx.drawRoundRect(12,121,111,28,6,TFT_DARKGREY);
  gfx.fillRect(55,124,24,22,TFT_GREEN); // zone de reussite
  int x = starX(now);
  drawStar(x,134,TFT_YELLOW);
  if (static_cast<int32_t>(now-gamePauseUntil)<0) {
    gfx.drawCentreString(lastHit?"BRAVO !":"RATE !",67,172,2);
  } else {
    gfx.drawCentreString("ATTRAPE !",67,171,2);
  }
  gfx.setTextColor(TFT_LIGHTGREY,TFT_BLACK);
  gfx.drawCentreString("RETOUR",67,216,1);
}
static void drawReflex(uint32_t now) {
  gfx.fillSprite(TFT_BLACK);
  gfx.drawRoundRect(3,3,129,234,8,TFT_YELLOW);
  gfx.setTextColor(TFT_YELLOW,TFT_BLACK);
  gfx.drawCentreString("REFLEXE",67,20,2);
  char roundText[25];
  snprintf(roundText,sizeof(roundText),"ESSAI %u / 3",(unsigned)(reflexRound+1));
  gfx.setTextColor(TFT_WHITE,TFT_BLACK);
  gfx.drawCentreString(roundText,67,65,1);
  const bool paused = (int32_t)(now-reflexPauseUntil)<0;
  const bool ready = !paused && (int32_t)(now-reflexReadyAt)>=0 &&
                     (uint32_t)(now-reflexReadyAt)<=700;
  gfx.fillRoundRect(14,103,107,54,12,ready?TFT_GREEN:paused?(reflexLastHit?TFT_CYAN:TFT_RED):TFT_DARKGREY);
  gfx.setTextColor(ready?TFT_BLACK:TFT_WHITE,ready?TFT_GREEN:paused?(reflexLastHit?TFT_CYAN:TFT_RED):TFT_DARKGREY);
  gfx.drawCentreString(paused?(reflexLastHit?"BRAVO !":"RATE !"):ready?"TOP !":"ATTENDS",67,119,2);
  gfx.setTextColor(TFT_LIGHTGREY,TFT_BLACK);
  gfx.drawCentreString("VALIDE AU VERT !",67,186,1);
  gfx.drawCentreString("RETOUR",67,215,1);
}
void setup() {
  // Relacher les verrous du retroeclairage apres un reveil deep sleep.
  gpio_deep_sleep_hold_dis();
  gpio_hold_dis(GPIO_NUM_4);
  pinMode(PIN_NEXT,INPUT);          // Pull-up physique sur T-Display V1.1
  pinMode(PIN_OK,INPUT_PULLUP);
  pinMode(PIN_LIGHT,OUTPUT);
  digitalWrite(PIN_LIGHT,LOW);
  pinMode(PIN_BAT_ENABLE,OUTPUT);
  digitalWrite(PIN_BAT_ENABLE,LOW);
  pinMode(PIN_BAT_ADC,INPUT);
  analogReadResolution(12);
  analogSetPinAttenuation(PIN_BAT_ADC, ADC_11db);
  nextKey.pin=PIN_NEXT;
  okKey.pin=PIN_OK;

  load();
  refreshBattery();
  // Aucun affichage/jeu tant que la batterie ne s'est pas suffisamment rechargee.
  if (batteryHibernating) {
    if (batteryMv && batteryMv < BAT_RESUME_MV) hibernateBattery();
    batteryHibernating = false;
  } else if (batteryMv && batteryMv < BAT_SLEEP_MV) {
    hibernateBattery();
  }

  lcd.init();
  lcdReady = true;
  lcd.setRotation(0);              // Portrait 135x240
  lcd.fillScreen(TFT_BLACK);
  digitalWrite(PIN_LIGHT,HIGH);
  gfx.setColorDepth(16);
  if (gfx.createSprite(SW,SH) == nullptr) {
    lcd.setTextColor(TFT_WHITE,TFT_BLACK);
    lcd.drawCentreString("ERREUR RAM",67,112,1);
    while(true) delay(1000);
  }
  gfx.setTextDatum(TL_DATUM);
  randomSeed((uint32_t)micros() ^ (uint32_t)esp_random());
  lastSecond=millis();
  lastSave=millis();
  lastInteraction=millis();
  say("BONJOUR !",FX_WAKE,1800);
}
void loop() {
  const uint32_t now=millis();
  const bool next=nextKey.readPress(now);
  const bool ok=okKey.readPress(now);
  const bool wasOff = displayOff;
  if (next || ok) {
    lastInteraction=now;
    if (wasOff) wakeDisplay();  // Premier appui = reveil, sans action involontaire.
  }
  if (!wasOff) {
    if(page==HOME) {
      if(next) selected=(selected+1)%MENU_COUNT;
      if(ok) chooseAction(now);
    } else if(page==INFOS) {
      if(ok) page=HOME;
      else if(next) page=SOUVENIRS;
    } else if(page==SOUVENIRS) {
      if(ok) page=HOME;
      else if(next) page=INFOS;
    } else if(page==MINIGAME) {
      if(next) { page=HOME; say("UNE AUTRE FOIS !",FX_NONE,1500); }
      else if(ok) gamePress(now);
    } else if(page==REFLEXGAME) {
      if(next) { page=HOME; say("UNE AUTRE FOIS !",FX_NONE,1500); }
      else if(ok) reflexPress(now);
    }
  }

  while((uint32_t)(now-lastSecond)>=1000) {
    lastSecond+=1000;
    tickSecond();
  }
  trySurprise();
  if((uint32_t)(now-lastSave)>=60000) save();

  if((uint32_t)(now-lastBatteryCheck)>=BAT_CHECK_MS) {
    refreshBattery();
    if (batteryMv && batteryMv < BAT_SLEEP_MV) {
      if (++consecutiveLow >= 3) {
        say("BATTERIE FAIBLE",FX_NONE,1000);
        hibernateBattery();
      }
    } else {
      consecutiveLow=0;
    }
  }
  // Ecran endormi seulement : Tama continue de vivre et se sauvegarde.
  if (!displayOff && (uint32_t)(now-lastInteraction)>=DISPLAY_IDLE_MS) {
    if(page==MINIGAME || page==REFLEXGAME) page=HOME;
    shutDisplay();
  }
  if(!displayOff && (uint32_t)(now-lastFrame)>=95) {
    lastFrame=now;
    if(page==INFOS) drawInfos();
    else if(page==SOUVENIRS) drawSouvenirs();
    else if(page==MINIGAME) drawGame(now);
    else if(page==REFLEXGAME) drawReflex(now);
    else drawHome(now);
    gfx.pushSprite(0,0);
  }
  delay(2);
}
