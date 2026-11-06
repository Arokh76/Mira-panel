# Mira Panel — source consolidée

Le dossier `MIRA_PANEL/` contient l'intégralité du code utilisé par la compilation multi-cible.

Base 0.7.37, enrichie de correctifs bêta WT32 testés : gestion des valeurs JSON, thème Rose optionnel, logo Mira compact au démarrage. Le dépôt contient également les ressources LVGL, Web, MQTT, caméra et les pilotes matériels.

Le build prend directement cette source, avec les réglages spécifiques WT32/Waveshare/Nextion dans `.github/workflows/build-mira.yml`. Les anciens scripts de reconstruction par versions et fichiers ZIP ne sont plus requis.
