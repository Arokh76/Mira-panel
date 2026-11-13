# Mira Panel — Waveshare 7" STD, ESP-IDF 5.5.6

**Source officielle du portage 7 pouces : ce répertoire** (`CMakeLists.txt`, `main/`, `sdkconfig.defaults`, `partitions.csv`).

Ces 64 fichiers source correspondent à la compilation du binaire `MIRA7_ETATS_COULEURS_BOOT_RESYNC_HTTP24K_OTA.bin`, commit `fe6867f5d6ef09d9fe96d0de04181942020c790d` du 10/11/2026.

**Dernier binaire de référence :** [`bin/MIRA7_ETATS_COULEURS_BOOT_RESYNC_HTTP24K_OTA.bin`](bin/MIRA7_ETATS_COULEURS_BOOT_RESYNC_HTTP24K_OTA.bin). SHA-256 : `698d1f3a7cc7960215366cbfea84ee06a19f27929e0208b9776f9541aa2b1326`.

La compilation de développement : `.github/workflows/build-mira-idf-waveshare7.yml`. Ses nouveaux résultats sont nommés `MIRA7_IDF_DEV_OTA.bin`, sans remplacer la référence.

Le code préserve la pile de 24 KiB du travailleur HTTP Jeedom. **Ne pas modifier le comportement d'Arduino pendant l'adaptation mémoire/API.** Les plantages chez Dimitri restent à analyser séparément.

Référence centrale : [VERSION_ACTIVE.md](../VERSION_ACTIVE.md).
