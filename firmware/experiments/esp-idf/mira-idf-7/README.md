# Mira Panel — Waveshare 7 STD (ESP-IDF beta, premier jet)

Source logique : `firmware-beta/firmware/experiments/esp-idf/mira-idf` (portage 4.3C avec les corrections réseau/DNS/NTP, Jeedom/MQTT, widgets et veille).
Pilote panneau : branche d'essais `experiment/mira-idf-full-ui-7`, issue du profil `experiment/mira-idf-waveshare7-pclk13-stable`.

## Paramètres matériels repris

- Waveshare ESP32-S3 Touch LCD 7 standard, RGB565 800 × 480, 2 framebuffers PSRAM, pas de bounce buffer.
- PCLK **13 MHz**, synchronisation H/V 4/8/8, broches RGB du pilote 7 précédemment testé.
- Expandeur **CH422G** : mode 0x24, données 0x38, séquence alimentation/réinitialisation LCD validée.
- GT911 I2C 0x5D / 0x14 : initialisation via EXIO1 + GPIO4 ; lecture du premier contact depuis 0x814F.
- **Aucune commande du CH32 du 4.3C n'est envoyée** ; l'API des entrées/sorties isolées du 4.3C renvoie NOT_SUPPORTED.
- PWM de rétroéclairage 7 pouces **non validé** : l'API luminosité ne pilote pas de PWM sur cette cible. Ne pas considérer le curseur comme une commande matérielle effective.

## Construction et essais

La compilation dédiée est `.github/workflows/build-mira-idf-waveshare7.yml`. Elle publie deux images clairement identifiées 7 STD : `Mira_LVGL96_7_STD_UI_OTA.bin` et `Mira_LVGL96_7_STD_UI_FACTORY_USB_0x0.bin`.

**Premier jet expérimental**. Vérifier sur appareil réel : démarrage, alimentation LCD/CH422G, RGB sans dérive, tactile, date/heure NTP (y compris IP fixe automatique), widgets Jeedom/MQTT, veille, mémoire et OTA. Le rendu tactile 7 pouces et la luminosité ne sont pas considérés comme validés par la compilation seule.

La branche `firmware-beta` conserve le projet 4.3C inchangé dans `mira-idf/` et le firmware Arduino WT32 inchangé.
