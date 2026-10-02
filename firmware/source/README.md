# Sources du firmware Mira Panel

Le workflow de compilation utilise actuellement l'archive `Mira_Panel_Source_V0.4.3.zip` placée dans ce dossier.

L'archive contient le dossier de sketch Arduino `MIRA_PANEL/`, avec le fichier principal `MIRA_PANEL.ino`, les sources C/C++, les pages HTML embarquées, `lv_conf.h` et les fichiers auxiliaires nécessaires à la compilation.

La version 0.4.3 contient notamment le remaniement de l'interface Web, le nettoyage des traces série et la base MQTT Mira validée sur l'écran.

Cette organisation temporaire permet de compiler le firmware sur GitHub Actions tout en gardant le dépôt léger et simple à maintenir. Les sources pourront être décompressées dans le dépôt plus tard si besoin.
