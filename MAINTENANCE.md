# Maintenance / stabilité

## Matériels

| Cible | Flash | Propriété |
| --- | ---: | --- |
| WT32-SC01 Plus | 8 Mo | Firmware 0.6.38 installé et confirmé fonctionnel sur matériel ; OTA et Web Installer publiés encore en 0.6.5 |
| Waveshare Touch LCD 4.3C | 16 Mo | Firmware d'essai 0.6.38 ; RGB 800×480 ; LVGL 8.4 et triple-buffer |
| Open Nextion ONX2432G028 | 16 Mo | Future cible 2,8 pouces ; ESP32-S3R8, LVGL, ST7789 240×320 et CST826 ; firmware à porter |

Ne jamais publier un firmware Waveshare ou Nextion sous un chemin OTA générique WT32. À terme, publier **un seul manifeste de version Mira comportant une entrée par matériel** ; le système d'installation choisira le fichier correspondant au profil détecté, avec vérification du modèle avant flash.

## Point de récupération Waveshare

- Build testé : **0.6.38**, commit [e1b89c9](https://github.com/Arokh76/Mira-panel/commit/e1b89c912f651d6a0adb557a4395077b15a7b40d).
- Job GitHub Actions [#38003987693](https://github.com/Arokh76/Mira-panel/actions/runs/38003987693) : compilation WT32 et Waveshare réussie.
- Le ZIP d'artefact contient `Mira_Panel_Waveshare_4.3C_0.6.38.bin` (OTA) et `Mira_Panel_Waveshare_4.3C_0.6.38_FACTORY.bin` (image fusionnée).
- **Via l'interface web OTA de Mira : utiliser uniquement le binaire OTA**, pas le fichier FACTORY.
- Le fichier FACTORY sert aux procédures de flashage USB compatibles avec le partitionnement 16 Mo.
- Si des artefacts RGB reviennent, comparer ce build avec la 0.6.37, qui utilisait la même configuration sauf **PCLK 16 MHz**.
- Le correctif du libellé initial de luminosité (valeur brute telle que 220 avant affichage en %) reste à faire séparément, sans toucher au pilote RGB.

### Paramètres vidéo conservés en 0.6.38

- `EXAMPLE_LCD_PIXEL_CLOCK_HZ = 12 * 1000 * 1000`
- `LVGL_PORT_AVOID_TEAR_MODE = 2` : triple-buffer + full refresh.
- `EXAMPLE_LCD_RGB_BUFFER_NUMS = 3`.
- Bounce buffer : `EXAMPLE_LCD_H_RES * 10` pixels, VSYNC et PWM inchangés.

## Pourquoi conserver les scripts historiques ?

Le build reconstruit un sketch à partir du ZIP de référence 0.4.3, puis rejoue **dans l'ordre** les patches et scripts jusque `v0.6.38.py`. La suppression des scripts 0.4.x–0.6.37 **rendrait la 0.6.38 impossible à reconstruire**.

Avant de les archiver, effectuer un export consolidé des sources 0.6.38, le committer dans un dossier source versionné et obtenir une nouvelle compilation réussie pour les deux matériels. Cette consolidation n'a **pas** été réalisée pendant le nettoyage des workflows.

## Workflows et archivage

Le workflow **`.github/workflows/build-mira-0638.yml`** génère deux archives séparées : `Mira_Panel_WT32_SC01_Plus_0.6.38_preview` (OTA ESP32-S3 8 Mo avec LVGL 8.3.11) et `Mira_Panel_Waveshare_4.3C_0.6.38_preview` (OTA + FACTORY 16 Mo avec LVGL 8.4, triple-buffer, RGB 12 MHz). Les deux partagent les mêmes scripts Mira, mais **pas les paramètres du pilote ni le partitionnement**. Le WT32 et le Waveshare 0.6.38 ont été validés par l'utilisateur sur matériel ; le Nextion attend son portage et ses tests.

Les workflows GitHub Actions exécutables vivent exclusivement dans `.github/workflows/`. Les workflows de diagnostic et de versions dépassées sont archivés en `.github/archive/workflows/`, avec contenu identique et historique conservé. Si un ancien workflow est utile, il suffit de le restaurer dans `.github/workflows/` dans une branche de travail.

Les workflows de publication du WT32 restent actifs : `build-firmware.yml`, `publish-firmware.yml`, `sync-web-installer.yml`, `pages.yml`. Le workflow Waveshare officiel de référence reste disponible. **Ne pas relier le workflow 0.6.38 à la publication générique WT32.**

## Prochaines étapes proposées

1. Compléter les tests longue durée des deux 0.6.38 existantes et corriger les libellés de plateforme.
2. Construire la **publication multi-profils** : un seul numéro de version, un seul workflow de publication, plusieurs fichiers ciblés, un seul manifeste, auto-détection avant installation quand elle est fiable.
3. Étudier le portage de l'Open Nextion ONX2432G028 2,8 pouces, avec son pilote ST7789 et son tactile CST826.
4. Consolider les sources et, dans une révision distincte, corriger le pourcentage de luminosité sans modifier le RGB Waveshare.
