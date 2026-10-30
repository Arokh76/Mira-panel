# Mira Panel 0.7.35 — candidature stable

Sources issues de `firmware-beta` (30 octobre 2026), avec les correctifs d'affichage, synchronisation et Google Cast, sans copier les anciens paquets OTA de préversion.

## Publication des firmwares

La fusion du code dans `main` ne doit pas remplacer `firmware/targets.json` par le manifeste `firmware-beta`. Le pipeline `build-mira.yml` compile les trois cibles sur `main`; **uniquement si ce build est réussi**, `publish-mira-targets.yml` publie conjointement les binaires, SHA-256 et manifestes typés pour WT32 SC01 Plus, Waveshare 4.3C et Nextion 2.8.

Les anciens clients utilisant le flux non typé restent sur la version historique, sans basculement automatique vers un mauvais matériel.

## Validation

- WT32 SC01 Plus : tests fonctionnels de l'utilisateur sur l'affichage météo et la reprise d'état Jeedom ; rétablissement de la synchronisation confirmé chez Dimitri après correction du plugin.
- Waveshare 4.3C : compilation automatisée requise ; validation sur matériel distincte.
- Nextion 2.8 (ONX2432G028) : profil **expérimental** ; compilation automatisée requise et validation sur matériel distincte.
- Plugin Jeedom stable compagnon : `1.2.28` ; mise à jour nécessaire pour le remappage non destructif des IDs.

Ne pas présenter les compilations CI comme une validation matérielle complète. En cas de problème, les binaires et manifestes précédents restent dans l'historique Git pour retour arrière.
