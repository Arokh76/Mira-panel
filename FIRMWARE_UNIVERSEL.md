# Mira Panel — architecture matérielle

Le logiciel Mira Panel partage les fonctions Jeedom, MQTT, widgets et stockage. Chaque carte doit utiliser un firmware compilé pour son matériel.

| Matériel | Statut dans `firmware-beta` |
| --- | --- |
| WT32-SC01 Plus | Arduino actif |
| Open Nextion Genius 2.8 | Arduino en test |
| Waveshare 4.3C | **Arduino supprimé**, remplacement prévu en ESP-IDF |
| Waveshare 7 pouces | Développement ESP-IDF indépendant |

Les binaires OTA/FACTORY ne sont jamais interchangeables. La branche `main` conserve ses propres anciennes distributions, sans être modifiée par la suppression de la cible Arduino sur `firmware-beta`.
