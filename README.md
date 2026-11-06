# Mira Panel — firmware

Firmware pour Jeedom et MQTT. Ce dépôt conserve une source consolidée et des profils matériels spécifiques : WT32-SC01 Plus, Waveshare 4.3C et Open Nextion 2.8.

## Travail en cours : branche firmware-beta

- **Sources actives** : `firmware/source/consolidated/MIRA_PANEL/`, base 0.7.37.
- **Bêta WT32** : correctif de stockage JSON, thème Rose optionnel, logo Mira compact haute résolution au démarrage. Modifications issues de la version testée physiquement, conservées sans refonte.
- **Versions binaires conservées dans cette branche** : `firmware/releases/0.7.37/`. Attention : ces binaires publiés sont ceux de la version 0.7.37 antérieure aux modifications WT32 ci-dessus ; ils ne sont pas une compilation de la bêta Rose.
- **Manifeste des binaires publiés** : `firmware/targets.json` ; fichiers d'installation USB sous `docs/`.
- **Compilation** : `.github/workflows/build-mira.yml` construit séparément les profils. Ne pas interchanger les binaires des modèles.
- **Publication** : `.github/workflows/publish-mira-targets.yml` est réservé au processus validé depuis `main`.
- **Plugin Jeedom** : `jeedom/source/mira.zip` ; publication sur sa branche propre.

Les scripts correctifs historiques, vieux snapshots ZIP et anciennes releases ont été retirés de l'arborescence active. Ils restent accessibles dans l'historique Git et dans `backup/firmware-beta-pre-cleanup-20261106`.

Le portage ESP-IDF (4,3 et 7 pouces) se poursuit séparément. Cette opération de nettoyage ne modifie pas `main` ni les branches IDF.
