# Mira Panel — branche firmware-beta

**Deux cibles actives, deux sources officielles, deux binaires de référence.**

| Cible | Sources à modifier | Dernier binaire OTA archivé |
| --- | --- | --- |
| **WT32-SC01 Plus — Arduino** | [`Arduino/MIRA_PANEL/`](Arduino/MIRA_PANEL/) | [`Arduino/bin/Mira_Panel_WT32_SC01_Plus_0.7.44.bin`](Arduino/bin/Mira_Panel_WT32_SC01_Plus_0.7.44.bin) |
| **Waveshare 7" STD — ESP-IDF** | [`esp-idf/`](esp-idf/) | [`esp-idf/bin/MIRA7_ETATS_COULEURS_BOOT_RESYNC_HTTP24K_OTA.bin`](esp-idf/bin/MIRA7_ETATS_COULEURS_BOOT_RESYNC_HTTP24K_OTA.bin) |

## À consulter impérativement avant toute modification

- [**VERSION_ACTIVE.md**](VERSION_ACTIVE.md) : provenance des **sources exactes**, exécutions GitHub Actions d'origine, empreintes SHA-256 et protections à conserver.
- [**ORGANISATION_FIRMWARE_BETA.md**](ORGANISATION_FIRMWARE_BETA.md) : organisation des dossiers.
- [**Arduino/README.md**](Arduino/README.md) et [**esp-idf/README.md**](esp-idf/README.md) : instructions propres à chaque écran.

### Compilation

- **WT32** : [`.github/workflows/build-mira.yml`](.github/workflows/build-mira.yml), à partir de `Arduino/MIRA_PANEL/`.
- **Waveshare 7 pouces** : [`.github/workflows/build-mira-idf-waveshare7.yml`](.github/workflows/build-mira-idf-waveshare7.yml), à partir de `esp-idf/`.
- Les sorties de nouvelles compilations sont **des artefacts de développement**. Les derniers binaires vérifiés dans `Arduino/bin/` et `esp-idf/bin/` restent figés et ne doivent jamais être écrasés automatiquement.

### Règle fondamentale du portage

**ESP-IDF est une migration technique d'Arduino, pas une réécriture fonctionnelle.** Préserver strictement les widgets, la configuration, la clé secrète, Jeedom, les commandes, les états et les couleurs. Adapter uniquement les API, les pilotes, les accès réseau, la RAM/PSRAM et les piles indispensables.

Le projet historique ESP-IDF Waveshare 4,3" reste conservé séparément dans `firmware/experiments/esp-idf/mira-idf/` pour ne pas perdre ses travaux ; il ne constitue **pas** une base du 7 pouces.

Les anciennes copies des sources WT32 et 7 pouces dans `firmware/source/consolidated/` et `firmware/experiments/esp-idf/mira-idf-7/` ont été retirées après compilation réussie des nouveaux dossiers. Les branches temporaires sont tracées dans [BRANCHES_AVANT_NETTOYAGE_20261113.md](BRANCHES_AVANT_NETTOYAGE_20261113.md).

Le dépôt public `main`, les branches stables et les sauvegardes ne sont pas modifiés par ce classement.
