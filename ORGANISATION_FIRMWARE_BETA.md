# Organisation firmware-beta

- `Arduino/MIRA_PANEL/` : source Arduino WT32-SC01 Plus ; `Arduino/bin/` : binaires WT32 (archives étiquetées).
- `esp-idf/` : source ESP-IDF Waveshare 7 STD ; `esp-idf/bin/` : manifeste des binaires du 7 pouces en attente de copie.

Ces dossiers ont été créés **par copie** des sources existantes afin de préserver les anciens chemins utilisés par la CI. Le nettoyage des chemins `firmware/experiments/` et le repointage des workflows nécessitent validation des compilations avant suppression. Les anciennes branches GitHub restent intactes.

Toute correction ultérieure du portage 7 pouces doit partir de `esp-idf/` une fois la chaîne de build adaptée, et être comparée à la référence fonctionnelle Arduino, sans réécriture de la logique.
