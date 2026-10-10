# Mira Panel : architecture universelle côté utilisateur

## Décision produit (10 octobre 2026)

**Un seul projet à maintenir, un seul numéro de version et une seule action de publication, avec un binaire OTA par matériel si nécessaire.**

La priorité n'est donc plus d'obtenir un fichier binaire identique malgré des périphériques incompatibles. L'utilisateur doit voir une seule version de Mira Panel, et **le bon firmware doit être sélectionné automatiquement lorsque l'identification du modèle est fiable**. En cas d'incertitude, l'installateur propose le choix du modèle avant écriture, plutôt que de risquer un mauvais flash.

Le plugin Jeedom conserve le bouton « Installer Mira Panel » et le parcours « Programmer un écran ESP32 » pour les équipements et widgets. L'objectif est de rendre l'installation simple sans dupliquer les fonctionnalités Jeedom.

## Profils matériels (cible)

| Identifiant de profil | Matériel | Écran / tactile | Flash / PSRAM | État Mira |
| --- | --- | --- | --- | --- |
| `wt32-sc01-plus` | WT32-SC01 Plus | 320×480 / LovyanGFX et tactile WT32 | 8 Mo / PSRAM module WT32 à vérifier | Mira 0.6.38 installée et fonctionnelle ; LVGL 8.3.11 dans le build actuel |
| `waveshare-esp32s3-touch-lcd-4.3c` | Waveshare 4.3C | RGB 800×480 / GT911 | 16 Mo / 8 Mo | Mira 0.6.38 fonctionnelle, LVGL 8.4, RGB 12 MHz, triple-buffer |
| `opennextion-onx2432g028` | Open Nextion 2,8 pouces ONX2432G028 | ST7789 240×320 / CST826 | 16 Mo / 8 Mo (ESP32-S3R8) | Portage futur, aucune version Mira publiée |

**Ne jamais flasher un binaire destiné à un autre matériel**, même lorsque le numéro de version Mira est identique. Le choix multi-binaire préserve les configurations PSRAM, le partitionnement, les options d'outils et les pilotes stables de chaque carte. Passer le WT32 en LVGL 8.4 reste un objectif éventuel de maintenance, **pas une condition préalable au service d'installation multi-profils**.

## Une seule action de publication

1. GitHub Actions reconstruit la même révision des sources applicatives Mira.
2. Le workflow compile le profil WT32, le profil Waveshare et, lorsqu'il sera prêt, le profil Open Nextion.
3. Si l'un des profils officiellement pris en charge échoue, **ne pas publier de version partielle**.
4. Après validation, une seule opération publie les artefacts distincts, leurs sommes SHA-256 et un manifeste de version unique.
5. Le plugin Jeedom et le Web Installer utilisent ce manifeste pour choisir le bon fichier. Aucun URL générique non typé comme `firmware/Mira_Panel.bin` ne doit être utilisé pour mettre à jour les trois cartes.

Un exemple conceptuel des champs du manifeste (pas encore implémenté) :

```json
{
  "project": "Mira Panel",
  "version": "0.6.39",
  "targets": {
    "wt32-sc01-plus": {"ota": "URL_WT32", "factory": "URL_FACTORY_WT32", "sha256": "SHA256_WT32"},
    "waveshare-esp32s3-touch-lcd-4.3c": {"ota": "URL_WAVESHARE", "factory": "URL_FACTORY_WAVESHARE", "sha256": "SHA256_WAVESHARE"},
    "opennextion-onx2432g028": {"ota": "URL_NEXTION", "factory": "URL_FACTORY_NEXTION", "sha256": "SHA256_NEXTION"}
  }
}
```

Ces chaînes sont des **emplacements de conception**, pas de vraies URL de téléchargement. Ne pas publier d'entrée Nextion tant que le portage et les tests matériels ne sont pas terminés.

## Installation : deux cas différents

### Écran déjà équipé de Mira

- Depuis Jeedom ou la page Web du panneau, lire le profil exposé par l'écran (`/displayinfo` ou un futur endpoint d'identité sans ambiguïté).
- Comparer la version installée à la version publiée et choisir l'OTA de la **même cible** dans le manifeste.
- Vérifier la taille, SHA-256 et la destination avant envoi ; prévoir un refus côté ESP si l'identité du fichier ne correspond pas.
- Une nouvelle version publiée une seule fois devient disponible pour tous les appareils. Les appareils devront toujours effectuer leur installation OTA, éventuellement orchestrée par Jeedom ; plusieurs firmwares publiés ne signifient pas plusieurs mises à jour manuelles côté développeur.

### Premier flashage USB depuis navigateur

- Le Web Installer proposé par le plugin Jeedom utilise Web Serial / esptool-js pour lire les caractéristiques du composant avant d'écrire la Flash.
- Ces informations (famille ESP32-S3, taille Flash, champs eFuse) **n'identifient pas systématiquement le modèle exact de carte**. Ne pas déduire « Waveshare » de « ESP32-S3 / 16 Mo » : Nextion peut présenter le même couple.
- OpenNextion indique stocker son identifiant produit ONX2432G028 dans `EFUSE_BLK9` : piste à vérifier techniquement sur la lecture Web Serial. Sur écran déjà équipé de Mira, le profil stocké dans le firmware est plus fiable.
- Si une empreinte fabricant/matériel sûre est reconnue, proposer automatiquement l'image correcte.
- Sinon afficher un sélecteur WT32 / Waveshare / Open Nextion et demander de confirmer le modèle. **Ne jamais lancer un flash automatique sur un profil deviné.**
- Garder l'image usine (FACTORY) séparée de l'OTA applicative, en respectant les partitions et offsets propres au modèle.

## Mesures de sécurité

- Les builds doivent porter leur profil dans une métadonnée vérifiable, sans dépendre seulement du nom du fichier.
- Garder une stratégie de récupération USB (sauvegarde d'une image fonctionnelle) et vérifier les tailles de partitions.
- Ne pas modifier les réglages du pilote RGB stable Waveshare 0.6.38 (12 MHz et triple-buffer) pour la seule gestion de version.
- Ne pas activer l'ancien endpoint GitHub de publication WT32 pour les autres profils : `firmware/version.json` et `docs/manifest.json` publient actuellement une ancienne version générique WT32 0.6.5.
- Préparer la distribution multi-profils en branche jusqu'à validation avec les modèles effectivement disponibles.

## Prochaines étapes

1. Stabiliser la publication multi-cibles de la 0.6.38 **sans changement de firmware matériel**.
2. Ajouter un manifeste ciblé, un endpoint d'identité (propre à chaque écran) et une sélection par profil, en tests.
3. Adapter le Web Installer et le bouton « Installer Mira Panel » pour l'installation initiale et l'OTA.
4. Porter l'Open Nextion 2,8 pouces ; ajouter ses binaires uniquement après compilation et essai sur carte réelle.
5. Consolider les sources historiques et rationaliser les workflows.

## Références matériel

- Open Nextion 2,8 pouces officiel : https://open-nextion-app-store.nextion.tech/devices/onx2432g028/
- OpenNextion GitHub : https://github.com/OpenNextion/OpenNextion-SKU-ONX2432G028
- Espressif esptool-js Web Serial : https://github.com/espressif/esptool-js
