# Maintenance — firmware-beta

Le projet Arduino compilé depuis `Arduino/MIRA_PANEL/` cible **WT32-SC01 Plus** et **Open Nextion 2.8**.

- Conserver les correctifs WT32 validés : JSON, thème Rose, logo compact.
- Le pilote **Waveshare43CDisplay.h** et la chaîne Arduino Waveshare 4.3C sont volontairement retirés.
- Le fichier `MiraPartitions16MB.csv` est le fichier de partitionnement 16 Mo nécessaire au Nextion (octets identiques à l'ancien fichier au nom Waveshare).
- Des conditions historiques Waveshare peuvent subsister, inactives, dans le code LVGL partagé. Ne pas les réactiver : le Waveshare 4.3C est désormais conservé séparément dans **`esp-idf-4.3C/`**, avec ses sources, binaires et son workflow.
- Ne pas flasher un binaire WT32 sur Waveshare. Les mécanismes historiques d'OTA non typés restent épinglés pour éviter toute migration erronée.
- Les binaires 0.7.37 archivés ne contiennent pas nécessairement le thème Rose de la source bêta actuelle.
- Aucune publication de firmware ou installation physique automatique ne découle de ce tri.

`main` reste distincte de `firmware-beta`, dont les portages IDF 4,3C et 7 pouces sont chacun dans leur propre dossier. Historique et sauvegarde avant tri : `backup/firmware-beta-before-wave-arduino-removal-20261106`.

**Avant chaque modification**, vérifier les références exactes dans [VERSION_ACTIVE.md](VERSION_ACTIVE.md).
