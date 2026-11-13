# Rôle des branches Mira Panel — réorganisation du 13 novembre 2026

**Développement firmware : `firmware-beta`. Développement du plugin Jeedom : `jeedom-beta`.**

| Branche | Contenu réel | À utiliser pour |
| --- | --- | --- |
| [`firmware-beta`](https://github.com/Arokh76/Mira-panel/tree/firmware-beta) | Arduino WT32 et ESP-IDF Waveshare 7" et 4,3C, sources et binaires de référence | **Firmware des écrans** — lire [VERSION_ACTIVE.md](VERSION_ACTIVE.md) |
| [`jeedom-beta`](https://github.com/Arokh76/Mira-panel/tree/jeedom-beta) | **Plugin Jeedom actuel en développement**, ancien contenu intégral de `beta`, version déclarée `1.2.28-beta.29` | **Développement du plugin Jeedom** |
| [`stable`](https://github.com/Arokh76/Mira-panel/tree/stable) | Branche Git du plugin Jeedom, version déclarée `1.2.29` | Référence Git « stable » ; **pas une preuve de publication dans le Market Jeedom** |
| [`main`](https://github.com/Arokh76/Mira-panel/tree/main) | Firmware Arduino 0.7.37 publié sur GitHub, fichiers installateur et manifestes OTA historiques | Distribution publique ; ne pas modifier lors du développement du 7" |
| `release/*` | Snapshots de versions firmware ou plugin | Historique / retour arrière |

## Pourquoi `stable` affiche 1.2.29 alors que la bêta affiche 1.2.28-beta.29

Le 30 octobre 2026, la [pull request #85](https://github.com/Arokh76/Mira-panel/pull/85) a fusionné dans `stable` un changement explicitement intitulé « Mira Panel 1.2.29 stable — bêta validée ». Il s'agit d'un **changement de branche Git**, et non d'une confirmation de diffusion dans le Market Jeedom. Le dépôt GitHub ne présentait **aucune GitHub Release** lors du contrôle du 13 novembre 2026.

Le développement de la bêta a continué après ce commit sur une autre lignée et sa version déclarée est restée `1.2.28-beta.29`. Comparer les numéros seuls ne suffit donc pas à classer la fraîcheur ou la complétude du code. **Ne pas promouvoir de version vers `stable` sans décision explicite.**

## Renommage de la branche bêta Jeedom — 13 novembre 2026

- `jeedom-beta` historique (version `1.2.26-dev`, commit `5f1a0846d8ec2bb866641159a249115116725689`) archivée au tag `archive/jeedom-beta-historique-20261113`.
- Le contenu intégral de l'ancienne branche `beta` (commit `658f7992a0091230ac800f452c2158880c080c5a`) est devenu le nouvel état de `jeedom-beta`, par **avance de branche sans écrasement d'historique** ; archive `archive/jeedom-beta-source-beta-20261113`.
- La branche nommée `beta` a été supprimée **seulement après vérification** que `jeedom-beta` contenait son commit exact.
- Le workflow `.github/workflows/lint-jeedom-beta.yml` suit désormais `jeedom-beta` pour les push et pull requests.
- Le code de `stable` et de `main` n'a pas été modifié durant cette opération.

## Nettoyage GitHub

De **58 branches à 9 branches**, en préservant les anciens historiques sous **50 tags Git** créés :
- `archive/mira-precleanup-20261113/*` : 40 tags d'essais historiques ;
- `archive/mira-final-20261113/*` : 8 tags de sauvegardes/nettoyage ;
- `archive/jeedom-beta-historique-20261113` et `archive/jeedom-beta-source-beta-20261113` : 2 tags du renommage Jeedom.

Le dépôt `firmware-beta` contient également `jeedom/source/mira.zip` pour les usages historiques d'intégration, mais **la source de développement du plugin est `jeedom-beta`**.

Voir [BRANCHES_AVANT_NETTOYAGE_20261113.md](BRANCHES_AVANT_NETTOYAGE_20261113.md) et [BRANCHES_FINALES_ARCHIVEES_20261113.md](BRANCHES_FINALES_ARCHIVEES_20261113.md) pour la traçabilité des branches archivées.
