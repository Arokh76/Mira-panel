# Organisation de firmware-beta (13 novembre 2026)

| Matériel | Sources officielles | Binaires de référence |
| --- | --- | --- |
| WT32 Arduino 0.7.44 | `Arduino/MIRA_PANEL/` | `Arduino/bin/` |
| Waveshare 7" ESP-IDF HTTP24K | `esp-idf/` | `esp-idf/bin/` |

**Sources et binaires exacts retrouvés et corrélés aux compilations GitHub Actions d'origine.**

- WT32 : run 43027107986, sources commit `dacb114deefcc9495ab781161449def50e288b8e`.
- 7" : run 42899018718, sources commit `fe6867f5d6ef09d9fe96d0de04181942020c790d`.

Voir [VERSION_ACTIVE.md](VERSION_ACTIVE.md) pour les empreintes, règles de flash et chemins de build.

Les autres chemins sont historiques. Les anciens chemins encore utilisés par des workflows restent en place jusqu'à validation des nouveaux builds.
