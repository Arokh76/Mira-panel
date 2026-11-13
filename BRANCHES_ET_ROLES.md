# Rôle des branches Mira Panel — état au 13 novembre 2026

**`firmware-beta` = développement des firmwares des écrans. `beta` = développement du plugin Jeedom.**

| Branche | Contenu | Usage |
| --- | --- | --- |
| [`firmware-beta`](https://github.com/Arokh76/Mira-panel/tree/firmware-beta) | Sources et binaires Arduino WT32, ESP-IDF Waveshare 7 pouces et 4,3C | Développement des firmwares — commencer par [VERSION_ACTIVE.md](VERSION_ACTIVE.md) |
| [`beta`](https://github.com/Arokh76/Mira-panel/tree/beta) | Plugin Jeedom actuel, version déclarée `1.2.28-beta.29` | **Branche bêta du plugin utilisée par Jeedom** |
| [`stable`](https://github.com/Arokh76/Mira-panel/tree/stable) | Plugin Jeedom, version déclarée `1.2.29` | Branche Git stable, **sans preuve de publication dans le Market Jeedom** |
| [`main`](https://github.com/Arokh76/Mira-panel/tree/main) | Distributions Arduino 0.7.37, Web Installer et manifestes historiques | Publication GitHub des firmwares |
| `release/*` | Versions archivées firmware ou plugin | Historique et retours arrière |

## Important — retour au nom `beta`

Le 13 novembre 2026, la branche `beta` avait été temporairement renommée `jeedom-beta`. Or les installations du plugin dans Jeedom étaient déjà configurées pour suivre `beta`, ce qui a entraîné une erreur.

**Correction effectuée :**
- `beta` a été recréée depuis le dernier commit de `jeedom-beta`, sans perte du code du plugin ;
- le workflow `.github/workflows/lint-jeedom-beta.yml` exécute de nouveau les tests à chaque push ou pull request vers **`beta`** ;
- les tests du plugin ont réussi : [GitHub Actions 43503559351](https://github.com/Arokh76/Mira-panel/actions/runs/43503559351) ;
- l'ancienne référence `jeedom-beta` a été sauvegardée sous `archive/jeedom-beta-avant-retour-beta-20261113`, vérifiée puis supprimée, après validation de `beta` ;
- la branche `stable` et les firmwares n'ont pas été modifiés.

Référence du nettoyage : [GitHub Actions 43503666733](https://github.com/Arokh76/Mira-panel/actions/runs/43503666733).

## Pourquoi `stable` affiche 1.2.29

La [pull request #85](https://github.com/Arokh76/Mira-panel/pull/85) a fusionné la promotion Git `1.2.29` dans `stable` le 30 octobre 2026. **Cela ne prouve pas une publication sur le Market Jeedom.** La branche `beta` a poursuivi son propre développement et déclare encore `1.2.28-beta.29`. Ne pas promouvoir ou publier de stable sans demande explicite.

## Historique préservé

Le dépôt contient **9 branches actives ou de publication**. Les précédents nettoyages ont remplacé 48 branches temporaires par des tags, et les deux opérations autour du nom Jeedom ont ajouté les tags `archive/jeedom-beta-historique-20261113`, `archive/jeedom-beta-source-beta-20261113` et `archive/jeedom-beta-avant-retour-beta-20261113`.

La branche `firmware-beta` conserve aussi un ancien package `jeedom/source/mira.zip` à des fins de compatibilité, mais **le code du plugin est maintenu sur `beta`**.
