# Mira Panel

Firmware domotique **Jeedom / MQTT** pour **WT32-SC01 Plus** (320 × 480, flash 8 Mo) et **Waveshare ESP32-S3 Touch LCD 4.3C** (800 × 480, flash 16 Mo).

## Version de référence : 0.6.52

**Une seule version fonctionnelle, deux binaires spécifiques au matériel.** Ne jamais échanger les binaires des deux modèles.

| Cible | Version | Validation |
| --- | --- | --- |
| WT32-SC01 Plus | **0.6.52** | Testé sur écran réel : synchronisation Jeedom, commandes physiques et 123 navigations web sans plantage |
| Waveshare 4.3C | **0.6.52** | Compilation CI validée ; 0.6.49 stable sur écran réel, validation matérielle 0.6.52 en cours |
| Open Nextion ONX2432G028 | Portage futur | Non pris en charge pour le moment |

### Correctif de stabilité WT32

La version 0.6.52 déplace les allocations mémoire des objets **LVGL** du WT32 vers la **PSRAM** plutôt que de réserver 96 Ko de DRAM interne. Après synchronisation Jeedom, la RAM interne disponible est passée d'environ 24 Ko à 123 Ko, et 123 changements d'onglets ont réussi sans plantage. La configuration LVGL native du Waveshare reste spécifique à sa carte.

La version conserve les améliorations jusqu'à 0.6.50 (dont le champ `displayName` séparé de l'identifiant technique) ainsi que le diagnostic `/mira-health` introduit en 0.6.51.

### Mise à jour et installation

- **OTA (configuration conservée)** : depuis l'interface web de l'écran, utiliser le binaire `ota.bin` de **son propre profil** dans [firmware/releases/0.6.52](firmware/releases/0.6.52).
- **Installation USB / restauration** : [Web Installer](https://arokh76.github.io/Mira-panel/), puis choisir explicitement WT32 ou Waveshare. L'installation usine peut effacer la configuration.
- **Découverte de la version et des binaires** : [firmware/targets.json](firmware/targets.json), utilisé pour distinguer les profils ; [firmware/version.json](firmware/version.json) reste réservé à la compatibilité historique WT32.
- `firmware/Mira_Panel.bin` demeure un alias **WT32 exclusivement** pour les anciens mécanismes OTA. Ne jamais l'utiliser sur Waveshare.

### Construire / publier

- `.github/workflows/build-mira.yml` reconstruit Mira et compile les **deux profils** avec leurs dépendances et configurations matérielles respectives.
- `.github/workflows/publish-mira-targets.yml` publie les **deux binaires OTA et FACTORY**, les manifestes, les empreintes SHA-256 et l'alias historique WT32, uniquement après un build réussi sur `main` correspondant au même commit.
- `.github/workflows/pages.yml` sert à publier le Web Installer.
- `.github/workflows/build-waveshare-official-reference.yml` conserve la compilation de référence du constructeur pour les diagnostics.

### Sources et maintenance

La chaîne de build reconstruit toujours le projet depuis `firmware/source/Mira_Panel_Source_V0.4.3.zip`, les correctifs `firmware/patches/`, les surcharges HTML et les scripts successifs `firmware/scripts/`. **Ces scripts historiques ne doivent pas être supprimés** tant que nous n'avons pas reconstruit et validé un snapshot consolidé des sources pour les deux appareils.

Les workflows expérimentaux anciens sont conservés sous `.github/archive/workflows/` (non exécutés). Voir [MAINTENANCE.md](MAINTENANCE.md) et [FIRMWARE_UNIVERSEL.md](FIRMWARE_UNIVERSEL.md) pour les détails.

**MQTT** : `mira/<nom_ecran>/Actions/<topic>` et `mira/<nom_ecran>/States/<topic>` ; identifiant client `mira-panel-XXXXXX`.
