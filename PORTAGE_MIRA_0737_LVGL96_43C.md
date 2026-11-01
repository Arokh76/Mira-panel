# Portage de Mira Panel 0.7.37 complet vers LVGL 9.6 — Waveshare 4.3C

## Base obligatoire
- Source: `firmware/source/consolidated/MIRA_PANEL/` sur `main` (Mira 0.7.37 complet).
- Branche de portage: `experiment/mira-0737-full-lvgl96-43c` (copie du vrai Mira, **pas** `mira_full.c`).
- Interface, réglages, JSON /miralvgl.json, HTTP/Jeedom, MQTT, caméra et médias: **réutiliser les classes d'origine** `MiraPanelUI`, `MiraPanelServer`, `MiraMqtt`, `MiraCameraBmp`, sans recréer un backend d'imitation.
- Référence matérielle de test fonctionnelle: `experiment/mira-idf-mira-ui-43c`, correctif GT911 commit `ef32200866fdba1bf0c7b484e7789bf7d2290b2b`.
- Référence relief ~9.7 FPS: `experiment/mira-idf-light-relief-shadows-43c`, `CARD_RELIEF_OUTLINE` = 2px outline opacité 50%, ombre 0. Ces 9.7 FPS étaient mesurés **sur banc d'essai**, pas sur le Mira complet.

## Déjà intégré à cette branche (code source, pas firmware prêt à flasher)
- Pilote ESP-IDF RGB565 natif 800x480, PCLK 12MHz, deux FB PSRAM, EDMA sans bounce: `src/MiraNative43C/mira_display.c`.
- GT911 reconnu 0x5D; remise à zéro via expander Waveshare; échantillonnage indépendant 10ms; lecture correcte des coordonnées à 0x814F; événements LVGL conservés dans une queue.
- Adaptateur `MiraNative43CDisplay.h` pour garder la classe `MiraDisplay` utilisée par l'interface originale et faire piloter la luminosité par l'expander 0x24.
- Condition expérimentale `MIRA_LVGL9_NATIVE` dans `MiraHardware.h` et `MiraPanelUI`. Les autres cibles et `main` restent inchangées.
- Le style des cartes d'origine emploie le relief 2px sans ombre floutée dans le chemin LVGL9.
- L'initialisation native fournit déjà l'indev tactile; éviter de l'enregistrer une deuxième fois dans `MiraPanelUI::begin()`.

## À terminer AVANT de compiler et de donner une OTA
1. Remplacer toutes les API LVGL8 incompatibles avec LVGL9 dans `MiraPanelUI.cpp` et `MiraPanelUI.h` (images, polices, snapshot/scroll-cache, colorwheel, LVGL9 input);
2. Adapter `MiraFrenchFont.cpp` et les polices sommeil 84/120 et accueil 96 à la structure `lv_font_t` LVGL9, et conserver les accents;
3. Mettre à jour `lv_conf.h` pour LVGL 9.6 et activer l'ensemble des polices/widgets utilisés par Mira;
4. Ajouter un build reproductible incluant Arduino ESP32, bibliothèques Serveur/Jeedom existantes et LVGL9.6; vérifier absence de doublon de serveur HTTP ou tâches GUI;
5. Vérifier le retour des données Jeedom, l'absence d'actions involontaires au boot, la persistance des réglages et la compatibilité SPIFFS/OTA;
6. Mesurer FPS sur Mira complet et vérifier relief/synchronisation VSYNC sans réintroduire les artefacts.

## État
**EN COURS, NON COMPILÉ / NON VALIDÉ SUR MATÉRIEL. NE PAS FLASHER.**
Ne pas utiliser l'ancien prototype `mira_full.c` comme source applicative. Le but est de conserver exactement les fonctionnalités de Mira 0.7.37.
