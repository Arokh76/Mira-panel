# Organisation de firmware-beta (13 novembre 2026)

| Matériel | Sources officielles | Binaires de référence |
| --- | --- | --- |
| WT32 Arduino 0.7.44 | `Arduino/MIRA_PANEL/` | `Arduino/bin/` |
| Waveshare 7" ESP-IDF HTTP24K | `esp-idf/` | `esp-idf/bin/` |
| Waveshare 4,3C ESP-IDF (conservé) | `esp-idf-4.3C/` | `esp-idf-4.3C/bin/` |

**Sources et binaires exacts retrouvés et corrélés aux compilations GitHub Actions d'origine.**

- WT32 : run 43027107986, sources commit `dacb114deefcc9495ab781161449def50e288b8e`.
- 7" : run 42899018718, sources commit `fe6867f5d6ef09d9fe96d0de04181942020c790d`.

Voir [VERSION_ACTIVE.md](VERSION_ACTIVE.md) pour les empreintes, règles de flash et chemins de build.

Les anciens doublons Arduino et ESP-IDF 7 pouces ont été supprimés après vérification des compilations de leurs nouveaux chemins. Le projet ESP-IDF 4,3 pouces, distinct, est encore conservé historiquement dans `esp-idf-4.3C/`. Les anciennes branches sont répertoriées dans `BRANCHES_AVANT_NETTOYAGE_20261113.md`.

**Nettoyage terminé :** 58 → 18 branches ; 40 tags d'archive préservent les anciens essais. Les trois workflows actifs de firmware de cette branche compilent WT32, Waveshare 7" et Waveshare 4,3C. Les workflows ponctuels d'archivage ont été déplacés dans `.github/archive/workflows/`.

- **Waveshare ESP-IDF 4,3C (à conserver)** : `esp-idf-4.3C/`, source et binaires propres, distinct du 7 pouces.
