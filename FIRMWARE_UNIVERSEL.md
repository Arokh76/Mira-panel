# Mira Panel — architecture matérielle

Le logiciel Mira Panel partage les fonctions Jeedom, MQTT, widgets et stockage. Chaque carte doit utiliser un firmware compilé pour son matériel.

| Matériel | Statut dans `firmware-beta` |
| --- | --- |
| WT32-SC01 Plus | Arduino actif |
| Open Nextion Genius 2.8 | Arduino en test |
| Waveshare 4.3C | **ESP-IDF présent** dans `esp-idf-4.3C/`, OTA et Factory conservés ; l'ancienne cible Arduino n'est plus active en bêta |
| Waveshare 7 pouces | ESP-IDF actif dans `esp-idf/`, dernier OTA HTTP24K archivé |

Les binaires OTA/FACTORY ne sont jamais interchangeables. La branche `main` conserve ses propres anciennes distributions, sans être modifiée par la suppression de la cible Arduino sur `firmware-beta`.

Pour le détail des sources, des binaires et des versions actives : [VERSION_ACTIVE.md](VERSION_ACTIVE.md).
