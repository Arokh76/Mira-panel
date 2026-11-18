// Mini-Tamagotchi : LilyGO T-Display V1.1 (ESP32 original, ST7789 135 x 240)
// GPIO35 = nourrir ; GPIO0 = jouer ; GPIO4 = retroeclairage.
// Autonome : aucun Wi-Fi, aucune connexion et aucune soudure.
#include <Arduino.h>
#include <TFT_eSPI.h>

static TFT_eSPI tft;
static TFT_eSprite scene(&tft);

constexpr uint8_t BTN_MANGER = 35;
constexpr uint8_t BTN_JOUER  = 0;
constexpr uint8_t TFT_LED    = 4;

uint8_t humeur = 0;
uint32_t finHumeur = 0;
bool ancienManger = false;
bool ancienJouer = false;
uint32_t derniereAction = 0;

void setup() {
  pinMode(BTN_MANGER, INPUT);       // GPIO35 : entree, pull-up sur la carte
  pinMode(BTN_JOUER, INPUT_PULLUP); // GPIO0 : bouton BOOT
  pinMode(TFT_LED, OUTPUT);
  digitalWrite(TFT_LED, HIGH);

  tft.init();
  tft.setRotation(1);              // paysage 240 x 135
  tft.fillScreen(TFT_BLACK);
  scene.setColorDepth(16);
  if (!scene.createSprite(240, 135)) {
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawCentreString("Erreur memoire", 120, 60, 2);
    while (true) delay(1000);
  }
  scene.setTextDatum(TL_DATUM);
}

void loop() {
  const uint32_t now = millis();
  const bool manger = digitalRead(BTN_MANGER) == LOW;
  const bool jouer = digitalRead(BTN_JOUER) == LOW;

  if (now - derniereAction >= 180) {
    if (manger && !ancienManger) {
      humeur = 1;
      finHumeur = now + 1800;
      derniereAction = now;
    } else if (jouer && !ancienJouer) {
      humeur = 2;
      finHumeur = now + 1800;
      derniereAction = now;
    }
  }
  ancienManger = manger;
  ancienJouer = jouer;
  if (humeur && (int32_t)(now - finHumeur) >= 0) humeur = 0;

  const int saut = ((now / 300) % 2) ? 0 : 4;
  const bool cligne = (now % 3500) < 160;

  scene.fillSprite(TFT_BLACK);
  scene.fillRoundRect(85, 31 + saut, 70, 74, 25, TFT_CYAN);
  if (cligne) {
    scene.drawFastHLine(98, 63 + saut, 14, TFT_BLACK);
    scene.drawFastHLine(128, 63 + saut, 14, TFT_BLACK);
  } else {
    scene.fillCircle(105, 62 + saut, 5, TFT_BLACK);
    scene.fillCircle(135, 62 + saut, 5, TFT_BLACK);
  }
  if (humeur == 1) {
    scene.fillCircle(120, 84 + saut, 5, TFT_BLACK);
  } else {
    scene.drawLine(111, 83 + saut, 120, 89 + saut, TFT_BLACK);
    scene.drawLine(120, 89 + saut, 129, 83 + saut, TFT_BLACK);
  }

  scene.setTextColor(TFT_WHITE, TFT_BLACK);
  scene.setTextSize(2);
  if (humeur == 1) scene.drawString("MIAM !", 87, 5);
  else if (humeur == 2) scene.drawString("YOUPI !", 80, 5);
  else scene.drawString("COUCOU", 82, 5);

  scene.setTextSize(1);
  scene.drawString("MANGER", 24, 121);
  scene.drawString("JOUER", 188, 121);
  scene.pushSprite(0, 0);
  delay(50);
}
