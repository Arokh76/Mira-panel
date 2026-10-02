# Mira Panel

Mira Panel est un écran tactile domotique pour Jeedom basé sur le WT32-SC01 Plus.

Ce dépôt sert notamment à publier les métadonnées et binaires de mise à jour du firmware.

## Mise à jour du firmware

Le panneau vérifie la version publiée dans `firmware/version.json`.
Le binaire courant est publié sous `firmware/Mira_Panel.bin`.

## MQTT

- `mira/<nom_ecran>/Actions/<topic>`
- `mira/<nom_ecran>/States/<topic>`

Client ID : `mira-panel-XXXXXX`.
