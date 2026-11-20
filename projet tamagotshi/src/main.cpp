// Tamagotchi V8.1 — meme jeu, deux firmwares : TTGO V1.1 (SPI) et T-Display S3 (parallele).
// Espace graphique logique 135x240 conserve ; S3 agrandi en 170x300 (+ marges).
// Boutons : V1.1 GPIO35/0 ; S3 GPIO14/0.
 // Stockage compatible V2 : namespace "tama_v2", magic 0x54414D32.
#include <Arduino.h>
#include <Preferences.h>
#include <TFT_eSPI.h>
#include <math.h>
#include <esp_sleep.h>
#include <driver/gpio.h>
#ifdef TAMA_S3_LCD
#include <esp_heap_caps.h>
#endif

static TFT_eSPI lcd;

#ifdef TAMA_S3_LCD
#include <esp_partition.h>
class TamaNativeSprite : public TFT_eSprite {
  static int32_t X(int32_t a){return a*170/135;}
  static int32_t Y(int32_t a){return a*320/240;}
  static int32_t W(int32_t a){return max(1,a*170/135);}
  static int32_t H(int32_t a){return max(1,a*320/240);}
  static int32_t R(int32_t a){return max(1,a*170/135);}
public:
  explicit TamaNativeSprite(TFT_eSPI *t):TFT_eSprite(t){}
  void rawPixel(int32_t x,int32_t y,uint16_t c){TFT_eSprite::drawPixel(x,y,c);}
  void drawPixel(int32_t x,int32_t y,uint32_t c){TFT_eSprite::drawPixel(X(x),Y(y),c);}
  void drawLine(int32_t x,int32_t y,int32_t x2,int32_t y2,uint32_t c){TFT_eSprite::drawLine(X(x),Y(y),X(x2),Y(y2),c);}
  void drawFastHLine(int32_t x,int32_t y,int32_t w,uint32_t c){TFT_eSprite::drawFastHLine(X(x),Y(y),W(w),c);}
  void drawFastVLine(int32_t x,int32_t y,int32_t h,uint32_t c){TFT_eSprite::drawFastVLine(X(x),Y(y),H(h),c);}
  void fillRect(int32_t x,int32_t y,int32_t w,int32_t h,uint32_t c){TFT_eSprite::fillRect(X(x),Y(y),W(w),H(h),c);}
  void fillRoundRect(int32_t x,int32_t y,int32_t w,int32_t h,int32_t rad,uint32_t c){TFT_eSprite::fillRoundRect(X(x),Y(y),W(w),H(h),R(rad),c);}
  void drawRoundRect(int32_t x,int32_t y,int32_t w,int32_t h,int32_t rad,uint32_t c){TFT_eSprite::drawRoundRect(X(x),Y(y),W(w),H(h),R(rad),c);}
  void fillCircle(int32_t x,int32_t y,int32_t rad,uint32_t c){TFT_eSprite::fillCircle(X(x),Y(y),R(rad),c);}
  void drawCircle(int32_t x,int32_t y,int32_t rad,uint32_t c){TFT_eSprite::drawCircle(X(x),Y(y),R(rad),c);}
  void fillEllipse(int32_t x,int32_t y,int32_t rx,int32_t ry,uint32_t c){TFT_eSprite::fillEllipse(X(x),Y(y),W(rx),H(ry),c);}
  void drawEllipse(int32_t x,int32_t y,int32_t rx,int32_t ry,uint32_t c){TFT_eSprite::drawEllipse(X(x),Y(y),W(rx),H(ry),c);}
  void fillTriangle(int32_t x,int32_t y,int32_t x2,int32_t y2,int32_t x3,int32_t y3,uint32_t c){TFT_eSprite::fillTriangle(X(x),Y(y),X(x2),Y(y2),X(x3),Y(y3),c);}
  template<class T> int16_t drawString(const T &str,int32_t x,int32_t y,uint8_t font=1){return TFT_eSprite::drawString(str,X(x),Y(y),font);}
  template<class T> int16_t drawCentreString(const T &str,int32_t x,int32_t y,uint8_t font=1){return TFT_eSprite::drawCentreString(str,X(x),Y(y),font);}
  template<class T> int16_t drawRightString(const T &str,int32_t x,int32_t y,uint8_t font=1){return TFT_eSprite::drawRightString(str,X(x),Y(y),font);}
};
#endif

#ifdef TAMA_S3_LCD
static TamaNativeSprite gfx(&lcd);
#else
static TFT_eSprite gfx(&lcd);
#endif
static Preferences prefs;
#ifdef TAMA_S3_LCD
static const uint8_t PIN_NEXT = 14, PIN_OK = 0, PIN_LIGHT = 38;
static const uint8_t PIN_POWER=15, PIN_BAT_ADC=4;
#else
static const uint8_t PIN_NEXT = 35, PIN_OK = 0, PIN_LIGHT = 4;
static const uint8_t PIN_BAT_ENABLE = 14, PIN_BAT_ADC = 34;
#endif
// Seuils conservateurs pour LiPo 1S. Le % reste une estimation.
static const uint16_t BAT_SLEEP_MV = 3480, BAT_RESUME_MV = 3700;
static const uint8_t BAT_HOME_WARNING_PCT = 10; // alerte visuelle avant le deep sleep
// A plus de 4.40V, l'ADC voit probablement le rail USB, PAS la cellule.
static const uint16_t USB_PROBABLE_MV = 4400;
static const uint32_t BAT_CHECK_MS = 30000, DISPLAY_IDLE_MS = 120000;
static const uint64_t BAT_RECHECK_US = 5ULL * 60ULL * 1000000ULL;
RTC_DATA_ATTR static bool batteryHibernating = false;
static uint16_t batteryMv = 0;   // Tension LiPo credible hors charge
static uint16_t rawSupplyMv = 0;
static bool usbProbable = false; // Heuristique, pas une broche VBUS !
static uint8_t batteryPercent = 0, consecutiveLow = 0;
static uint32_t lastBatteryCheck = 0, lastInteraction = 0;
static bool lcdReady = false, displayOff = false;
#ifdef TAMA_S3_LCD
static void drawS3ArtCreature(uint32_t now); // utilise dans drawHome
#endif
static const uint32_t MAGIC = 0x54414D32UL;
static const int SW = 135, SH = 240;
static const char* const MENU[] = {
  "MANGER", "ETOILE", "REFLEXE", "DODO", "BAIN", "SOINS", "CADEAU", "CALIN", "INFOS", "SAC", "MAISON"
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

enum Page : uint8_t { HOME, INFOS, SOUVENIRS, PROFIL, SAC, MINIGAME, REFLEXGAME, ROOM };
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
static uint32_t speechUntil = 0, effectUntil = 0, effectStarted = 0;
static uint32_t lastFrame = 0, lastSecond = 0, lastSave = 0, lastGift = 0;
// V6 : attributs permanents et objets conserves dans le NVS de V5.
static uint8_t personality = 0, adultForm = 0, bagChoice = 0;
static uint8_t homeScene = 0, homeDecor = 0, roomChoice = 0; // jardin/chambre, decoration
// V8 : famille tiree a l'eclosion, puis voie A ou B au passage adolescent.
static uint8_t tamaFamily=0, tamaBranch=0; // 1 Terre/2 Eau/3 Ciel ; 1 A/2 B
static const char* const FAMILY_NAMES[]={"MYSTERE","TERRE","EAU","CIEL"};
// Par famille : bebe, jeune, ado A, adulte A, ado B, adulte B.
static const char* const CREATURE_NAMES[3][6]={
  {"POUSSIN","FEUILLU","BOURGEON","SYLVAN","MOUSSE","FLORALIS"},
  {"GOUTI","NAGEO","AQUILON","MAREON","RIVAGE","LAGRIN"},
  {"PLUMI","AELO","ALIZE","ZEPHYR","EOLE","AERYS"}
};
static uint8_t inventory[3] = {1,0,0}; // biscuit, ballon, doudou
static uint16_t fedCount = 0, careCount = 0;
static const char* const PERSONALITY[] = {"INCONNUE","GOURMAND","JOUEUR","CALIN","REVEUR"};
static const char* const EVOLUTION[] = {"EN COURS","GOURMAND","JOUEUR","CALIN","SAGE"};
static const char* const ITEMS[] = {"BISCUIT","BALLON","DOUDOU","RETOUR"};
static const char* const ROOM_OPTIONS[] = {"JARDIN","CHAMBRE","SANS DECO","FLEURS","BALLON","DOUDOU","RETOUR"};
static const uint8_t ROOM_OPTIONS_COUNT = sizeof(ROOM_OPTIONS) / sizeof(ROOM_OPTIONS[0]);
static bool decorUnlocked(uint8_t decoration) {
  switch(decoration) {
    case 0: return true;
    case 1: return fedCount >= 3; // 3 repas
    case 2: return gameWins >= 1; // ETOILE parfait
    case 3: return reflexWins >= 1 || surpriseCount >= 3; // REFLEXE parfait ou surprises
    default: return false;
  }
}
static void giveItem(uint8_t type) {
  if(type<3 && inventory[type]<9) ++inventory[type];
}
static uint8_t pickAdultForm() {
  const uint32_t play=static_cast<uint32_t>(gamePlayed)+reflexPlayed;
  const uint32_t score[4] = {
    static_cast<uint32_t>(fedCount)*3+(personality==1?2U:0U),
    play*3+gameWins+reflexWins+(personality==2?2U:0U),
    static_cast<uint32_t>(hugCount)*3+(personality==3?2U:0U),
    static_cast<uint32_t>(careCount)*3+(personality==4?2U:0U)
  };
  uint8_t best=0;
  for(uint8_t i=1;i<4;++i) if(score[i]>score[best]) best=i;
  return best+1;
}

static uint8_t clip100(int v) {
  return static_cast<uint8_t>(v < 0 ? 0 : v > 100 ? 100 : v);
}
static uint8_t stage() {
  if (pet.ageSec < 60U) return 0;    // oeuf 1 min (test)
  if (pet.ageSec < 420U) return 1;   // bebe de 1 a 7 min
  if (pet.ageSec < 900U) return 2;   // jeune de 7 a 15 min
  if (pet.ageSec < 1800U) return 3;  // ado de 15 a 30 min
  return 4;                          // adulte, conserve le seuil de la V7.3
}
static const char* stageLabel() {
  switch(stage()) {
    case 0: return "OEUF";
    case 1: return "BEBE";
    case 2: return "JEUNE";
    case 3: return "ADO";
    default:return "ADULTE";
  }
}
static const char* creatureName() {
  if(tamaFamily<1 || tamaFamily>3 || stage()==0) return "OEUF";
  const uint8_t st=stage();
  const uint8_t idx=st==1?0:st==2?1:st==3?(tamaBranch==2?4:2):(tamaBranch==2?5:3);
  return CREATURE_NAMES[tamaFamily-1][idx];
}
// La voie A recompense surtout les jeux, la B les repas, calins et soins.
// Les ecarts faibles sont tires au sort pour ne pas verrouiller une branche.
static uint8_t chooseBranchV8() {
  const uint32_t play=(uint32_t)(gamePlayed+reflexPlayed)*4U+
                      (uint32_t)(gameWins+reflexWins)*2U+
                      (personality==2?2U:0U);
  const uint32_t care=(uint32_t)fedCount*2U+(uint32_t)careCount*3U+
                      (uint32_t)hugCount*2U+(friendship>=80?3U:0U)+
                      (personality==3||personality==4?2U:0U);
  if(play>care+2U) return 1;
  if(care>play+2U) return 2;
  return (uint8_t)random(1,3);
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
  effectStarted = now;
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
  prefs.putUChar("perso",personality);
  prefs.putUChar("form",adultForm);
  prefs.putUShort("feedn",fedCount);
  prefs.putUShort("caren",careCount);
  prefs.putUChar("item0",inventory[0]);
  prefs.putUChar("item1",inventory[1]);
  prefs.putUChar("item2",inventory[2]);
  prefs.putUChar("room",homeScene);
  prefs.putUChar("decor",homeDecor);
  prefs.putUChar("family",tamaFamily);
  prefs.putUChar("branch",tamaBranch);
  prefs.putUChar("evo_v8",1); // distinguer migration V7 et nouveau cycle
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
    personality=prefs.getUChar("perso",0);
    adultForm=prefs.getUChar("form",0);
    if(personality>4) personality=0;
    if(adultForm>4) adultForm=0;
    fedCount=prefs.getUShort("feedn",0);
    careCount=prefs.getUShort("caren",0);
    inventory[0]=min((int)prefs.getUChar("item0",1),9);
    inventory[1]=min((int)prefs.getUChar("item1",0),9);
    inventory[2]=min((int)prefs.getUChar("item2",0),9);
    homeScene=prefs.getUChar("room",0);
    homeDecor=prefs.getUChar("decor",0);
    if(homeScene>1) homeScene=0;
    if(homeDecor>3 || !decorUnlocked(homeDecor)) homeDecor=0;
    if(prefs.getUChar("evo_v8",0)==1) {
      tamaFamily=prefs.getUChar("family",0);
      tamaBranch=prefs.getUChar("branch",0);
      if(tamaFamily>3) tamaFamily=0;
      if(tamaBranch>2) tamaBranch=0;
    }
  } else {
    lastSurpriseAge = pet.ageSec;
    save();
  }
}
// Lecture du pont diviseur 1:2 de la version originale (GPIO14 HIGH -> GPIO34 ADC).
// Une valeur hors plage est refusee pour eviter un sommeil intempestif.
static uint16_t sampleBatteryMv() {
#ifdef TAMA_S3_LCD
  // GPIO4 est relie a VBAT par diviseur x2 sur le LCD 1,9".
  // D'apres LilyGO l'ADC n'est PAS fiable en USB-C : lecture 0 = inconnue.
  (void)analogReadMilliVolts(PIN_BAT_ADC);
#else
  // Sur l'ancien V1.1 GPIO14 doit rester HIGH sur batterie.
  digitalWrite(PIN_BAT_ENABLE,HIGH);
  delay(12);
  (void)analogReadMilliVolts(PIN_BAT_ADC);
#endif
  uint16_t samples[12];
  for(uint8_t n=0;n<12;n++) {
    samples[n]=analogReadMilliVolts(PIN_BAT_ADC);
    delay(2);
  }
  for(uint8_t i=0;i<12;++i) for(uint8_t j=i+1;j<12;++j) {
    if(samples[j]<samples[i]) {
      const uint16_t t=samples[i];samples[i]=samples[j];samples[j]=t;
    }
  }
  const uint32_t v=(uint32_t)samples[3]*2U;
#ifdef TAMA_S3_LCD
  // Ne jamais prendre une mesure incoherente sous USB pour une batterie vide.
  if(v<3000U || v>4400U) return 0;
#else
  if(v<2600U || v>5400U) return 0;
#endif
  return (uint16_t)v;
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
  rawSupplyMv = sampleBatteryMv();
  // La cellule 1S ne monte normalement pas au-dessus de 4.2V.
  // Une tension >4.4V indique probablement le secteur USB sur ce modele.
#ifdef TAMA_S3_LCD
  // Pas de mesure VBUS sur cette carte. Une tension VBAT invalide lorsque
  // le MCU fonctionne signifie alimentation USB/externe PROBABLE uniquement.
  usbProbable = (rawSupplyMv == 0);
#else
  usbProbable = rawSupplyMv > USB_PROBABLE_MV;
#endif
  batteryMv = usbProbable ? 0 : rawSupplyMv;
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
// Page d'alerte autonome, visible 2 secondes, meme lors d'un reveil bouton.
static void showEmptyBattery() {
  if (!lcdReady) {
    lcd.init();
    lcdReady = true;
    lcd.setRotation(0);
  }
  if (displayOff) wakeDisplay();
  digitalWrite(PIN_LIGHT, HIGH);
  lcd.fillScreen(TFT_BLACK);
  lcd.drawRoundRect(20,72,91,62,9,TFT_RED);
  lcd.fillRect(111,92,8,22,TFT_RED);
  lcd.fillRect(31,83,11,40,TFT_RED); // Reserve minimale
  lcd.drawLine(53,83,87,122,TFT_RED);
  lcd.drawLine(87,83,53,122,TFT_RED);
  lcd.setTextColor(TFT_RED,TFT_BLACK);
  lcd.drawCentreString("BATTERIE VIDE",67,150,2);
  lcd.setTextColor(TFT_LIGHTGREY,TFT_BLACK);
  lcd.drawCentreString("RECHARGE MOI !",67,174,1);
  delay(2000);
}
#ifndef TAMA_S3_LCD
static void hibernateBattery(bool showWarning=false) {
  // Ecrire en NVS une fois seulement, pas toutes les 5 min.
  if (!batteryHibernating) save();
  batteryHibernating = true;
  if (showWarning) showEmptyBattery();
  shutDisplay();
  digitalWrite(PIN_LIGHT, LOW);
  // Ne pas laisser GPIO35 au niveau bas pendant l'entree en deep sleep,
  // sinon le bouton provoquerait une boucle de reveils instantanes.
  while(digitalRead(PIN_NEXT)==LOW) delay(15);
  gpio_hold_en(GPIO_NUM_4);
  gpio_deep_sleep_hold_en();
  esp_sleep_enable_timer_wakeup(BAT_RECHECK_US);
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_35, 0);
  // La ligne GPIO14 se met LOW uniquement lors de l'hibernation.
  digitalWrite(PIN_BAT_ENABLE, LOW);
  delay(12);
  esp_deep_sleep_start();
}
#endif  // TAMA_S3_LCD : hibernation basse tension a valider avant activation
static void displayBatteryIcon() {
  // Accueil volontairement epure : aucune jauge permanente ni indication USB.
  // A 10 % ou moins, une batterie rouge VIDE apparait, sans pourcentage.
  // Ne rien afficher si la mesure est indisponible ou si USB est probable.
  if (!batteryMv || usbProbable || batteryPercent > BAT_HOME_WARNING_PCT) return;
  const int x=109, y=196;
  gfx.drawRoundRect(x,y,18,11,2,TFT_RED);
  gfx.fillRect(x+18,y+3,3,5,TFT_RED);
}

// Animation quand une alimentation USB est probable et Tama inutilise.
static void drawUsbIdle(uint32_t now) {
  gfx.fillSprite(TFT_BLACK);
  gfx.setTextColor(TFT_CYAN,TFT_BLACK);
  gfx.drawCentreString("USB",67,30,2);
  gfx.drawRoundRect(17,86,101,67,12,TFT_CYAN);
  gfx.fillRect(118,108,8,23,TFT_CYAN);
  const int step = (now/650)%5;
  for(int i=0;i<4;i++) {
    const uint16_t c = i<=step ? TFT_GREEN : TFT_DARKGREY;
    gfx.fillRoundRect(26+i*22,97,17,46,4,c);
  }
  gfx.fillTriangle(64,99,53,125,67,119,TFT_YELLOW);
  gfx.fillTriangle(68,110,82,110,59,137,TFT_YELLOW);
  gfx.setTextColor(TFT_LIGHTGREY,TFT_BLACK);
  gfx.drawCentreString("ALIMENTATION USB",67,173,1);
  gfx.drawCentreString("APPUYER POUR JOUER",67,204,1);
}
// NOTE: Impossible d'affirmer "CHARGE TERMINEE" sans le signal CHRG
// du TP4054 ; GPIO34 mesure le rail d'alimentation en USB.
static void tickSecond() {
  const uint8_t oldStage = stage();
  ++pet.ageSec;
  if (pet.ageSec % (personality==1?35U:42U) == 0) pet.faim = clip100(pet.faim-1);
  if (pet.ageSec % (personality==2?45U:55U) == 0) pet.joie = clip100(pet.joie-1);
  if (pet.ageSec % 75 == 0) pet.proprete = clip100(pet.proprete-1);
  if (pet.dort) {
    if (pet.ageSec % 7 == 0) pet.energie = clip100(pet.energie+(personality==4?2:1));
    if (pet.energie >= 100) {
      pet.dort = false;
      say("BIEN DORMI !", FX_WAKE);
    }
  } else if (pet.ageSec % (personality==4?65U:50U) == 0) {
    pet.energie = clip100(pet.energie-1);
  }
  if (pet.ageSec % (personality==3?240U:180U) == 0 && friendship > 0) {
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
  if(stage()!=oldStage) {
    if(stage()==1) {
      if(tamaFamily==0) tamaFamily=(uint8_t)random(1,4); // chances 1/3
      if(personality==0) personality=(uint8_t)random(1,5);
      say(tamaFamily==1?"BONJOUR, TERRE !":tamaFamily==2?"BONJOUR, EAU !":"BONJOUR, CIEL !",FX_GROW,3700);
    } else if(stage()==3) {
      if(tamaBranch==0) tamaBranch=chooseBranchV8();
      say(tamaBranch==1?"VOIE A !":"VOIE B !",FX_GROW,3600);
    } else if(stage()==4) {
      if(tamaBranch==0) tamaBranch=chooseBranchV8();
      if(adultForm==0) adultForm=pickAdultForm(); // compatibilite sauvegarde V6
      say("MA FORME ADULTE !",FX_GROW,3600);
    } else {
      say("J'AI GRANDI !",FX_GROW,3300);
    }
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
    if (gameHits == 3) { ++gameWins; giveItem(1); }
    friendship = clip100(friendship + gameHits * 3);
    page = HOME;
    say(gameHits == 3 && gameWins==1 ? "DECO BALLON !":gameHits == 3 ? "GAGNE : BALLON !" : gameHits ? "BIEN JOUE !" : "ON REESSAIE ?", FX_PLAY, 2900);
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
    if (reflexHits == 3) { ++reflexWins; giveItem(2); }
    pet.joie = clip100(pet.joie + 5 + 9 * reflexHits);
    pet.energie = clip100(pet.energie - 6);
    friendship = clip100(friendship + reflexHits * 3);
    page = HOME;
    say(reflexHits == 3 && reflexWins==1 ? "DECO DOUDOU !" : reflexHits == 3 ? "GAGNE : DOUDOU !" : reflexHits ? "BIEN JOUE !" : "ON REESSAIE ?", FX_PLAY, 2900);
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
      giveItem(static_cast<uint8_t>(random(0,3)));
      pet.joie=clip100(pet.joie+5);
      say("OBJET TROUVE !",FX_GIFT,2600);
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
  if(surpriseCount==3 && reflexWins==0) say("DECO DOUDOU !",FX_GIFT,3000);
  save();
}
static void chooseAction(uint32_t now) {
  switch(selected) {
    case 0:
      pet.dort = false;
      pet.faim = clip100(pet.faim + (personality==1?33:26));
      if(fedCount<65535) ++fedCount;
      pet.proprete = clip100(pet.proprete - 3);
      pet.sante = clip100(pet.sante + 2);
      say(fedCount==3?"FLEURS DEBLOQUEES !":"MIAM MIAM !", FX_FOOD, 3400);
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
      if(careCount<65535) ++careCount;
      pet.joie = clip100(pet.joie + 4);
      say("TOUT PROPRE !", FX_CLEAN, 3600);
      break;
    case 5:
      pet.sante = clip100(pet.sante + 25);
      if(careCount<65535) ++careCount;
      pet.joie = clip100(pet.joie - 2);
      say("CA VA MIEUX !", FX_HEAL, 3100);
      break;
    case 6:
      if ((int32_t)(now - lastGift) < 25000 && lastGift != 0) {
        say("PATIENCE !", FX_NONE, 1100);
        return;
      }
      lastGift = now;
      pet.joie = clip100(pet.joie + 18);
      friendship = clip100(friendship + 4);
      say("OH MERCI !", FX_GIFT, 3500);
      break;
    case 7:
      pet.dort = false;
      pet.joie = clip100(pet.joie + 12);
      friendship = clip100(friendship + (personality==3?16:10));
      ++hugCount;
      say("GROS CALIN !", FX_HUG, 3400);
      break;
    case 8: page=INFOS; return;
    case 9: bagChoice=0; page=SAC; return;
    case 10: roomChoice=0; page=ROOM; return;
  }
  save();
}
static void useBagItem() {
  if(bagChoice==3) { page=HOME; return; }
  if(bagChoice>=3 || inventory[bagChoice]==0) {
    say("SAC VIDE !",FX_NONE,1400);
    return;
  }
  --inventory[bagChoice];
  pet.dort=false;
  if(bagChoice==0) {
    pet.faim=clip100(pet.faim+23);
    say("UN BISCUIT !",FX_FOOD,3300);
  } else if(bagChoice==1) {
    pet.joie=clip100(pet.joie+26);
    pet.energie=clip100(pet.energie-3);
    say("ON JOUE !",FX_PLAY,3300);
  } else {
    pet.energie=clip100(pet.energie+20);
    friendship=clip100(friendship+12);
    say("MON DOUDOU !",FX_HUG,3300);
  }
  page=HOME;
  save();
}

static void chooseRoom() {
  if(roomChoice==ROOM_OPTIONS_COUNT-1) {
    page=HOME;
    return;
  }
  if(roomChoice<=1) {
    homeScene=roomChoice;
    say(homeScene?"DANS MA CHAMBRE !":"AU JARDIN !",FX_SURPRISE,2200);
  } else {
    const uint8_t requested=roomChoice-2;
    if(!decorUnlocked(requested)) return; // Objectif de debloquage visible dans le menu.
    homeDecor=requested;
    say(homeDecor?"NOUVELLE DECO !":"DECO RANGEE !",FX_SURPRISE,2300);
  }
  page=HOME;
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
// L'oeuf commun a toutes les familles, tachete et anime, avant la naissance.
static uint16_t rgb565(uint8_t r,uint8_t g,uint8_t b);
static void drawEgg(uint32_t now) {
  const uint32_t age=pet.ageSec;
  const int shake=age<22?0:age<38?((now/210UL)%2?2:-2):((now/98UL)%2?4:-4);
  const int cx=67+shake;
  const int cy=142+(age>40 && (now/280UL)%2?2:0);
  const uint16_t shell=rgb565(255,244,211);
  const uint16_t pink=rgb565(246,145,185);
  const uint16_t edge=rgb565(142,112,127);
  // L'oeuf a une silhouette creme avec taches roses.
  gfx.fillEllipse(cx,cy,39,49,edge);
  gfx.fillEllipse(cx,cy-1,36,47,shell);
  gfx.fillEllipse(cx-17,cy-23,9,7,TFT_WHITE);
  gfx.fillCircle(cx-18,cy+4,8,pink);
  gfx.fillCircle(cx+19,cy-18,9,pink);
  gfx.fillCircle(cx+14,cy+26,7,pink);
  gfx.fillCircle(cx-5,cy-31,5,pink);
  gfx.fillCircle(cx-22,cy+28,5,pink);
  gfx.fillCircle(cx+25,cy+5,5,pink);
  // Debut de l'eclosion pendant les 20 dernieres secondes.
  if(age>=41) {
    gfx.drawLine(cx-17,cy-12,cx-7,cy-4,edge);
    gfx.drawLine(cx-7,cy-4,cx+2,cy-13,edge);
    gfx.drawLine(cx+2,cy-13,cx+10,cy-5,edge);
  }
  if(age>=52) {
    gfx.drawLine(cx+10,cy-5,cx+24,cy-18,edge);
    gfx.drawLine(cx-7,cy-4,cx-12,cy+9,edge);
    gfx.fillCircle(cx-10,cy+14,3,rgb565(36,44,55));
    gfx.fillCircle(cx+10,cy+14,3,rgb565(36,44,55));
    gfx.drawFastHLine(cx-3,cy+24,6,edge);
  }
  if(age>=57 && (now/180UL)%2) {
    gfx.drawLine(cx-38,cy-9,cx-45,cy-17,TFT_YELLOW);
    gfx.drawLine(cx+38,cy-9,cx+45,cy-17,TFT_YELLOW);
  }
}

static bool isWalking(uint32_t now);
static uint16_t rgb565(uint8_t r,uint8_t g,uint8_t b);
// Petite version du personnage pour les promenades : plus de place au jardin.
// Couleurs, humeur et forme adulte V6 conservees.
// V7.2 : retour du dessin prefere, ventre blanc + centre gris clair.
// On ne change pas son style : seul l'emplacement descend sous la bouche.
// Les dimensions sont ajustees pour ne pas deborder sur les pieds.
// V8 — art procedurale adaptee au TFT 135x240. Vue de FACE, selon les
// 18 concepts valides : Terre (feuilles/fleurs), Eau (nageoires), Ciel (ailes).
// Les couleurs sont en RGB565, sans bitmap a charger ni besoin de Wi-Fi.
struct TamaBrush {
  int cx,cy,s;
  int X(int v) const { return cx+(v*s)/100; }
  int Y(int v) const { return cy+(v*s)/100; }
  int R(int v) const { return max(1,(v*s)/100); }
  void ball(int x,int y,int r,uint16_t c) const {
    gfx.fillCircle(X(x),Y(y),R(r),c);
  }
  void oval(int x,int y,int rx,int ry,uint16_t c) const {
    gfx.fillEllipse(X(x),Y(y),R(rx),R(ry),c);
  }
  void line(int x1,int y1,int x2,int y2,uint16_t c) const {
    gfx.drawLine(X(x1),Y(y1),X(x2),Y(y2),c);
  }
  void tri(int x1,int y1,int x2,int y2,int x3,int y3,uint16_t c) const {
    gfx.fillTriangle(X(x1),Y(y1),X(x2),Y(y2),X(x3),Y(y3),c);
  }
  void rect(int x,int y,int w,int h,int r,uint16_t c) const {
    gfx.fillRoundRect(X(x),Y(y),R(w),R(h),R(r),c);
  }
};
static void tamaLeaf(const TamaBrush &p,int x,int y,bool left,uint16_t col) {
  const int dir=left?-1:1;
  p.tri(x,y+14,x+dir*27,y-13,x+dir*10,y-18,col);
  p.tri(x,y+14,x+dir*10,y-18,x+dir*23,y+4,col);
  p.line(x,y+12,x+dir*14,y-7,rgb565(47,139,69));
}
static void tamaFlower(const TamaBrush &p,int x,int y) {
  const uint16_t petal=rgb565(255,244,245),mid=rgb565(255,204,75);
  p.ball(x-7,y,5,petal);
  p.ball(x+7,y,5,petal);
  p.ball(x,y-7,5,petal);
  p.ball(x,y+7,5,petal);
  p.ball(x,y,5,mid);
}
static void tamaWing(const TamaBrush &p,int x,int y,bool left,
                     uint16_t outer,uint16_t inner,bool big) {
  const int d=left?-1:1;
  // Ailes plumees, reconnaissables de face pour le theme Ciel.
  p.tri(x,y+12,x+d*(big?44:33),y-30,x+d*(big?39:27),y+22,outer);
  p.oval(x+d*16,y-10,big?19:15,big?12:10,outer);
  p.oval(x+d*25,y-2,big?18:12,big?10:8,inner);
  if(big) p.oval(x+d*34,y+12,12,8,outer);
  p.line(x+d*7,y+9,x+d*(big?36:27),y-8,rgb565(122,104,177));
}
static void tamaFin(const TamaBrush &p,int x,int y,bool left,
                    uint16_t col,bool big) {
  const int d=left?-1:1;
  p.tri(x,y-16,x+d*(big?23:16),y-26,x+d*(big?25:19),y-2,col);
  p.tri(x,y-2,x+d*(big?26:21),y+13,x,y+20,col);
  p.line(x,y+2,x+d*(big?22:17),y+3,rgb565(255,235,151));
}
static void drawTamaFaceV8(const TamaBrush &p, uint8_t fam, Mood m,
                           uint32_t now, uint8_t phase) {
  const uint16_t ink=rgb565(24,37,55);
  const bool sleep=m==ASLEEP, blink=sleep || now%3900UL<165UL;
  const int sep=(fam==3?16:17);
  if(m==SICK && !sleep) {
    p.line(-sep-5,-13,-sep+5,-3,ink);
    p.line(-sep-5,-3,-sep+5,-13,ink);
    p.line(sep-5,-13,sep+5,-3,ink);
    p.line(sep-5,-3,sep+5,-13,ink);
  } else if(blink) {
    p.line(-sep-6,-7,-sep+6,-7,ink);
    p.line(sep-6,-7,sep+6,-7,ink);
  } else {
    const int look=((now/4600UL)%3UL==0)?-1:((now/4600UL)%3UL==1?1:0);
    p.oval(-sep+look,-9,6,8,ink);
    p.oval(sep+look,-9,6,8,ink);
    p.ball(-sep-2+look,-12,2,TFT_WHITE);
    p.ball(sep-2+look,-12,2,TFT_WHITE);
  }
  p.oval(-25,4,7,5,rgb565(249,160,186));
  p.oval(25,4,7,5,rgb565(249,160,186));
  if(fam==3) {
    // Les oiseaux gardent un petit bec orange a tous les stades.
    p.tri(-6,7,6,7,0,15,rgb565(247,167,56));
    p.line(-5,7,5,7,rgb565(255,226,133));
  } else if(sleep) {
    p.line(-5,13,5,13,ink);
  } else if(m==SAD || m==SICK) {
    p.line(-7,18,0,13,ink);
    p.line(0,13,7,18,ink);
  } else if(m==HAPPY) {
    p.line(-7,11,0,17,ink);
    p.line(0,17,7,11,ink);
  } else {
    p.line(-5,13,0,15,ink);
    p.line(0,15,5,13,ink);
  }
}
static void drawTamaCreatureV8(uint32_t now) {
  const uint8_t st=stage();
  if(st==0 || tamaFamily<1 || tamaFamily>3) { drawEgg(now); return; }
  const bool bedroom=homeScene==1 || pet.dort;
  const bool walking=isWalking(now);
  const uint8_t branch=tamaBranch==2?2:1;
  // 0.80 durant balade, 0.87 dans la chambre, taille normale au jardin.
  const int scale=walking?80:bedroom?87:100;
  const int bob=pet.dort?2:walking?((now/180UL)%2UL?1:-3):
                 ((now/330UL)%2UL?1:-2);
  const TamaBrush p={67,139+(bedroom?5:0)+bob,scale};
  const uint8_t fam=tamaFamily;
  const bool teen=st==3,adult=st==4,baby=st==1,young=st==2;
  const bool pathB=(teen||adult)&&branch==2;
  const Mood m=mood();
  const int rw=baby?30:young?36:teen?(pathB&&fam==3?37:39):43;
  const int rh=baby?30:young?35:teen?39:42;
  uint16_t mainColor=fam==1?rgb565(167,230,72):
                     fam==2?rgb565(69,202,239):rgb565(224,219,251);
  uint16_t shade=fam==1?rgb565(112,194,72):
                   fam==2?rgb565(35,158,213):rgb565(169,153,220);
  uint16_t outline=fam==1?rgb565(43,109,62):
                     fam==2?rgb565(22,103,143):rgb565(104,88,149);
  if(m==SICK) mainColor=rgb565(181,192,195);
  // Pieds et pattes, ensuite corps, visage puis accessoires de la famille.
  if(fam==3) {
    p.oval(-18,rh-2,10,6,rgb565(240,168,55));
    p.oval(18,rh-2,10,6,rgb565(240,168,55));
  } else {
    p.oval(-20,rh-2,13,8,outline);
    p.oval(20,rh-2,13,8,outline);
    p.oval(-20,rh-3,10,6,mainColor);
    p.oval(20,rh-3,10,6,mainColor);
  }
  if(fam==1) {
    // TERRE : une pousse verte, qui devient feuillage ou floral.
    const uint16_t foliage=rgb565(68,184,65);
    const uint16_t light=rgb565(119,221,62);
    if(st>=3) {
      for(int i=0;i<(adult?5:3);++i) {
        const int a=i*14;
        p.oval(-rw-5,a-29,10,14,i%2?light:foliage);
        p.oval(rw+5,a-29,10,14,i%2?light:foliage);
      }
      if(!pathB) {
        p.tri(-rw+7,-21,-rw-8,-48,-rw+23,-39,light);
        p.tri(rw-7,-21,rw+8,-48,rw-23,-39,light);
        p.tri(-rw+8,-29,-rw-2,-42,-rw+20,-37,rgb565(245,202,172));
        p.tri(rw-8,-29,rw+2,-42,rw-20,-37,rgb565(245,202,172));
      }
    }
    if(st==1) {
      tamaLeaf(p,-2,-rh-11,true,light);
      tamaLeaf(p,2,-rh-11,false,foliage);
    } else if(st==2) {
      tamaLeaf(p,-4,-rh-10,true,light);
      tamaLeaf(p,4,-rh-10,false,foliage);
    } else {
      tamaLeaf(p,-3,-rh-12,true,light);
      tamaLeaf(p,3,-rh-12,false,foliage);
    }
  } else if(fam==2) {
    // EAU : crest dorsal sur toutes les formes. Bleu en ado B,
    // orange en adulte B ; deux nageoires orange symetriques de FACE.
    const uint16_t fin=rgb565(253,175,65);
    if(st>=2) {
      tamaFin(p,-rw+3,-2,true,fin,adult);
      tamaFin(p,rw-3,-2,false,fin,adult);
    } else {
      p.oval(-rw+2,15,11,7,shade);
      p.oval(rw-2,15,11,7,shade);
    }
    const uint16_t headFin=(adult && pathB)?fin:
                            (adult&&!pathB)?fin:rgb565(44,175,238);
    p.tri(-10,-rh+6,0,-rh-(adult?29:young?24:18),
          10,-rh+6,outline);
    p.tri(-7,-rh+3,0,-rh-(adult?26:young?21:15),
          7,-rh+3,headFin);
    if(teen||adult) {
      const uint16_t accent=pathB?rgb565(156,229,245):rgb565(72,196,232);
      p.oval(-rw+8,-17,7,6,accent);
      p.oval(rw-8,-17,7,6,accent);
    }
  } else if(fam==3) {
    // CIEL : ailettes plumes beiges/violacees depuis le bebe,
    // toujours en VUE DE FACE ; ado B legerement plus fin.
    const uint16_t wingMain=rgb565(195,184,248);
    const uint16_t wingLight=rgb565(246,238,255);
    if(st==1) {
      tamaWing(p,-rw+2,-3,true,wingMain,wingLight,false);
      tamaWing(p,rw-2,-3,false,wingMain,wingLight,false);
    } else {
      tamaWing(p,-rw+5,-4,true,wingMain,wingLight,teen||adult);
      tamaWing(p,rw-5,-4,false,wingMain,wingLight,teen||adult);
    }
    if(teen||adult) {
      if(pathB) {
        // Eole/Aerys : lune, ruban de plumes, sans gonfler le visage.
        p.oval(0,-rh-16,10,13,rgb565(246,215,103));
        p.oval(5,-rh-20,9,10,rgb565(30,37,73));
        p.oval(-13,-rh-6,9,4,wingMain);
      } else {
        // Alize/Zephyr : plumes volantes, petite couronne doree adulte.
        p.tri(-8,-rh+3,-15,-rh-18,0,-rh-10,wingMain);
        p.tri(0,-rh-10,8,-rh-27,11,-rh+1,wingMain);
        if(adult) {
          p.tri(-14,-rh-12,-12,-rh-29,0,-rh-13,rgb565(255,209,86));
          p.tri(0,-rh-13,10,-rh-33,15,-rh-12,rgb565(255,209,86));
        }
      }
    } else if(young) {
      p.oval(1,-rh-9,10,14,wingMain);
    } else {
      p.oval(0,-rh-8,8,9,wingMain);
    }
  }
  // Bras/ailes devant le corps sont subtils, le relief et les joues
  // restent communs aux trois familles.
  if(fam!=3) {
    p.oval(-rw-2,17,9,15,shade);
    p.oval(rw+2,17,9,15,shade);
  }
  p.oval(0,0,rw+2,rh+2,outline);
  p.oval(0,-1,rw,rh,mainColor);
  p.oval(-rw/3,-rh/2,8,5,
         fam==1?rgb565(199,250,128):fam==2?rgb565(155,232,254):TFT_WHITE);
  // Deux ovales blanc et gris (design valide sur la V7.2) descendus.
  const int bellyW=baby?20:young?26:teen?30:35;
  const int bellyH=baby?14:young?17:teen?19:21;
  p.oval(0,23,bellyW,bellyH,rgb565(255,252,225));
  p.oval(0,24,bellyW-7,bellyH-5,rgb565(224,233,239));
  // Petits motifs distinctifs des adultes (aucun triangle sous la bouche).
  if(adult) {
    if(fam==1 && pathB) {
      tamaFlower(p,-30,-30);
      tamaFlower(p,30,-28);
    } else if(fam==1) {
      p.oval(-22,-34,9,11,rgb565(66,182,60));
      p.oval(22,-34,9,11,rgb565(66,182,60));
    } else if(fam==2) {
      p.line(-14,25,-6,29,rgb565(82,181,227));
      p.line(-6,29,4,25,rgb565(82,181,227));
      p.line(4,25,15,30,rgb565(82,181,227));
    }
  } else if(teen && fam==1 && pathB) {
    tamaFlower(p,24,-31);
  }
  drawTamaFaceV8(p,fam,m,now,st);
  if((now/12000UL)%5UL==3UL && !pet.dort && pet.joie>=60) {
    p.line(-rw-10,-22,-rw-10,-11,TFT_YELLOW);
    p.ball(-rw-10,-7,3,TFT_YELLOW);
  }
}

static void drawFX(uint32_t now) {
  if (effect == FX_NONE || static_cast<int32_t>(now-effectUntil) >= 0) return;
  const int drift = static_cast<int>((now/130)%45);
  switch (effect) {
    case FX_FOOD:
      // L'animation du biscuit est geree par drawActivity().
      break;
    case FX_CLEAN:
      // Douche complete dessinee dans drawActivity() : pas de bulles doublees.
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
// V7 : vraie animation liee a chaque action. Aucun asset bitmap n'est requis.
static void drawActivity(uint32_t now) {
  if(effect==FX_NONE || static_cast<int32_t>(now-effectUntil)>=0 || stage()==0) return;
  const uint32_t elapsed=now-effectStarted;
  const uint16_t chocolate=rgb565(177,103,58);
  switch(effect) {
    case FX_FOOD: {
      // Le biscuit se rapproche de la bouche; le Tama "mache" ensuite.
      if(elapsed<2350UL) {
        const uint32_t travel=elapsed<1300U?elapsed:1300U;
        const int x=109-static_cast<int>(travel*38UL/1300UL);
        const int y=181-static_cast<int>(travel*29UL/1300UL);
        const int radius=elapsed<1750UL?11: max(2,11-static_cast<int>((elapsed-1750UL)/72UL));
        gfx.fillCircle(x,y,radius,chocolate);
        gfx.drawCircle(x,y,radius,TFT_ORANGE);
        if(radius>5) {
          gfx.fillCircle(x-3,y-3,2,rgb565(89,62,42));
          gfx.fillCircle(x+4,y+2,2,rgb565(89,62,42));
        }
      }
      if(elapsed>1350UL && elapsed<2850UL) {
        const int mouth=static_cast<int>((now/185UL)%2UL)?5:2;
        gfx.fillEllipse(67,(homeScene==1?157:152),7,mouth,TFT_BLACK);
      }
      break;
    }
    case FX_CLEAN: {
      // V7.2 : un VRAI pommeau de douche et un jet d'eau anime.
      // Pommeau fixe au coin superieur gauche, arrose le Tama au centre.
      const uint16_t metal=rgb565(171,201,213);
      const uint16_t darkMetal=rgb565(66,109,138);
      gfx.fillRoundRect(7,37,9,27,3,metal);
      gfx.fillRoundRect(13,36,44,9,4,metal);
      gfx.fillRoundRect(51,39,10,15,4,metal);
      gfx.fillRoundRect(31,52,53,12,5,metal);
      gfx.drawFastHLine(36,63,43,darkMetal);
      gfx.drawFastHLine(42,57,32,TFT_WHITE);

      // Les filets se deplacent vers le bas a chaque image.
      // 7 colonnes de gouttes, trois gouttes en cascade par colonne.
      const uint16_t water=rgb565(76,202,250);
      const uint16_t foam=rgb565(197,236,253);
      for(uint8_t i=0;i<7;++i) {
        const int x=39+i*7;
        const int phase=static_cast<int>((elapsed/8UL+i*15UL)%123UL);
        for(uint8_t j=0;j<3;++j) {
          const int y=69+(phase+static_cast<int>(j)*41)%123;
          const int length=6+(i+j)%4;
          if(y+length<194) {
            gfx.drawFastVLine(x,y,length,water);
            gfx.drawPixel(x+1,y+1,foam);
          }
        }
      }
      // Ecume qui s'accumule au niveau des pieds.
      const int wave=static_cast<int>((now/180UL)%3UL);
      gfx.drawFastHLine(20,185+wave,95,TFT_CYAN);
      gfx.drawFastHLine(26,189+wave,82,TFT_SKYBLUE);
      // On conserve quelques bulles en complement de la pluie.
      for(uint8_t i=0;i<7;++i) {
        const int rise=static_cast<int>((elapsed/40UL+19UL*i)%102UL);
        const int x=15+((i*23+static_cast<int>(elapsed/170UL))%107);
        const int y=195-rise;
        const int r=3+(i%3)*2;
        gfx.drawCircle(x,y,r,TFT_CYAN);
        gfx.drawPixel(x-r/2,y-r/2,TFT_WHITE);
      }
      gfx.fillCircle(45,163,4+wave,TFT_WHITE);
      gfx.fillCircle(89,174,6-wave,foam);
      break;
    }
    case FX_HEAL: {
      // Soin sans pansement : halo doux et petites croix qui montent.
      const uint16_t glow=rgb565(119,207,184);
      const int breath=(now/260UL)%3UL;
      gfx.drawEllipse(67,141,46+breath,45+breath,glow);
      gfx.drawEllipse(67,141,50+breath,49+breath,rgb565(48,119,133));
      cross(23,145-(int)((elapsed/35UL)%83UL),TFT_GREEN);
      cross(113,158-(int)((elapsed/44UL)%87UL),TFT_CYAN);
      cross(81,73-(int)((elapsed/90UL)%33UL),TFT_WHITE);
      break;
    }
    case FX_GIFT: {
      // Le couvercle se souleve et laisse jaillir quelques etoiles.
      const int lift=min(26,static_cast<int>(elapsed/54UL));
      gfx.fillRoundRect(81,156,36,29,4,TFT_ORANGE);
      gfx.fillRect(97,157,5,27,TFT_YELLOW);
      gfx.fillRoundRect(78,151-lift,42,9,2,TFT_RED);
      gfx.fillRect(96,151-lift,7,9,TFT_YELLOW);
      if(elapsed>550UL) {
        const int drift=static_cast<int>((elapsed/55UL)%47UL);
        cross(101,138-drift,TFT_YELLOW);
        cross(82,147-drift/2,TFT_WHITE);
      }
      break;
    }
    case FX_HUG: {
      // Les coeurs montent et le personnage se serre contre son doudou.
      const int breathe=static_cast<int>((now/230UL)%3UL);
      heart(32,141-static_cast<int>((elapsed/70UL)%64UL),TFT_PINK);
      heart(108,136-static_cast<int>((elapsed/88UL)%58UL),TFT_RED);
      gfx.drawRoundRect(36-breathe,151-breathe,62+2*breathe,36+2*breathe,16,TFT_PINK);
      break;
    }
    case FX_PLAY: {
      const int y=157-static_cast<int>(abs(sinf(elapsed/180.0f))*43.0f);
      const int x=103-static_cast<int>(elapsed/165UL)%22;
      gfx.fillCircle(x,y,12,TFT_YELLOW);
      gfx.drawCircle(x,y,12,TFT_ORANGE);
      gfx.fillCircle(x-4,y-3,3,TFT_CYAN);
      break;
    }
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
  if((millis()/11000)%5==4) {
    switch(personality) {
      case 1: return "UN PETIT GOUTER ?";
      case 2: return "ON FAIT UN JEU ?";
      case 3: return "UN CALIN ?";
      case 4: return "JE REVE...";
    }
  }
  if ((millis()/11000)%4==0) return "COUCOU !";
  if ((millis()/11000)%4==1) return "...";
  if ((millis()/11000)%4==2) return "ON FAIT QUOI ?";
  return "JE SUIS BIEN !";
}
// V6.1 : monde anime sur le sprite 135x240. Aucun Wi-Fi ni image en flash.
// La distance augmente UNIQUEMENT pendant la promenade (cycle V6 existant).
static uint32_t sceneryDistance=0, sceneryLastFrame=0, sceneryRemainder=0;
static uint16_t rgb565(uint8_t r,uint8_t g,uint8_t b) {
  return (static_cast<uint16_t>(r&0xF8)<<8) |
         (static_cast<uint16_t>(g&0xFC)<<3) | (b>>3);
}
static int wrapPosition(int32_t x,int span) {
  int result=x%span;
  return result<0?result+span:result;
}
static bool isWalking(uint32_t now) {
  // Pas de changement brutal de taille durant les petites scenes de vie.
  return stage()>0 && !pet.dort && static_cast<int32_t>(now-effectUntil)>=0
         && homeScene==0 && (now/7200UL)%4==1;
}
static void drawCloud(int x,int y,uint16_t c) {
  gfx.fillRoundRect(x,y+9,39,12,6,c);
  gfx.fillCircle(x+12,y+10,11,c);
  gfx.fillCircle(x+25,y+7,13,c);
  gfx.fillCircle(x+32,y+13,8,c);
}
// Elements permanents choisis dans MAISON, accessibles apres quelques actions.
static void drawDecor(uint32_t now,bool indoors) {
  if(homeDecor==0 || !decorUnlocked(homeDecor)) return;
  const uint16_t stem=rgb565(43,117,73);
  if(homeDecor==1) {
    // Deux fleurs dans le jardin, une plante en pot dans la chambre.
    for(uint8_t i=0;i<(indoors?1:2);++i) {
      const int x=(i==0?15:120), y=(indoors?169:184);
      if(indoors) {
        gfx.fillRoundRect(x-9,y-2,18,17,3,rgb565(198,104,72));
        gfx.drawFastHLine(x-11,y-2,22,TFT_ORANGE);
      }
      gfx.drawLine(x,y,x-2,y-23,stem);
      gfx.fillEllipse(x-7,y-11,7,3,stem);
      gfx.fillCircle(x-2,y-25,7,i==0?TFT_PINK:TFT_YELLOW);
      gfx.fillCircle(x-2,y-25,3,TFT_YELLOW);
    }
  } else if(homeDecor==2) {
    const int sway=static_cast<int>((now/400UL)%5UL)-2;
    const int x=115+sway, y=107;
    gfx.drawLine(x,y+16,112,184,indoors?TFT_WHITE:TFT_LIGHTGREY);
    gfx.fillEllipse(x,y,13,16,TFT_RED);
    gfx.fillCircle(x-4,y-5,4,TFT_PINK);
    gfx.fillTriangle(x-3,y+12,x+3,y+12,x,y+20,TFT_RED);
  } else if(homeDecor==3) {
    // Petit doudou visible au pied du Tama.
    const int x=115, y=171;
    gfx.fillCircle(x-9,y-12,5,TFT_ORANGE);
    gfx.fillCircle(x+9,y-12,5,TFT_ORANGE);
    gfx.fillRoundRect(x-11,y-8,22,20,8,rgb565(206,150,110));
    gfx.fillCircle(x,y-12,13,rgb565(206,150,110));
    gfx.fillCircle(x-5,y-13,2,TFT_BLACK);
    gfx.fillCircle(x+5,y-13,2,TFT_BLACK);
    gfx.fillCircle(x,y-7,3,TFT_ORANGE);
  }
}
// Chambre simple, claire et animee : l'animal garde ses animations habituelles.
static void drawBedroom(uint32_t now) {
  const uint32_t cycle=pet.ageSec%720UL;
  const bool night=pet.dort || (cycle>=420UL && cycle<610UL);
  const uint16_t wall=night?rgb565(78,77,123):rgb565(236,193,163);
  const uint16_t floor=night?rgb565(80,80,107):rgb565(182,139,112);
  gfx.fillSprite(wall);
  gfx.fillRect(0,170,SW,40,floor);
  for(int i=0;i<5;++i) gfx.drawFastHLine(0,35+i*35,SW,night?rgb565(88,83,132):rgb565(244,205,171));
  // Une fenetre sur le jardin : soleil ou lune selon l'heure.
  const uint16_t window=night?rgb565(31,50,92):rgb565(119,197,222);
  gfx.fillRoundRect(86,39,42,58,5,rgb565(226,230,217));
  gfx.fillRect(91,44,32,48,window);
  gfx.drawFastVLine(106,44,48,TFT_WHITE);
  gfx.drawFastHLine(91,68,32,TFT_WHITE);
  gfx.fillCircle(113,54,6,night?TFT_WHITE:TFT_YELLOW);
  gfx.fillRect(93,86,28,6,night?rgb565(43,97,97):rgb565(75,170,113));
  // Horloge murale avec aiguille animee.
  gfx.fillCircle(20,58,13,TFT_WHITE);
  gfx.drawCircle(20,58,13,rgb565(109,86,88));
  gfx.drawFastVLine(20,52,7,TFT_DARKGREY);
  gfx.drawLine(20,58,20+static_cast<int>((now/2800UL)%7UL)-3,65,TFT_DARKGREY);
  // Petite etagere et objet rond de decoration.
  gfx.fillRect(5,118,35,4,rgb565(121,87,64));
  gfx.fillRoundRect(12,104,13,14,3,TFT_CYAN);
  gfx.fillCircle(30,111,6,TFT_YELLOW);
  // Le lit reste au fond : le personnage est dessine devant.
  gfx.fillRoundRect(3,169,129,33,8,night?rgb565(71,105,168):rgb565(128,178,204));
  gfx.fillRoundRect(7,165,36,16,7,TFT_WHITE);
  gfx.drawFastHLine(5,199,125,rgb565(79,71,65));
  drawDecor(now,true);
}

static void drawScenery(uint32_t now) {
  // Toutes les 12 min de temps ACTIF : jour -> coucher -> nuit -> aube.
  // Si le Tama dort, le monde passe en mode nuit.
  const uint32_t cycle=pet.ageSec%720UL;
  const bool night=pet.dort || (cycle>=420UL && cycle<610UL);
  const bool dusk=!pet.dort && cycle>=300UL && cycle<420UL;
  const bool dawn=!pet.dort && cycle>=610UL;
  const uint16_t sky=night?rgb565(13,24,65):
                     dusk?rgb565(105,76,123):
                     dawn?rgb565(124,119,158):rgb565(116,190,225);
  const uint16_t skyLow=night?rgb565(34,47,93):
                        dusk?rgb565(208,119,120):
                        dawn?rgb565(231,159,132):rgb565(164,216,231);
  const uint16_t cloud=night?rgb565(94,108,144):
                       dusk?rgb565(243,161,151):rgb565(232,247,243);
  const uint16_t farHill=night?rgb565(30,58,83):
                         dusk?rgb565(91,119,139):rgb565(95,156,180);
  const uint16_t nearHill=night?rgb565(24,73,75):
                          dusk?rgb565(74,108,123):rgb565(75,139,157);
  const uint16_t soil=night?rgb565(52,76,85):rgb565(156,135,97);
  const uint16_t grass=night?rgb565(84,118,103):rgb565(113,186,114);
  const uint16_t tree=night?rgb565(36,92,91):rgb565(41,127,74);
  const uint16_t trunk=rgb565(118,83,59);
  gfx.fillSprite(sky);
  gfx.fillRect(0,76,SW,72,skyLow);

  if(night) {
    gfx.fillCircle(110,57,12,rgb565(242,238,178)); // lune
    gfx.fillCircle(115,52,11,sky); // croissant
    for(uint8_t i=0;i<11;++i) {
      int x=wrapPosition(13+i*41,SW);
      int y=35+(i*23)%69;
      uint16_t star=((now/750+i)%5==0)?TFT_WHITE:rgb565(154,181,202);
      gfx.drawPixel(x,y,star);
      if(i%3==0) gfx.drawFastHLine(x-1,y,3,star);
    }
  } else {
    gfx.fillCircle(108,62,14,dusk||dawn?rgb565(255,206,112):rgb565(255,235,132));
    gfx.fillCircle(103,57,5,rgb565(255,242,181)); // reflet doux
  }
  // Les nuages avancent lentement meme lorsque la creature est immobile.
  if(!night) {
    const int cloudOffset=static_cast<int>((now/1600UL)%225UL);
    for(int i=0;i<3;++i) {
      const int x=wrapPosition(28+i*83-cloudOffset,225)-43;
      drawCloud(x,38+(i%2)*29,cloud);
    }
  }

  // Scrolling cumule sans saut entre les cycles balade/immobilite.
  if(sceneryLastFrame!=0) {
    const uint32_t elapsed=now-sceneryLastFrame;
    if(isWalking(now) && elapsed<1500UL) {
      sceneryRemainder+=elapsed;
      sceneryDistance+=sceneryRemainder/70UL; // 14 px/s
      sceneryRemainder%=70UL;
    }
  }
  sceneryLastFrame=now;
  const int farShift=static_cast<int>((sceneryDistance/5UL)%178UL);
  const int nearShift=static_cast<int>((sceneryDistance/2UL)%148UL);
  for(int i=0;i<4;++i) {
    int x=wrapPosition(35+i*92-farShift,368)-65;
    gfx.fillCircle(x,183,60,farHill);
  }
  for(int i=0;i<4;++i) {
    int x=wrapPosition(22+i*88-nearShift,352)-65;
    gfx.fillCircle(x,203,45,nearHill);
  }
  // Quelques arbres lointains glissent derriere le Tama.
  const int treeShift=static_cast<int>((sceneryDistance/2UL)%210UL);
  for(int i=0;i<3;++i) {
    int x=wrapPosition(38+i*86-treeShift,258)-42;
    gfx.fillRoundRect(x-3,138,7,43,3,trunk);
    gfx.fillCircle(x,132,19,tree);
    gfx.fillCircle(x-11,140,13,tree);
    gfx.fillCircle(x+11,140,12,tree);
  }

  gfx.fillRect(0,183,SW,27,soil);
  gfx.drawFastHLine(0,183,SW,grass);
  const int groundShift=static_cast<int>(sceneryDistance%174UL);
  // Petites touffes et fleurs, devant le paysage mais derriere la creature.
  for(int i=0;i<7;++i) {
    int x=wrapPosition(18+i*29-groundShift,203)-24;
    const int y=192+(i%3)*4;
    gfx.drawLine(x-3,y,x,y-4,grass);
    gfx.drawLine(x,y-4,x+3,y,grass);
    if(i%3==0) {
      const uint16_t petal=night?rgb565(143,126,145):
                           (i%2?TFT_PINK:TFT_YELLOW);
      gfx.fillCircle(x,y-6,3,petal);
      gfx.drawPixel(x,y-6,night?TFT_LIGHTGREY:TFT_WHITE);
    }
  }
  drawDecor(now,false);
  // Ombre sous le personnage pour l'ancrer dans le monde.
  gfx.fillEllipse(67,192,37,3,night?rgb565(35,53,64):rgb565(110,96,79));
}

// Petits Z qui s'elevent par vagues, et une bulle de ronflement subtile.
// Dessine apres le Tama pour eviter qu'ils disparaissent derriere son corps.
static void drawSleepFX(uint32_t now) {
  if(!pet.dort || stage()==0) return;
  const uint32_t tick=now/80UL;
  for(uint8_t i=0;i<3;++i) {
    const int rise=static_cast<int>((tick+i*42UL)%127UL);
    const int y=154-rise;
    // Z qui part du coin haut droit de la tete et serpente en montant.
    const int x=101+static_cast<int>((rise/13+i*3)%12UL);
    const uint16_t col=rise>99?rgb565(155,183,210):
                       rise>66?rgb565(207,225,242):TFT_WHITE;
    gfx.setTextColor(col);
    gfx.drawString(i==2?"z":"Z",x,y,i==0?2:1);
  }
  // Petit ronflement : trois ondes respiratoires, discretes.
  const int breath=static_cast<int>((now/420UL)%4UL);
  const uint16_t line=rgb565(215,237,247);
  gfx.drawCircle(114,152,2+breath,line);
  if(breath>=2) gfx.drawCircle(114,152,7+breath,rgb565(127,172,204));
}

static void drawHome(uint32_t now) {
  if(stage()==0 || homeScene==1 || pet.dort) drawBedroom(now);
  else drawScenery(now);
  // S3 : personnages reels extraits de la planche, en taille native.
#ifdef TAMA_S3_LCD
  drawS3ArtCreature(now);
#else
  drawTamaCreatureV8(now);
#endif
  drawActivity(now);
  drawSleepFX(now);
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
  if (usbProbable)
    snprintf(batt,sizeof(batt),"USB - CHARGE NON VERIF.");
  else if (batteryMv)
    snprintf(batt,sizeof(batt),"BAT %u%% %u.%02uV",(unsigned)batteryPercent,
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
static void drawProfil() {
  gfx.fillSprite(TFT_BLACK);
  gfx.drawRoundRect(2,2,131,236,8,TFT_YELLOW);
  gfx.setTextColor(TFT_YELLOW,TFT_BLACK);
  gfx.drawCentreString("MON PROFIL",67,10,2);
  gfx.setTextColor(TFT_WHITE,TFT_BLACK);
  gfx.drawCentreString("CREATURE",67,37,1);
  gfx.setTextColor(TFT_CYAN,TFT_BLACK);
  gfx.drawCentreString(creatureName(),67,51,2);
  gfx.setTextColor(TFT_WHITE,TFT_BLACK);
  gfx.drawCentreString("FAMILLE / STADE",67,82,1);
  char label[35];
  snprintf(label,sizeof(label),"%s - %s",FAMILY_NAMES[tamaFamily],stageLabel());
  gfx.setTextColor(TFT_GREEN,TFT_BLACK);
  gfx.drawCentreString(label,67,96,1);
  gfx.setTextColor(TFT_WHITE,TFT_BLACK);
  gfx.drawCentreString("PERSONNALITE",67,121,1);
  gfx.setTextColor(TFT_CYAN,TFT_BLACK);
  gfx.drawCentreString(PERSONALITY[personality],67,135,2);
  if(stage()>=3) {
    gfx.setTextColor(TFT_YELLOW,TFT_BLACK);
    gfx.drawCentreString(tamaBranch==2?"EVOLUTION : VOIE B":"EVOLUTION : VOIE A",67,164,1);
  } else {
    gfx.setTextColor(TFT_LIGHTGREY,TFT_BLACK);
    gfx.drawCentreString("VOIE A/B A L'ADO",67,164,1);
  }
  snprintf(label,sizeof(label),"REPAS %u SOINS %u",(unsigned)fedCount,(unsigned)careCount);
  gfx.drawCentreString(label,67,187,1);
  snprintf(label,sizeof(label),"JEUX %lu CALINS %u",
           (unsigned long)((uint32_t)gamePlayed+reflexPlayed),(unsigned)hugCount);
  gfx.drawCentreString(label,67,201,1);
  gfx.drawCentreString("SUIVANT      RETOUR",67,222,1);
}

static void drawRoomMenu() {
  gfx.fillSprite(TFT_BLACK);
  gfx.drawRoundRect(2,2,131,236,8,TFT_YELLOW);
  gfx.setTextColor(TFT_YELLOW,TFT_BLACK);
  gfx.drawCentreString("MA PETITE MAISON",67,10,2);
  gfx.setTextColor(TFT_LIGHTGREY,TFT_BLACK);
  gfx.drawCentreString(homeScene?"CHAMBRE":"JARDIN",67,31,1);
  for(uint8_t i=0;i<ROOM_OPTIONS_COUNT;++i) {
    const int y=52+i*21;
    const bool active=roomChoice==i;
    const bool locked=(i>=3 && i<=5 && !decorUnlocked(i-2));
    const bool equipped=(i<2 && homeScene==i) || (i>=2 && i<=5 && homeDecor==i-2);
    if(active) gfx.fillRoundRect(9,y-4,117,20,5,TFT_NAVY);
    const uint16_t bg=active?TFT_NAVY:TFT_BLACK;
    gfx.setTextColor(locked?TFT_DARKGREY:(active?TFT_CYAN:TFT_WHITE),bg);
    gfx.drawString(ROOM_OPTIONS[i],17,y,1);
    gfx.setTextColor(locked?TFT_RED:equipped?TFT_GREEN:TFT_DARKGREY,bg);
    gfx.drawRightString(locked?"X":equipped?"OK":"",115,y,1);
  }
  const char *hint="DECOR A DEBLOQUER";
  if(roomChoice==3) hint=decorUnlocked(1)?"FLEURS DISPONIBLES":"3 REPAS POUR FLEURS";
  else if(roomChoice==4) hint=decorUnlocked(2)?"BALLON DISPONIBLE":"1 ETOILE PARFAITE";
  else if(roomChoice==5) hint=decorUnlocked(3)?"DOUDOU DISPONIBLE":"REFLEXE OU 3 SURPRISES";
  else if(roomChoice<2) hint="CHOISIS TON PAYSAGE";
  else if(roomChoice==2) hint="RANGER LES OBJETS";
  gfx.setTextColor(TFT_YELLOW,TFT_BLACK);
  gfx.drawCentreString(hint,67,202,1);
  gfx.setTextColor(TFT_LIGHTGREY,TFT_BLACK);
  gfx.drawCentreString("SUIVANT / VALIDER",67,222,1);
}

static void drawSac() {
  gfx.fillSprite(TFT_BLACK);
  gfx.drawRoundRect(2,2,131,236,8,TFT_MAGENTA);
  gfx.setTextColor(TFT_MAGENTA,TFT_BLACK);
  gfx.drawCentreString("MON SAC",67,12,2);
  for(uint8_t i=0;i<4;++i) {
    const int y=51+i*38;
    const bool active=bagChoice==i;
    if(active) gfx.fillRoundRect(10,y-5,115,31,7,TFT_NAVY);
    gfx.setTextColor(active?TFT_CYAN:TFT_WHITE,active?TFT_NAVY:TFT_BLACK);
    gfx.drawString(ITEMS[i],19,y,2);
    if(i<3) {
      char count[8];
      snprintf(count,sizeof(count),"x%u",(unsigned)inventory[i]);
      gfx.drawRightString(count,113,y,2);
    }
  }
  gfx.setTextColor(TFT_LIGHTGREY,TFT_BLACK);
  gfx.drawCentreString("SUIVANT / UTILISER",67,215,1);
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
// Tamagotchi V8.2 S3 : 18 personnages RLE / palettes RGB565
// dans la partition data "tama_art" (0xC90000) du flash 16 Mo.
#ifdef TAMA_S3_LCD
struct TamaArtDesc {
  uint32_t offset,length;
  uint16_t width,height;
  uint16_t palette[32];
};
static const esp_partition_t *artPartition=nullptr;
static uint8_t *artPixels=nullptr;
static TamaArtDesc artCurrent={};
static uint8_t cachedArtId=255;
static bool artReady=false;
static void initS3FrameBuffer() {
  artPartition=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                         (esp_partition_subtype_t)0x40,"tama_art");
  if(!artPartition) {
    Serial.println("[TAMA-S3] Pas de partition tama_art, formes secours.");
    return;
  }
  uint8_t hdr[16];
  uint32_t count=0;
  if(esp_partition_read(artPartition,0,hdr,sizeof(hdr))!=ESP_OK) return;
  memcpy(&count,hdr+8,4);
  if(memcmp(hdr,"TAMASPR1",8)!=0 || count!=18) {
    Serial.println("[TAMA-S3] Pas d'images dans tama_art, formes secours.");
    return;
  }
  artPixels=(uint8_t*)heap_caps_malloc(130U*120U,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
  if(!artPixels)artPixels=(uint8_t*)malloc(130U*120U);
  artReady=artPixels!=nullptr;
  Serial.printf("[TAMA-S3] 18 sprites originaux: %s\n",artReady?"OK":"memoire KO");
}
static bool loadArt(uint8_t id) {
  if(!artReady || id>=18)return false;
  if(cachedArtId==id)return true;
  if(esp_partition_read(artPartition,16U+(uint32_t)id*sizeof(TamaArtDesc),
                         &artCurrent,sizeof(artCurrent))!=ESP_OK)return false;
  const uint32_t count=(uint32_t)artCurrent.width*artCurrent.height;
  if(!count || count>130U*120U || artCurrent.length>16000U ||
     artCurrent.offset+artCurrent.length>artPartition->size)return false;
  uint8_t *data=(uint8_t*)malloc(artCurrent.length);
  if(!data)return false;
  bool ok=esp_partition_read(artPartition,artCurrent.offset,data,artCurrent.length)==ESP_OK;
  uint32_t n=0,i=0;
  while(ok && i<artCurrent.length && n<count){
    const uint8_t t=data[i++];
    if(t<128){
      const uint32_t run=(uint32_t)t+1;
      if(i+run>artCurrent.length || n+run>count){ok=false;break;}
      memcpy(artPixels+n,data+i,run);
      i+=run;n+=run;
    } else {
      const uint32_t run=(uint32_t)(t-128)+3;
      if(i>=artCurrent.length || n+run>count){ok=false;break;}
      memset(artPixels+n,data[i++],run);n+=run;
    }
  }
  free(data);
  if(!ok || n!=count){cachedArtId=255;return false;}
  cachedArtId=id;
  return true;
}
static void drawS3ArtCreature(uint32_t now){
  const uint8_t st=stage();
  if(st==0 || tamaFamily<1 || tamaFamily>3){drawEgg(now);return;}
  const uint8_t idx=st==1?0:st==2?1:st==3?(tamaBranch==2?4:2):(tamaBranch==2?5:3);
  if(!loadArt((uint8_t)((tamaFamily-1)*6+idx))){
    drawTamaCreatureV8(now);return;
  }
  const int bob=pet.dort?1:((now/350UL)%2UL?1:-2);
  const int ground=(pet.dort||homeScene==1)?251:250;
  const int x0=(170-(int)artCurrent.width)/2;
  const int y0=ground-(int)artCurrent.height+bob;
  const uint32_t n=(uint32_t)artCurrent.width*artCurrent.height;
  for(uint32_t i=0;i<n;++i){
    const uint8_t c=artPixels[i];
    if(c>0 && c<32){
      gfx.rawPixel(x0+(int)(i%artCurrent.width),
                   y0+(int)(i/artCurrent.width),artCurrent.palette[c]);
    }
  }
}
static void pushTamaFrame(){gfx.pushSprite(0,0);}
#else
static void pushTamaFrame(){gfx.pushSprite(0,0);}
#endif

void setup() {
#ifdef TAMA_S3_LCD
  Serial.begin(115200);
  delay(250);
  Serial.println("\n[TAMA-S3] v8.1.1 boot, noyau Arduino 2.0.14");
  Serial.printf("[TAMA-S3] PSRAM %u octets\n",(unsigned)ESP.getPsramSize());
  // Sur S3, GPIO15 alimente le LCD et GPIO38 controle son retroeclairage.
  // GPIO14 est un BOUTON, ne jamais l'utiliser pour autoriser la batterie !
  pinMode(PIN_POWER,OUTPUT);
  digitalWrite(PIN_POWER,HIGH);
  delay(25);
  pinMode(PIN_NEXT,INPUT_PULLUP);
#else
  // T-Display V1.1 d'origine, cables inchanges.
  gpio_deep_sleep_hold_dis();
  gpio_hold_dis(GPIO_NUM_4);
  pinMode(PIN_NEXT,INPUT);
  pinMode(PIN_BAT_ENABLE,OUTPUT);
  digitalWrite(PIN_BAT_ENABLE,HIGH);
#endif
  pinMode(PIN_OK,INPUT_PULLUP);
  pinMode(PIN_LIGHT,OUTPUT);
  digitalWrite(PIN_LIGHT,LOW);
  pinMode(PIN_BAT_ADC,INPUT);
  analogReadResolution(12);
  analogSetPinAttenuation(PIN_BAT_ADC, ADC_11db);
  nextKey.pin=PIN_NEXT;
  okKey.pin=PIN_OK;

#ifdef TAMA_S3_LCD
  Serial.println("[TAMA-S3] Lecture NVS et ADC");
#endif
  load();
  refreshBattery();
#ifndef TAMA_S3_LCD
  // Protection basse tension connue et testee sur le TTGO V1.1 uniquement.
  if (batteryHibernating) {
    if (batteryMv && batteryMv < BAT_RESUME_MV) {
      const bool pressed = esp_sleep_get_wakeup_cause()==ESP_SLEEP_WAKEUP_EXT0;
      hibernateBattery(pressed);
    }
    batteryHibernating = false;
  } else if (batteryMv && batteryMv < BAT_SLEEP_MV) {
    hibernateBattery(true);
  }
#endif

#ifdef TAMA_S3_LCD
  Serial.println("[TAMA-S3] Initialisation TFT_eSPI");
#endif
  lcd.init();
  lcdReady = true;
#ifdef TAMA_S3_LCD
  Serial.printf("[TAMA-S3] LCD init OK %dx%d\n",lcd.width(),lcd.height());
#endif
  lcd.setRotation(0);              // Portrait physique 135x240 ou 170x320
  lcd.fillScreen(TFT_BLACK);
  digitalWrite(PIN_LIGHT,HIGH);
  gfx.setColorDepth(16);
#ifdef TAMA_S3_LCD
  Serial.println("[TAMA-S3] Creation sprite 135x240");
#endif
#ifdef TAMA_S3_LCD
  if (gfx.createSprite(170,320) == nullptr) {
#else
  if (gfx.createSprite(SW,SH) == nullptr) {
#endif
    lcd.setTextColor(TFT_WHITE,TFT_BLACK);
    lcd.drawCentreString("ERREUR RAM",lcd.width()/2,lcd.height()/2,1);
    while(true) delay(1000);
  }
#ifdef TAMA_S3_LCD
  // Reserve un tampon RGB565 en PSRAM, sans modifier le sprite historique.
  initS3FrameBuffer();
  Serial.println("[TAMA-S3] Sprite et buffer OK");
#endif
  gfx.setTextDatum(TL_DATUM);
  randomSeed((uint32_t)micros() ^ (uint32_t)esp_random());
  // Migration sans perte du Tama de V5.1.
  bool migrated=false;
  if(stage()>0 && personality==0) {
    personality=(uint8_t)random(1,5);
    migrated=true;
  }
  if(stage()>0 && tamaFamily==0) {
    // V7.3 -> V8 : la couleur de la forme adulte aide a maintenir
    // une parente visuelle. Les nouvelles naissances restent 100% aleatoires.
    if(stage()==4 && adultForm>0) {
      tamaFamily=adultForm==2?2:adultForm==3?3:1;
    } else {
      tamaFamily=(uint8_t)random(1,4);
    }
    migrated=true;
  }
  if(stage()>=3 && tamaBranch==0) {
    tamaBranch=chooseBranchV8();
    migrated=true;
  }
  if(stage()==4 && adultForm==0) {
    adultForm=pickAdultForm();
    migrated=true;
  }
  if(migrated) save();
  lastSecond=millis();
  lastSave=millis();
  lastInteraction=millis();
  say("BONJOUR !",FX_WAKE,1800);
#ifdef TAMA_S3_LCD
  Serial.println("[TAMA-S3] setup termine, debut animation");
#endif
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
      else if(next) page=PROFIL;
    } else if(page==PROFIL) {
      if(ok) page=HOME;
      else if(next) page=INFOS;
    } else if(page==SAC) {
      if(next) bagChoice=(bagChoice+1)%4;
      else if(ok) useBagItem();
    } else if(page==ROOM) {
      if(next) roomChoice=(roomChoice+1)%ROOM_OPTIONS_COUNT;
      else if(ok) chooseRoom();
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
#ifndef TAMA_S3_LCD
    if (batteryMv && batteryMv < BAT_SLEEP_MV) {
      if (++consecutiveLow >= 3) hibernateBattery(true);
    } else {
      consecutiveLow=0;
    }
#endif
  }
  // Inactivite: batterie -> ecran eteint; USB probable -> animation.
  const bool idle = (uint32_t)(now-lastInteraction)>=DISPLAY_IDLE_MS;
  if (!displayOff && idle && !usbProbable) {
    if(page==MINIGAME || page==REFLEXGAME) page=HOME;
    shutDisplay();
  }
  if(displayOff && idle && usbProbable) wakeDisplay();
  if(!displayOff && (uint32_t)(now-lastFrame)>=(idle&&usbProbable ? 330UL : 95UL)) {
    lastFrame=now;
    if(idle && usbProbable) drawUsbIdle(now);
    else if(page==INFOS) drawInfos();
    else if(page==SOUVENIRS) drawSouvenirs();
    else if(page==PROFIL) drawProfil();
    else if(page==SAC) drawSac();
    else if(page==ROOM) drawRoomMenu();
    else if(page==MINIGAME) drawGame(now);
    else if(page==REFLEXGAME) drawReflex(now);
    else drawHome(now);
    pushTamaFrame();
  }
  delay(2);
}
