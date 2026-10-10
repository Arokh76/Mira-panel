# Mira Panel

Écran domotique pour Jeedom : **WT32-SC01 Plus** et **Waveshare ESP32-S3 Touch LCD 4.3C**.

Le dépôt contient les sources, les correctifs successifs et les automatisations de compilation des deux matériels. **Les firmwares ne sont pas interchangeables** : ne pas flasher un binaire Waveshare sur un WT32 (ni l'inverse).

## Versions conservées

| Cible | Version | État | Source de build |
| --- | --- | --- | --- |
| WT32-SC01 Plus (8 Mo) | 0.6.5 | Version publiée pour la mise à jour automatique | `.github/workflows/build-firmware.yml` |
| Waveshare 4.3C (16 Mo, 800×480) | 0.6.38 | Testée sur écran réel ; stabilité encourageante, validation longue durée en cours | `.github/workflows/build-waveshare-0638.yml` |

**Waveshare 0.6.38** : port LVGL 8.4 Waveshare, trois buffers RGB, rafraîchissement complet et pixel clock réduit de **16 MHz à 12 MHz**. Cette seule modification de la 0.6.37 a supprimé les artefacts observés pendant les premiers essais. Il reste à confirmer la stabilité sur la durée.

Le dernier build Waveshare validé par GitHub Actions est consultable ici :
[Build 0.6.38](https://github.com/Arokh76/Mira-panel/actions/runs/38003987693).
L'artefact `Mira_Panel_Waveshare_4.3C_0.6.38_preview` contient un binaire **OTA** ainsi qu'une image **FACTORY** ; l'archive GitHub Actions a une durée de rétention limitée.

**Attention** : `firmware/version.json`, `firmware/Mira_Panel.bin` et l'installateur `docs/` concernent toujours le **WT32 0.6.5**. Ils ne doivent pas être remplacés par un binaire 16 Mo du Waveshare.

## Workflows actifs

- `build-waveshare-0638.yml` : firmware Waveshare 0.6.38 et vérification de compilation WT32.
- `build-firmware.yml` : build de publication WT32 0.6.5.
- `publish-firmware.yml` et `sync-web-installer.yml` : publication WT32 ; ne pas utiliser pour le Waveshare.
- `pages.yml` : déploiement de l'installateur web.
- `build-waveshare-official-reference.yml` : firmware Waveshare d'origine, outil de diagnostic indépendant de Mira.

Les **17 anciens workflows d'essai** sont archivés sous `.github/archive/workflows/` : ils restent lisibles dans Git, mais ne s'exécutent plus depuis GitHub Actions.

## Sources et maintenance

La compilation reconstruit actuellement les sources depuis `firmware/source/Mira_Panel_Source_V0.4.3.zip`, les `firmware/patches/`, les surcharges HTML et **tous les scripts successifs** de `firmware/scripts/`. Il ne faut pas supprimer ces scripts ou réécrire la chaîne sans reconstruire un snapshot source consolidé puis vérifier les deux cibles.

Voir [MAINTENANCE.md](MAINTENANCE.md) pour le nettoyage réversible, les mises à jour OTA et la procédure de retour arrière.

## MQTT

- `mira/<nom_ecran>/Actions/<topic>`
- `mira/<nom_ecran>/States/<topic>`

Client ID : `mira-panel-XXXXXX`.
