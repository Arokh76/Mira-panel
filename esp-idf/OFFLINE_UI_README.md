# Stage 2 — Mira 7 UI sans réseau

Test isolé après validation matérielle des couleurs, animation et tactile,
basé sur le même profil Waveshare 16MHz/16000 pixels bounce DMA.

- Essaie de lire les vraies pages Mira depuis la partition SPIFFS existante,
  \`/spiffs/miralvgl.json\`, sans jamais formater une partition.
- Démarre seulement NVS (sans erase), le pilote RGB, GT911, LVGL, la
  construction de l'interface native et ses timers.
- **Pas de Wi-Fi, HTTP, MQTT, Jeedom, caméra ni album-art worker.**
- Affichage d'un petit point vert qui se déplace en haut à droite (preuve de vie).
- \`OFFLINE_UI_READY\` et \`OFFLINE_UI_ALIVE\` dans les logs.
- Si la partition de configuration est vide, Mira affichera son écran
  de première configuration, mais le réseau est intentionnellement coupé.
- Aucune fonctionnalité de pilotage domestique n'est utilisable dans ce firmware.
- À flasher par USB à l'adresse \`0x0\` sans effacement complet de la flash.
- Ne pas fusionner avec firmware-beta avant retour d'essai matériel.
