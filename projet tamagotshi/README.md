# Projet Tamagotshi 🐣

Mini-Tamagotchi pour **LilyGO TTGO T-Display V1.1** (ESP32 original, 1,14" ST7789, 240 × 135 en paysage).

Le programme est autonome et ne demande aucun câblage supplémentaire :
- La bestiole cyan bouge et cligne des yeux.
- Bouton GPIO35 : « MIAM ! ».
- Bouton GPIO0 (BOOT) : « YOUPI ! ».

## Compilation automatique

Le workflow GitHub Actions [Tamagotchi - compiler T-Display V1.1](../../actions/workflows/tamagotchi-build.yml) compile avec **PlatformIO** et la bibliothèque **TFT_eSPI**.

Le binaire construit est publié, si la compilation et la publication réussissent, ici :
[`firmware/TamaTest_TDisplay_V11_full.bin`](firmware/TamaTest_TDisplay_V11_full.bin).

## Flash Download Tools (Windows)

1. Ouvrir Espressif Flash Download Tools, mode **ESP32 / Develop**.
2. Charger **`TamaTest_TDisplay_V11_full.bin`** à l'adresse **`0x0000`**.
3. Sélectionner **SPI SPEED 40 MHz**, **SPI MODE DIO**, **FLASH SIZE 32Mbit (4MB)**.
4. Choisir le port COM de la carte, cliquer **START**, puis appuyer sur **RESET**.

⚠️ Ne pas utiliser sur un T-Display S3. Le fichier complet contient le bootloader, les partitions et l'application. Le flash peut effacer le programme précédemment présent sur la carte.

## Sources

- `src/main.cpp` : firmware
- `platformio.ini` : définition de carte, écran et bibliothèques
- `.github/workflows/tamagotchi-build.yml` : compilation et publication automatiques

Ce projet est indépendant de Mira Panel.
