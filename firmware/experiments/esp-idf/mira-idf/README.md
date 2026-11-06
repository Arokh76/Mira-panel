# Mira Panel — Portage ESP-IDF du Waveshare 4,3″ (branche firmware-beta)

Ce répertoire contient les **sources autonomes du portage ESP-IDF 5.5.6 / LVGL 9.6.0** pour le Waveshare 4,3C ESP32-S3 (800 × 480). Il est maintenu séparément du firmware **WT32 Arduino**, qui reste la référence de comportement fonctionnel. Ce portage n'est **pas** un remplacement de la version stable.

## Périmètre importé

- Interface LVGL, rendu, tactile GT911, pilotes d'affichage et alimentation, veille, polices, date/heure et widgets.
- Compatibilité avec le plugin Jeedom, commandes et états, réseau/IP fixe automatique, DNS/DHCP, NTP, MQTT, configuration web, stockage, OTA et portail de récupération.
- Fichiers `CMakeLists.txt`, `sdkconfig.defaults`, `partitions.csv`, composant `esp_jpeg` déclaré dans `main/idf_component.yml`, tests de parité web et workflow GitHub Actions.
- Conserve les noms de fichiers, chemins, partitionnement et réglages du portage déjà compilé dans `experiment/mira-idf-mira-ui-43c`.

## État au transfert — 6 novembre 2026

Le correctif du mode `fixed_auto` conserve les DNS après passage DHCP → IP fixe ; l'heure et la date ont été **confirmées sur l'écran physique** après le build n°1306. La veille dispose d'une heure 120 px confirmée, avec date 28 px et température 40 px intégrées dans le build n°1324 ; **le rendu physique de ces deux dernières tailles reste à confirmer**. Ne pas déduire une parité fonctionnelle complète de la réussite d'une compilation.

## Compiler et tester

Depuis la racine de ce projet :

```sh
idf.py set-target esp32s3
idf.py build
```

Le composant LVGL doit être fourni en version **v9.6.0** dans `components/lvgl` (le workflow CI le récupère automatiquement). Le workflow `.github/workflows/build-mira-idf-waveshare.yml` compile avec ESP-IDF **v5.5.6**, lance le test de parité web et publie les binaires USB factory et OTA en **artefacts de test**. Ne pas confondre les deux formats lors du flash.

Commande de vérification des pages web depuis la racine du dépôt :

```sh
python3 firmware/experiments/esp-idf/mira-idf/tests/check_beta_web_parity.py
```

## Règles de conservation

- Ne pas modifier `firmware/source/consolidated/MIRA_PANEL/` ni le firmware WT32 validé pour ce transfert.
- Ne pas fusionner la branche d'expérimentation entière : seuls les fichiers nécessaires au portage sont intégrés.
- Ne pas publier les binaires du portage en OTA de production avant validation sur le matériel et tests Jeedom/MQTT.
- Le portage 7 pouces demeure distinct. Les pilotes CH422G et ses paramètres RGB ne doivent pas écraser ceux du Waveshare 4,3C.
