# Mira Panel — ESP-IDF + LVGL 9.6 (Waveshare 4.3C)

**État : prototype de migration isolé, non validé sur matériel.** Aucune modification de la stable ni de l'interface Jeedom.

## Pourquoi

- LVGL 8.4 Waveshare (J) : environ **5–6 FPS**, dessin ~100–143 ms/image, attente VSYNC 0–34 ms.
- LVGL 9.6 en Arduino : **5,8 FPS** sur le banc 35 cartes (mesuré sur écran réel).
- Version LVGL 9.6 avec deux tuiles de 20 lignes en SRAM : **5,8 FPS et effet accordéon** (tearing). **Rejetée.**
- Objectif de la migration native IDF : premier palier **10–15 FPS**, sans sacrifier le rendu, puis plus si possible. Ce n'est pas une promesse de résultat.

## Architecture prévue

- `main/mira_display.c` : carte Waveshare 4.3C, RGB565 800×480 / 12 MHz, deux framebuffers PSRAM et intégration LVGL direct mode selon **l'exemple officiel ESP-IDF 5.5**, tactile GT911 sur I2C, synchronisation **on_frame_buf_complete**.
- `main/mira_ui.c` : 35 cartes scrollables et compteur FPS ; **aucun appel hardware**, réutilisable pour WT32/7" avec d'autres drivers.
- `main/mira_recovery.c` : point d'accès de restauration OTA (appli `.bin`, pas image usine).
- `main/app_main.c` : tâche LVGL isolée sur cœur 1, compteur FPS, Wi-Fi sur cœur 0.
- `sdkconfig.defaults` : ESP32-S3 240 MHz, PSRAM OPI 80 MHz, compilation optimisée, LVGL 9.6 C standard string/allocator, rafraîchissement 10 ms.
- `partitions.csv` : partitionnement **identique** à celui de Mira 0.7.37 Waveshare (OTA0 0x10000, OTA1 0x310000, SPIFFS 0x610000).

## Compilation

GitHub Actions `build-mira-idf-waveshare.yml` utilise **ESP-IDF 5.5.6** et **LVGL 9.6.0 épinglé** (CMake/ESP-IDF pur, sans Arduino). Aucun besoin d'installer ESP-IDF sur Windows.

Compilation ne signifie pas un fonctionnement éprouvé ; valider l'écran, le tactile et le VSYNC sur le matériel avant de traiter ce binaire comme un OTA utilisable.

## Récupération depuis l'application de test

Si l'application démarre correctement, point d'accès **Mira-IDF-Test** (mot de passe **MiraIDF2026**), puis ouvrir `http://192.168.4.1`. On peut y téléverser le vrai `Mira 0.7.37 … OTA.bin` via HTTP ; les partitions SPIFFS ne sont pas effacées. `/perf` expose FPS/VSYNC.

**Risques :** ce portail n'est pas prouvé sur le panneau ; un mauvais port de l'écran peut empêcher son affichage, un crash peut empêcher le Wi-Fi. Garder un moyen de récupération USB et le binaire OTA stable. Ne pas flasher une image factory sur la page OTA. Ne pas utiliser le matériel en contrôle domotique pendant ce prototype.

## Suites

1. Valider IDF sur le Waveshare : affichage, toucher, défilement sans tearing, FPS.
2. Optimiser rendu/invalidations (caching/layers) si le pur IDF reste lent.
3. Porter `MiraPanelUI` et Jeedom sans perdre les fonctionnalités actuelles.
4. Introduire d'autres cartes par pilotes (WT32 puis 7"), en gardant le moteur de widgets responsive commun.
5. Comparer mesures FPS/tactile sous charges équivalentes avant toute migration de la stable.

Références : `esp-idf/examples/peripherals/lcd/rgb_panel` (Espressif IDF v5.5.6) et `lvgl/lvgl` v9.6.0.
