# Validation de compilation IDF 4,3 pouces — 2026-11-05

Ce fichier trace la demande de compilation complète du portage sur la branche expérimentale, sans modification de `main`, de `stable` ou de `firmware-beta`.

Le workflow `build-mira-idf-waveshare.yml` doit exécuter les contrôles de parité, compiler ESP-IDF 5.5.6 / LVGL 9.6.0 et produire les binaires `OTA` et `FACTORY_USB_0x0` destinés aux tests matériels.

La compilation ne vaut pas validation physique du panneau.
