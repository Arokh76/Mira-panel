# Mira Panel

Écran domotique pour Jeedom : **WT32-SC01 Plus**, **Waveshare ESP32-S3 Touch LCD 4.3C** et, à terme, **Open Nextion ONX2432G028 (2,8 pouces)**.

Le dépôt contient les sources, les correctifs successifs et les automatisations de compilation des deux matériels.

**Objectif du projet : un seul code source Mira, une seule version et une seule publication, avec des binaires adaptés à chaque matériel et choisis automatiquement par l'installateur.** Les fichiers propres aux cartes ne sont pas interchangeables. L'installation automatique multi-matériel n'est pas encore développée ; l'architecture cible compte aussi le futur Open Nextion 2,8 pouces.

Voir [FIRMWARE_UNIVERSEL.md](FIRMWARE_UNIVERSEL.md) pour la stratégie de build, de détection avant flash USB, de publication unique et d'OTA par profil.

## Versions conservées

| Cible | Version | État | Source de build |
| --- | --- | --- | --- |
| WT32-SC01 Plus (8 Mo) | 0.6.38 testée, 0.6.5 publiée | Mise à jour 0.6.38 installée et validée sur appareil par l'utilisateur | `.github/workflows/build-mira.yml` (preview) et `build-firmware.yml` (publication actuelle) |
| Waveshare 4.3C (16 Mo, 800×480) | 0.6.38 | Testée sur écran réel ; stabilité encourageante, validation longue durée en cours | `.github/workflows/build-mira.yml` |
| Open Nextion ONX2432G028 (16 Mo, 240×320) | Portage prévu | Matériel non encore porté dans Mira | À intégrer au même workflow |

**Waveshare 0.6.38** : port LVGL 8.4 Waveshare, trois buffers RGB, rafraîchissement complet et pixel clock réduit de **16 MHz à 12 MHz**. Cette seule modification de la 0.6.37 a supprimé les artefacts observés pendant les premiers essais. Il reste à confirmer la stabilité sur la durée.

Le premier build Waveshare 0.6.38 validé par GitHub Actions est consultable ici :
[Build 0.6.38](https://github.com/Arokh76/Mira-panel/actions/runs/38003987693).
L'artefact `Mira_Panel_Waveshare_4.3C_0.6.38_preview` contient un binaire **OTA** ainsi qu'une image **FACTORY** ; l'archive GitHub Actions a une durée de rétention limitée.

**Version 0.6.39 en préparation** : nouveau Web Installer par matériel et OTA ciblée. Les appareils installés en 0.6.38 ne sont pas mis à jour automatiquement tant que cette version n'est pas publiée et validée.

**Important** : la 0.6.38 est une **base de code commune**, compilée avec un profil par matériel. Les deux appareils existants ont été testés sur matériel. Les artefacts CI sont deux ZIP indépendants avec des `.bin` OTA explicitement nommés. `firmware/version.json`, `firmware/Mira_Panel.bin` et l'installateur `docs/` publient pour l'instant **WT32 0.6.5** : la publication multi-profils 0.6.38 et l'auto-détection initiale ne sont pas encore actives.

## Workflows actifs

- `build-mira.yml` : compile et génère **deux firmwares OTA distincts de version 0.6.38** : WT32-SC01 Plus (8 Mo) et Waveshare 4.3C (16 Mo).
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
