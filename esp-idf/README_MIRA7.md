# Mira Panel — ESP-IDF, Waveshare 7 pouces STD

Projet ESP-IDF : fichiers de ce répertoire (`CMakeLists.txt`, `main/`, `partitions.csv`, etc.). Source copiée sans modification de `test/waveshare7-jeedom-http-actions-psram` commit `37f6cc94603b14d5b776c52c181aa99006486c13` (11 novembre 2026).

**ATTENTION** : ces sources ne sont **pas attestées identiques** à celles utilisées pour produire le binaire OTA nommé ci-dessous. Ne pas présenter ce répertoire comme la source reproductible de ce binaire sans vérification du commit de build.

Dernier OTA connu : `MIRA7_ETATS_COULEURS_BOOT_RESYNC_HTTP24K_OTA.bin` (2 184 752 octets, SHA-256 `698d1f3a7cc7960215366cbfea84ee06a19f27929e0208b9776f9541aa2b1326`).

Image USB associée dans la bibliothèque ChatGPT : `MIRA7_ETATS_COULEURS_BOOT_RESYNC_HTTP24K_FACTORY_USB_0x0000.bin` (SHA-256 `9d773419d17903b5f5801ecd7961c9564fc466ec6da2547666bd14a63bdcc614`).

Ces binaires ne sont pas présents dans les objets Git du dépôt ; ils doivent être importés **sans modification** avant de considérer le rangement terminé. `bin/` conserve ce manifeste pour éviter toute substitution accidentelle.

Ce dossier est destiné exclusivement au Waveshare 7 pouces STD : ne pas utiliser son firmware sur WT32 ou Waveshare 4,3C.
