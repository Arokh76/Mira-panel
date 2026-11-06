# Maintenance — firmware-beta

Le projet Arduino compilé depuis `firmware/source/consolidated/MIRA_PANEL/` cible **WT32-SC01 Plus** et **Open Nextion 2.8**.

- Conserver les correctifs WT32 validés : JSON, thème Rose, logo compact.
- Le pilote **Waveshare43CDisplay.h** et la chaîne Arduino Waveshare 4.3C sont volontairement retirés.
- Le fichier `MiraPartitions16MB.csv` est le fichier de partitionnement 16 Mo nécessaire au Nextion (octets identiques à l'ancien fichier au nom Waveshare).
- Des conditions historiques Waveshare peuvent subsister, inactives, dans le code LVGL partagé. Ne pas les réactiver : le prochain Waveshare 4.3C passera par **ESP-IDF**.
- Ne pas flasher un binaire WT32 sur Waveshare. Les mécanismes historiques d'OTA non typés restent épinglés pour éviter toute migration erronée.
- Les binaires 0.7.37 archivés ne contiennent pas nécessairement le thème Rose de la source bêta actuelle.
- Aucune publication de firmware ou installation physique automatique ne découle de ce tri.

`main` et les branches IDF 4,3/7 pouces restent séparées. Historique et sauvegarde avant tri : `backup/firmware-beta-before-wave-arduino-removal-20261106`.
