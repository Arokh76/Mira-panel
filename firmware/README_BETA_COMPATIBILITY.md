# Attention : anciens canaux publics de mise à jour

La branche `firmware-beta` conserve intentionnellement des **manifestes et binaires historiques** dans `firmware/` et `docs/` pour préserver la compatibilité des anciennes installations et éviter un changement implicite des URLs OTA.

**Ce ne sont PAS les firmwares actuels à modifier ou à tester.**

- `firmware/targets.json` : distribution publique 0.7.37, URLs pointant vers `main`.
- `firmware/version.json` : alias historique WT32 (version déclarée 0.7.14), URL pointant vers `main`.
- `firmware/releases/0.7.37/` et `docs/firmware/` : binaires de publication historiques.
- `docs/manifest*.json` : manifestes de l'installateur public, versions historiques.

**Emplacements officiels de développement** :
- WT32 0.7.44 : [`Arduino/`](../Arduino/).
- Waveshare 7 pouces ESP-IDF HTTP24K : [`esp-idf/`](../esp-idf/).
- Waveshare 4,3C ESP-IDF : [`esp-idf-4.3C/`](../esp-idf-4.3C/).

Lire [`VERSION_ACTIVE.md`](../VERSION_ACTIVE.md) avant toute modification. Ne pas utiliser `firmware/targets.json` ou `firmware/version.json` pour sélectionner un binaire de développement. Ne pas modifier leur version ni leurs URLs vers `main` dans le cadre d'un simple rangement.
