# Sources du firmware Mira Panel

## Base consolidée 0.6.52

Le code complet utilisé par les deux écrans est sous
[`consolidated/MIRA_PANEL/`](consolidated/MIRA_PANEL/).

Il s'agit d'un export du code **Mira 0.6.52 déjà publié**, comprenant
les polices Montserrat d'accueil et de veille.

`.github/workflows/build-mira.yml` compile maintenant ce dossier
directement, avec des réglages par matériel :
- WT32 : LVGL 8.3.11, allocations LVGL en PSRAM, flash 8 Mo ;
- Waveshare : LVGL du fabricant 8.4, RGB 12 MHz, triple-buffer, flash 16 Mo.

L'ancien `Mira_Panel_Source_V0.4.3.zip` ainsi que les correctifs,
surcharges HTML et scripts historiques restent disponibles pour un retour
arrière. Le workflow d'export ponctuel est archivé sous
`.github/archive/workflows/consolidate-source-0652.yml`.

Les prochains travaux doivent modifier la source consolidée et **faire
compiler les deux profils** avant toute fusion.
