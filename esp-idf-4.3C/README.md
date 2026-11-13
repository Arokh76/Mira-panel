# Mira Panel — Waveshare ESP32-S3 Touch LCD 4.3C — ESP-IDF

**Portage ESP-IDF 4,3C préservé, indépendant du 7 pouces.** Ne pas supprimer ses fichiers lors du développement du 7".

- Sources : ce dossier (54 fichiers source conservés sans changement de code).
- Origine contrôlée : commit `2a9811cb885698401ee0652f2033e156d122f599`, les 54 fichiers identiques.
- Dernière compilation GitHub Actions confirmée pour cette source : [42285250664](https://github.com/Arokh76/Mira-panel/actions/runs/42285250664), 6 novembre 2026.
- Binaires exacts OTA et FACTORY : sous `bin/` après import contrôlé par SHA-256.
- Build de développement : `.github/workflows/build-mira-idf-waveshare43c.yml` ; ne remplace pas les binaires de référence.
- Ancien README conservé dans `README_HISTORIQUE_20261106.md`.

**Ne pas mélanger avec `esp-idf/`**, qui est réservé au Waveshare 7 pouces. La compilation n'est pas une validation de fonctionnement physique ; l'ancienne documentation décrit précisément ce qui était testé.

À ne pas faire : ne jamais utiliser une FACTORY USB comme OTA, ni supprimer les partitions de configuration.
