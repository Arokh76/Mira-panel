// Tamagotchi V7.0 — scenes de vie et petite maison — LilyGO T-Display V1.1 ORIGINAL (ESP32/ST7789, 135x240).
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
  } else {
    lastSurpriseAge = pet.ageSec;
    save();
  }
}
// Lecture du pont diviseur 1:2 de la version originale (GPIO14 HIGH -> GPIO34 ADC).
// Une valeur hors plage est refusee pour eviter un sommeil intempestif.
static uint16_t sampleBatteryMv() {
  // GPIO14 doit rester HIGH pendant l'utilisation sur batterie :
  // il intervient aussi dans l'autorisation d'alimentation du module.
  digitalWrite(PIN_BAT_ENABLE, HIGH);
  delay(12);
  (void)analogReadMilliVolts(PIN_BAT_ADC);
  uint16_t samples[12];
  for (uint8_t n=0; n<12; n++) {
    samples[n] = analogReadMilliVolts(PIN_BAT_ADC);
    delay(2);
  }
  // 25e percentile pour limiter les pics POSITIFS de l'ADC ESP32.
  for (uint8_t i=0; i<12; ++i) {
    for (uint8_t j=i+1; j<12; ++j) {
      if (samples[j]<samples[i]) {
        const uint16_t tmp=samples[i]; samples[i]=samples[j]; samples[j]=tmp;
      }
    }
  }
  const uint32_t v = static_cast<uint32_t>(samples[3]) * 2UL;
  if(v<2600 || v>5400) return 0;
  return static_cast<uint16_t>(v);
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
  usbProbable = rawSupplyMv > USB_PROBABLE_MV;
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
  if (stage() != oldStage) {
    if(stage()==1 && personality==0) personality=static_cast<uint8_t>(random(1,5));
    if(stage()==3 && adultForm==0) adultForm=pickAdultForm();
    say(stage()==1?"JE SUIS NE !":stage()==3?"MA NOUVELLE FORME !":"J'AI GRANDI !",FX_GROW,3400);
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
static bool isWalking(uint32_t now);
static uint16_t rgb565(uint8_t r,uint8_t g,uint8_t b);
// Petite version du personnage pour les promenades : plus de place au jardin.
// Couleurs, humeur et forme adulte V6 conservees.
static void drawWalkingCreature(uint32_t now,uint8_t st,Mood m,uint16_t body) {
  const int cx=67+static_cast<int>(sinf(now/900.0f)*4.0f);
  const int by=(st==1?105:st==2?91:81)+((now/190)%2?-3:2);
  const int w=st==1?58:st==2?74:83;
  const int h=st==1?74:st==2?89:100;
  const int stride=(now/190)%2?7:-7;
  const uint16_t outline=rgb565(28,65,72);
  // Oreilles, membres et pieds legerement alternes pendant la marche.
  if(st>=2) {
    gfx.fillRoundRect(cx-w/2+11,by-13,15,22,6,body);
    gfx.fillRoundRect(cx+w/2-26,by-13,15,22,6,body);
    gfx.drawRoundRect(cx-w/2+11,by-13,15,22,6,outline);
    gfx.drawRoundRect(cx+w/2-26,by-13,15,22,6,outline);
    gfx.fillCircle(cx-w/2+18,by-10,3,TFT_PINK);
    gfx.fillCircle(cx+w/2-19,by-10,3,TFT_PINK);
  }
  if(st==3) {
    // Accessoire propre a chacune des quatre evolutions.
    if(adultForm==1) {
      gfx.fillCircle(cx,by-15,12,TFT_YELLOW);
      gfx.fillCircle(cx-4,by-17,2,TFT_ORANGE);
      gfx.fillCircle(cx+4,by-12,2,TFT_ORANGE);
    } else if(adultForm==2) {
      gfx.fillTriangle(cx,by-26,cx-9,by-8,cx+9,by-8,TFT_YELLOW);
      gfx.fillCircle(cx,by-9,4,TFT_ORANGE);
    } else if(adultForm==3) {
      heart(cx,by-16,TFT_RED);
    } else if(adultForm==4) {
      gfx.fillTriangle(cx-12,by-6,cx-8,by-23,cx,by-7,TFT_YELLOW);
      gfx.fillTriangle(cx,by-7,cx+8,by-23,cx+12,by-6,TFT_YELLOW);
      gfx.fillRoundRect(cx-12,by-10,24,7,2,TFT_YELLOW);
    } else {
      gfx.fillRoundRect(cx-10,by-18,20,18,6,TFT_YELLOW);
    }
  }
  const int feetY=by+h-9;
  gfx.fillRoundRect(cx-29+stride,feetY,24,17,6,body);
  gfx.fillRoundRect(cx+5-stride,feetY,24,17,6,body);
  gfx.drawRoundRect(cx-29+stride,feetY,24,17,6,outline);
  gfx.drawRoundRect(cx+5-stride,feetY,24,17,6,outline);
  gfx.fillRoundRect(cx-w/2-7,by+39,17,35,7,body);
  gfx.fillRoundRect(cx+w/2-10,by+39,17,35,7,body);
  gfx.drawRoundRect(cx-w/2-7,by+39,17,35,7,outline);
  gfx.drawRoundRect(cx+w/2-10,by+39,17,35,7,outline);
  gfx.fillRoundRect(cx-w/2,by,w,h,st==1?25:30,body);
  gfx.drawRoundRect(cx-w/2,by,w,h,st==1?25:30,outline);
  // Un petit ventre et une expression reduite pour conserver la silhouette.
  if(st>=2) {
    gfx.fillRoundRect(cx-16,by+51,32,27,12,TFT_WHITE);
    gfx.fillRoundRect(cx-11,by+56,22,18,9,TFT_LIGHTGREY);
  }
  const int faceY=by+(st==1?29:34);
  const bool blink=now%3900<155;
  if(m==SICK) {
    gfx.drawLine(cx-20,faceY-5,cx-11,faceY+5,TFT_BLACK);
    gfx.drawLine(cx-20,faceY+5,cx-11,faceY-5,TFT_BLACK);
    gfx.drawLine(cx+11,faceY-5,cx+20,faceY+5,TFT_BLACK);
    gfx.drawLine(cx+11,faceY+5,cx+20,faceY-5,TFT_BLACK);
  } else if(blink) {
    gfx.drawFastHLine(cx-21,faceY,13,TFT_BLACK);
    gfx.drawFastHLine(cx+8,faceY,13,TFT_BLACK);
  } else {
    gfx.fillRoundRect(cx-21,faceY-6,12,15,4,TFT_BLACK);
    gfx.fillRoundRect(cx+9,faceY-6,12,15,4,TFT_BLACK);
    gfx.fillCircle(cx-17,faceY-3,2,TFT_WHITE);
    gfx.fillCircle(cx+13,faceY-3,2,TFT_WHITE);
  }
  gfx.fillCircle(cx-24,faceY+16,5,TFT_PINK);
  gfx.fillCircle(cx+24,faceY+16,5,TFT_PINK);
  if(m==SAD || m==SICK) {
    gfx.drawLine(cx-9,faceY+27,cx,faceY+21,TFT_BLACK);
    gfx.drawLine(cx,faceY+21,cx+9,faceY+27,TFT_BLACK);
  } else if(m==HAPPY) {
    gfx.drawLine(cx-9,faceY+20,cx,faceY+29,TFT_BLACK);
    gfx.drawLine(cx,faceY+29,cx+9,faceY+20,TFT_BLACK);
  } else {
    gfx.drawFastHLine(cx-8,faceY+25,16,TFT_BLACK);
  }
  if(st==3 && adultForm==2) {
    // Joueur : embleme discret.
    gfx.fillTriangle(cx,by+66,cx-7,by+77,cx+7,by+77,TFT_YELLOW);
  } else if(st==3 && adultForm==3) {
    gfx.fillCircle(cx,by+70,5,TFT_RED);
  }
}

static void drawCreature(uint32_t now) {
  const uint8_t st = stage();
  const Mood m = mood();
  const bool hop = (!pet.dort && (now/7200)%4==2);
  const bool walking = isWalking(now);
  const int bob = pet.dort ? 5 : hop ? ((now/160)%2 ? -11 : -2) :
                  walking ? ((now/190)%2 ? -4 : 1) : ((now/340)%2 ? -2 : 2);
  const int wander = walking
                   ? static_cast<int>(sinf(now/650.0f)*9.0f) : 0;
  int cx = 67 + wander;
  const bool dance = (!pet.dort && pet.joie >= 60 &&
                      (now / 12000) % 5 == 3);
  const bool wave = (!pet.dort && (((now/7200)%4==3) || dance));
  const uint16_t adultColor=adultForm==1?TFT_ORANGE:adultForm==2?TFT_CYAN:
                            adultForm==3?TFT_PINK:adultForm==4?TFT_GREEN:
                            friendship>=75?TFT_MAGENTA:TFT_SKYBLUE;
  uint16_t body=st==1?TFT_CYAN:st==2?TFT_GREEN:adultColor;
  if (m == SICK) body = TFT_LIGHTGREY;
  else if (st == 3 && m == SAD) body = TFT_ORANGE;

  if (st == 0) {
    drawEgg(now);
    return;
  }
  // Grande creature au repos, version a 75-80 % durant la balade.
  if(walking) {
    drawWalkingCreature(now,st,m,body);
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
    if(adultForm==1) { // Gourmand : biscuit
      gfx.fillCircle(cx,by-20,17,TFT_YELLOW);
      gfx.fillCircle(cx-7,by-24,3,TFT_ORANGE);
      gfx.fillCircle(cx+7,by-17,3,TFT_ORANGE);
    } else if(adultForm==2) { // Joueur : etoile
      gfx.fillTriangle(cx,by-32,cx-10,by-10,cx+10,by-10,TFT_YELLOW);
      gfx.fillCircle(cx,by-13,6,TFT_ORANGE);
    } else if(adultForm==3) {
      heart(cx,by-21,TFT_RED);
    } else if(adultForm==4) { // Sage : couronne
      gfx.fillTriangle(cx-18,by-5,cx-11,by-29,cx,by-8,TFT_YELLOW);
      gfx.fillTriangle(cx,by-8,cx+11,by-29,cx+18,by-5,TFT_YELLOW);
      gfx.fillRoundRect(cx-18,by-11,36,10,3,TFT_YELLOW);
    } else {
      gfx.fillRoundRect(cx-15,by-25,30,25,9,TFT_YELLOW);
      gfx.fillCircle(cx,by-25,6,TFT_ORANGE);
    }
  }

  // Jambes et bras avec mouvements.
  int legShift = walking ? ((now/190)%2 ? 8 : -8) : ((now/290)%2 ? 5 : -5);
  if (pet.dort) legShift = 0;
  gfx.fillRoundRect(cx-38+legShift,by+height-13,29,20,8,body);
  gfx.fillRoundRect(cx+9-legShift,by+height-13,29,20,8,body);
  gfx.fillRoundRect(cx-width/2-9,by+55 + (wave ? -17 : 0),23,45,10,body);
  gfx.fillRoundRect(cx+width/2-14,by+55 - (wave ? 17 : 0),23,45,10,body);
  gfx.fillRoundRect(bodyx,by,width,height,st == 1 ? 33 : 40,body);
  // Liseret fonce : particulierement utile pour le Tama vert sur le jardin.
  gfx.drawRoundRect(bodyx,by,width,height,st == 1 ? 33 : 40,rgb565(25,70,77));

  if (st >= 2) {
    gfx.fillRoundRect(cx-22,by+76,44,36,18,TFT_WHITE);
    gfx.fillRoundRect(cx-15,by+83,30,23,12,TFT_LIGHTGREY);
  }
  gfx.fillCircle(cx-31,by+68,7,TFT_PINK);
  gfx.fillCircle(cx+31,by+68,7,TFT_PINK);
  drawFace(cx,mouthY,now,m);
  if(st==3) {
    if(adultForm==1) {
      gfx.fillCircle(cx,by+102,8,TFT_YELLOW);
      gfx.fillCircle(cx-3,by+100,2,TFT_ORANGE);
    } else if(adultForm==2) {
      gfx.fillTriangle(cx,by+86,cx-12,by+106,cx+12,by+106,TFT_YELLOW);
    } else if(adultForm==3) {
      heart(cx,by+98,TFT_RED);
    } else if(adultForm==4) {
      gfx.drawCircle(cx,by+99,12,TFT_YELLOW);
      gfx.fillCircle(cx,by+99,4,TFT_YELLOW);
    }
  }
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
  if (m != ASLEEP && pet.faim < 20 && now%2400 < 900) {
    gfx.drawCircle(cx+44,by+15,6,TFT_ORANGE);
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
// V7 : vraie animation liee a chaque action. Aucun asset bitmap n'est requis.
static void drawActivity(uint32_t now) {
  if(effect==FX_NONE || static_cast<int32_t>(now-effectUntil)>=0 || stage()==0) return;
  const uint32_t elapsed=now-effectStarted;
  const uint16_t chocolate=rgb565(177,103,58);
  switch(effect) {
    case FX_FOOD: {
      // Le biscuit se rapproche de la bouche; le Tama "mache" ensuite.
      if(elapsed<2350UL) {
        const uint32_t travel=min(elapsed,1300UL);
        const int x=113-static_cast<int>(travel*42UL/1300UL);
        const int y=150-static_cast<int>(travel*32UL/1300UL);
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
        gfx.fillEllipse(67,125,7,mouth,TFT_BLACK);
      }
      break;
    }
    case FX_CLEAN: {
      // Une vague au niveau du ventre et de vraies bulles flottantes.
      const int wave=static_cast<int>((now/180UL)%3UL);
      gfx.drawFastHLine(20,184+wave,95,TFT_CYAN);
      gfx.drawFastHLine(26,187+wave,82,TFT_SKYBLUE);
      for(uint8_t i=0;i<11;++i) {
        const int rise=static_cast<int>((elapsed/36UL+17UL*i)%126UL);
        const int x=15+((i*23+static_cast<int>(elapsed/145UL))%110);
        const int y=198-rise;
        const int r=3+(i%3)*2;
        gfx.drawCircle(x,y,r,TFT_CYAN);
        gfx.drawPixel(x-r/2,y-r/2,TFT_WHITE);
      }
      gfx.fillCircle(45,154,5+wave,TFT_WHITE);
      gfx.fillCircle(85,166,7-wave,TFT_WHITE);
      break;
    }
    case FX_HEAL: {
      // Pansement qui se pose et croix de soins.
      const int x=90, y=143;
      gfx.fillRoundRect(x-18,y-5,36,12,5,rgb565(245,194,148));
      gfx.fillRect(x-5,y-4,10,10,TFT_WHITE);
      cross(x,y+1,TFT_RED);
      cross(24,111-static_cast<int>((elapsed/45UL)%36UL),TFT_GREEN);
      cross(110,90-static_cast<int>((elapsed/65UL)%29UL),TFT_CYAN);
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
  if(homeScene==1 || pet.dort) drawBedroom(now);
  else drawScenery(now);
  // Les personnages et leurs animations passent au-dessus du monde.
  drawCreature(now);
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
  gfx.drawCentreString("MON PROFIL",67,12,2);
  gfx.setTextColor(TFT_WHITE,TFT_BLACK);
  gfx.drawCentreString("PERSONNALITE",67,49,1);
  gfx.setTextColor(TFT_CYAN,TFT_BLACK);
  gfx.drawCentreString(PERSONALITY[personality],67,67,2);
  gfx.setTextColor(TFT_WHITE,TFT_BLACK);
  gfx.drawCentreString("EVOLUTION",67,104,1);
  gfx.setTextColor(TFT_GREEN,TFT_BLACK);
  gfx.drawCentreString(stage()==3?EVOLUTION[adultForm]:"EN COURS...",67,123,2);
  char line[35];
  snprintf(line,sizeof(line),"REPAS %u  SOINS %u",(unsigned)fedCount,(unsigned)careCount);
  gfx.setTextColor(TFT_LIGHTGREY,TFT_BLACK);
  gfx.drawCentreString(line,67,165,1);
  snprintf(line,sizeof(line),"JEUX %lu CALINS %u",
       (unsigned long)(static_cast<uint32_t>(gamePlayed)+reflexPlayed),(unsigned)hugCount);
  gfx.drawCentreString(line,67,181,1);
  gfx.drawCentreString("SUIVANT      RETOUR",67,220,1);
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
void setup() {
  // Relacher les verrous du retroeclairage apres un reveil deep sleep.
  gpio_deep_sleep_hold_dis();
  gpio_hold_dis(GPIO_NUM_4);
  pinMode(PIN_NEXT,INPUT);          // Pull-up physique sur T-Display V1.1
  pinMode(PIN_OK,INPUT_PULLUP);
  pinMode(PIN_LIGHT,OUTPUT);
  digitalWrite(PIN_LIGHT,LOW);
  pinMode(PIN_BAT_ENABLE,OUTPUT);
  digitalWrite(PIN_BAT_ENABLE,HIGH);
  pinMode(PIN_BAT_ADC,INPUT);
  analogReadResolution(12);
  analogSetPinAttenuation(PIN_BAT_ADC, ADC_11db);
  nextKey.pin=PIN_NEXT;
  okKey.pin=PIN_OK;

  load();
  refreshBattery();
  // Aucun affichage/jeu tant que la batterie ne s'est pas suffisamment rechargee.
  if (batteryHibernating) {
    if (batteryMv && batteryMv < BAT_RESUME_MV) {
      const bool pressed = esp_sleep_get_wakeup_cause()==ESP_SLEEP_WAKEUP_EXT0;
      hibernateBattery(pressed);
    }
    batteryHibernating = false;
  } else if (batteryMv && batteryMv < BAT_SLEEP_MV) {
    hibernateBattery(true);
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
  // Migration sans perte du Tama de V5.1.
  bool migrated=false;
  if(stage()>0 && personality==0) {
    personality=static_cast<uint8_t>(random(1,5));
    migrated=true;
  }
  if(stage()==3 && adultForm==0) {
    adultForm=pickAdultForm();
    migrated=true;
  }
  if(migrated) save();
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
    if (batteryMv && batteryMv < BAT_SLEEP_MV) {
      if (++consecutiveLow >= 3) {
        hibernateBattery(true);
      }
    } else {
      consecutiveLow=0;
    }
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
    gfx.pushSprite(0,0);
  }
  delay(2);
}
