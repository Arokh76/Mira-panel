# Maintenance Mira Panel — référence 0.6.52

## Matériels supportés

| Profil | Flash | Bibliothèque d'interface | État |
| --- | ---: | --- | --- |
| WT32-SC01 Plus | 8 Mo | LVGL 8.3.11, objets alloués en PSRAM | 0.6.52 validée sur matériel, synchro Jeedom et 123 navigations sans plantage |
| Waveshare ESP32-S3 Touch LCD 4.3C | 16 Mo | LVGL 8.4 du fournisseur, triple-buffer RGB 12 MHz | 0.6.49 validée sur matériel ; 0.6.52 compilée et en validation |
| Open Nextion ONX2432G028 | 16 Mo | Portage futur | Aucun firmware Mira |

Les binaires ne sont **jamais interchangeables**. Utiliser uniquement l'OTA de la cible correspondante. Les images FACTORY sont réservées à l'installation/à la restauration USB et peuvent écraser les données.

## Correctif de stabilité WT32 (0.6.52)

La 0.6.51 a mesuré 24 Ko de RAM interne libre après démarrage, et moins de 10 Ko dans certains pics. La 0.6.52 utilise les allocateurs personnalisés LVGL en **PSRAM** sur WT32, au lieu du pool statique de 96 Ko en DRAM interne. La DRAM libre mesurée est montée à environ 123 Ko après synchronisation Jeedom. Le serveur web a résisté à 123 changements d'onglets et les commandes physiques sont restées fonctionnelles.

Le port Waveshare **ne reçoit pas cette configuration LVGL WT32** : sa compilation remplace le fichier `lv_conf.h` par la configuration LVGL Waveshare et conserve les tampons DMA/RGB spécifiques.

`/mira-health` expose les compteurs de navigation et l'état de la mémoire. Il s'agit d'un outil de diagnostic, non d'un watchdog ni d'un mécanisme de réparation.

## Construction et diffusion

- Source commune : `firmware/source/Mira_Panel_Source_V0.4.3.zip` + `firmware/patches/` + `firmware/overrides/` + `firmware/final-overrides/` + scripts successifs `firmware/scripts/v0.*.py`.
- `.github/workflows/build-mira.yml` : compile les deux profils, fabrique OTA et FACTORY, vérifie les binaires et teste la préparation de la publication.
- `.github/workflows/publish-mira-targets.yml` : après un build réussi sur le commit exact de `main`, publie **les deux cibles ensemble** ; en cas d'échec d'une cible, la publication est bloquée.
- `firmware/targets.json` : version commune et chemins/empreintes SHA-256 propres à chaque matériel.
- `firmware/version.json` et `firmware/Mira_Panel.bin` : **compatibilité WT32 uniquement** avec les premières versions Mira.
- `docs/manifest-wt32.json`, `docs/manifest-waveshare.json` : Web Installer USB. `docs/manifest.json` : alias historique WT32.
- `pages.yml` : déploiement Web Installer.

Les anciens workflows sont archivés sous `.github/archive/workflows/`. **Conserver les scripts historiques** : ils restent indispensables à la reconstruction jusqu'à l'export d'une source consolidée qui produit exactement les deux firmwares.

## Retour arrière et précautions

- Garder les fichiers OTA de la dernière version fonctionnelle pour chaque profil : WT32 0.6.52 (testée) et Waveshare 0.6.49 (testée) avant toute nouvelle installation.
- Tester les modifications touchant Wi-Fi, LVGL ou le serveur web **séparément sur les deux écrans** avant de les considérer validées.
- Ne pas déclencher de commandes d'action Jeedom pendant l'initialisation ou la resynchronisation des états.
- Préserver la configuration SPIFFS : les mises à jour normales se font via le **binaire OTA**, jamais via FACTORY depuis la page de mise à jour.
