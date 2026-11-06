# Sources Arduino du firmware-beta

Source active : `consolidated/MIRA_PANEL/`.

Profils : WT32-SC01 Plus et Open Nextion 2.8. Le port Arduino Waveshare 4.3C est retiré en prévision d'un remplacement ESP-IDF indépendant. Le fichier de partitionnement 16 Mo pour Nextion a été conservé sous le nom `MiraPartitions16MB.csv`.

Le workflow `.github/workflows/build-mira.yml` construit directement la source consolidée et vérifie les deux profils.
