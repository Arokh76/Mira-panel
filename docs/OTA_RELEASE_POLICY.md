# Mira Panel — OTA et publications

Le firmware est construit par cible matérielle : WT32-SC01 Plus, Waveshare 4.3C et Open Nextion Genius 2.8. Ne jamais interchanger les binaires, même quand le numéro de version applicatif est identique.

La source consolidée est compilée par `.github/workflows/build-mira.yml`. La publication multi-cible depuis `main` valide les artefacts d'un même commit avant de modifier `firmware/targets.json`, les binaires OTA/FACTORY et les manifestes `docs/`.

**Version publiée conservée dans `firmware-beta` : 0.7.37.** Les modifications plus récentes de la source bêta WT32 (correctifs JSON, thème Rose et logo) ne sont pas automatiquement présentes dans ces binaires déjà publiés.

Les mises à jour OTA utilisent le profil matériel correspondant et préservent normalement les réglages. Les installations usine peuvent effacer des données. Les chemins historiques `firmware/version.json`, `firmware/version-wt32.json` et `firmware/Mira_Panel.bin` sont conservés pour éviter de modifier les mécanismes de transition des anciens appareils.

La validation par compilation ne remplace pas les essais physiques. Le profil Open Nextion doit notamment rester considéré comme distinctement à valider sur son propre matériel.
