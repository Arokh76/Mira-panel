# Mira Panel : objectif firmware universel

**Contrat produit : un seul et même fichier binaire OTA pour WT32-SC01 Plus et Waveshare ESP32-S3 Touch LCD 4.3C**, identification automatique du matériel au démarrage, une interface Mira / Jeedom commune, et LVGL 8.4 sur les deux écrans.

## État au 10 octobre 2026

- La version 0.6.38 a été installée avec succès sur le WT32-SC01 Plus (confirmation utilisateur).
- La version 0.6.38 Waveshare fonctionne avec les widgets, après passage du pixel clock RGB de 16 à 12 MHz.
- Les deux fichiers actuels sont encore **distincts et non interchangeables** : ils ne réalisent donc PAS l'objectif mono-binaire.
- La PR #2 est une proposition de nettoyage non destructif, conservée en brouillon. Ne pas la présenter comme un firmware universel achevé.

## Verrous techniques à résoudre

1. **Mode PSRAM dès le boot (prioritaire).** Les modules WT32-S3-WROVER-R2 documentés pour le WT32-SC01 Plus utilisent 2 Mo de PSRAM Quad-SPI ; le Waveshare 4.3C emploie ESP32-S3-WROOM-1-N16R8 avec 8 Mo de PSRAM Octal-SPI. Le mode PSRAM est normalement défini dans le SDK avant le démarrage du programme applicatif. Un simple test de détection d'écran dans setup() ne suffit pas si le binaire ne boote pas sur les deux modes. Vérifier l'identification du module WT32 réel avant toute conclusion.
2. **LVGL et Arduino-ESP32.** WT32 0.6.38 utilise Arduino-ESP32 2.0.17 + LVGL 8.3.11, Waveshare Arduino-ESP32 3.3.11 + LVGL 8.4. Tester LVGL 8.4 sur WT32, puis unifier la toolchain.
3. **Profils aujourd'hui choisis à la compilation.** La macro MIRA_TARGET_WAVESHARE_43C choisit le pilote, la résolution, la tâche LVGL, la couleur et certains transports ; il faut un choix runtime non intrusif et une interface de pilotes unique.
4. **Taille des buffers.** Triple-buffer Waveshare : 800 x 480 x 2 x 3 = 2 304 000 octets, plus le reste des allocations. Le WT32 R2 n'a pas la capacité de ce framebuffer : n'activer le triple-buffer que sur Waveshare.
5. **Flash / OTA.** Profils WT32 8 Mo et Waveshare 16 Mo avec tables de partitions distinctes. Le futur binaire unique doit tenir dans les deux partitions OTA existantes et être compatible avec les deux configurations de démarrage ; un firmware OTA n'est pas une image usine.

## Ordre d'avancement

1. Vérifier la PSRAM réelle des deux unités et expérimenter le passage du WT32 à LVGL 8.4, sans modifier le Waveshare 0.6.38 stable.
2. Trouver et valider le mécanisme permettant un **seul binaire bootable** avec les deux modes PSRAM. Si le SDK stock ne sait pas le faire, étudier les adaptations au démarrage avant de distribuer un binaire supposé universel.
3. Remplacer les sélections matérielles au préprocesseur par une détection runtime et deux adaptateurs matériels internes. Conserver les paramètres RGB 12 MHz / triple-buffer côté Waveshare.
4. Bâtir une compilation produisant exactement **un fichier OTA .bin** et tester ce fichier identique sur chaque écran : boot, tactile, affichage, luminosité, HTTP, Jeedom, widgets et OTA.
5. Après validation physique des deux cibles, seulement alors publier et simplifier davantage les anciens scripts et builds.

## Documentation des matériels

- WT32-SC01 Plus (référence famille R2) : https://device.report/m/064b7e37c420aa7d9503d879d6052d13418f3469a7885520121d02b717eae777
- Waveshare 4.3C (N16R8 / octal PSRAM) : https://github.com/waveshareteam/ESP32-S3-Touch-LCD-4.3C/blob/main/HARDWARE_REFERENCE.md
- Modes PSRAM ESP-IDF : https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-guides/flash_psram_config.html
