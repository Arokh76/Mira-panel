# Mira Panel : une base de code, des firmwares matériels distincts

Les sources communes sont situées dans `firmware/source/consolidated/MIRA_PANEL/`.

Les cibles de compilation sont le WT32-SC01 Plus, le Waveshare ESP32-S3 Touch LCD 4.3C et l'Open Nextion Genius 2.8. Elles utilisent chacune leurs propres pilotes, tailles de flash et images OTA/FACTORY. Ne jamais échanger les images entre modèles.

Le build multi-cible est défini dans `.github/workflows/build-mira.yml` et le mécanisme de publication dans `.github/workflows/publish-mira-targets.yml`. Le processus public de publication demeure lié à `main` et nécessite la validation de la compilation.

La branche `firmware-beta` peut contenir du code plus récent que les binaires publiés (notamment les correctifs JSON, le thème Rose et le logo WT32). Les artefacts `firmware/releases/0.7.37` sont les derniers binaires publiés conservés dans la branche, et ne doivent pas être présentés comme une compilation de ces correctifs bêta.

Les anciens scripts correctifs et archives restent récupérables via Git, sans être gardés dans l'arborescence active.
