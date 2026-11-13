# Provenance du portage Mira7

Le projet source officiel se trouve **dans ce dossier** `esp-idf/`, et non dans une branche expérimentale.

Les sources viennent du commit exact `fe6867f5d6ef09d9fe96d0de04181942020c790d`. Le fichier `bin/MIRA7_ETATS_COULEURS_BOOT_RESYNC_HTTP24K_OTA.bin` provient de GitHub Actions run `42899018718` et a été copié sans recompilation après vérification SHA-256.

Le développement doit repartir de ce dossier et ne modifier que ce qui est demandé, en gardant la pile HTTP Jeedom de 24 KiB et la compatibilité Arduino.

Voir [VERSION_ACTIVE.md](../VERSION_ACTIVE.md).
