# Rôles des branches Mira Panel (vérifié le 13 novembre 2026)

**Ne jamais confondre le firmware des écrans avec le plugin Jeedom.** Ces projets sont rangés dans un même dépôt GitHub, mais les branches ont des contenus différents.

| Branche | Contenu | Usage |
| --- | --- | --- |
| `firmware-beta` | Code et binaires Arduino WT32, ESP-IDF 7 pouces et ESP-IDF 4,3C | **Développement des écrans** ; commencer dans [VERSION_ACTIVE.md](VERSION_ACTIVE.md) |
| `beta` | **Plugin Jeedom** uniquement, code PHP/JS/CSS, tests ; `plugin_info/info.json` version `1.2.28-beta.29` au 13/11 | Développement / vérification du plugin Jeedom |
| `jeedom-beta` | Ancienne branche du **plugin Jeedom**, `plugin_info/info.json` version `1.2.26-dev`, datée du 20/10 | Historique conservé, **pas** la bêta actuelle du firmware |
| `stable` | Plugin Jeedom version `1.2.29` | Version stable du plugin |
| `main` | Firmware Arduino **0.7.37** publié, installateur Web, distributions et anciens manifestes | Publication publique / compatibilité des versions installées ; ne pas modifier pour tester Mira 7 pouces |
| `release/*` | Snapshots de versions firmware et plugin publiées | Retours arrière / historique de publication |

La branche `firmware-beta` contient aussi `jeedom/source/mira.zip` pour la publication/intégration, mais **le développement du plugin en tant que tel se fait sur `beta`**.

## Nettoyage du 13 novembre 2026

Le dépôt est passé de **58 à 10 branches**. Les 48 anciennes branches d'essais/sauvegardes restent accessibles sous forme de tags, créés et vérifiés avant suppression :
- `archive/mira-precleanup-20261113/*` (40 tags) ;
- `archive/mira-final-20261113/*` (8 tags).

Les commits et fichiers restent accessibles via leur tag Git ou leur SHA. Voir [BRANCHES_AVANT_NETTOYAGE_20261113.md](BRANCHES_AVANT_NETTOYAGE_20261113.md) et [BRANCHES_FINALES_ARCHIVEES_20261113.md](BRANCHES_FINALES_ARCHIVEES_20261113.md).
