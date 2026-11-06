# OTA et publications — firmware-beta

Les profils Arduino actifs dans cette branche sont WT32-SC01 Plus et Open Nextion Genius 2.8.

La cible **Waveshare 4.3C Arduino** n'est plus proposée : son pilote, sa compilation et ses artefacts ont été retirés de `firmware-beta`. Le profil ESP-IDF 4.3C sera intégré après validation. Les anciens installateurs et firmwares du dépôt `main` ne sont pas modifiés.

Le code WT32 peut être plus récent que les binaires 0.7.37 archivés : les derniers correctifs JSON/thème Rose/logo se trouvent dans la source bêta, sans mise à jour OTA automatique.

Toujours sélectionner un binaire adapté à la carte. L'ancienne URL non typée `firmware/version.json` est volontairement figée, car elle était utilisée par des appareils plus anciens.
