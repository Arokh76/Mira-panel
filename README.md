# Mira Panel — firmware-beta

**Point d'entrée obligatoire : [VERSION_ACTIVE.md](VERSION_ACTIVE.md).**

| Matériel | Sources de travail | Binaire de référence |
| --- | --- | --- |
| WT32 (Arduino) | [Arduino/MIRA_PANEL/](Arduino/MIRA_PANEL/) | `Mira_Panel_WT32_SC01_Plus_0.7.44.bin` |
| Waveshare 7 pouces (ESP-IDF) | [esp-idf/](esp-idf/) | `MIRA7_ETATS_COULEURS_BOOT_RESYNC_HTTP24K_OTA.bin` |

**Important :** les binaires exacts 0.7.44/24K sont encore conservés dans la bibliothèque de fichiers ChatGPT. Leurs SHA-256 et leur statut sont documentés dans `VERSION_ACTIVE.md`. Ne pas substituer un autre binaire ni reprendre les anciens dossiers expérimentaux.

---

# Mira Panel — firmware-beta

## Firmwares Arduino maintenus

- **WT32-SC01 Plus** : base 0.7.37, avec les derniers correctifs bêta validés (stockage des valeurs JSON, thème Rose optionnel et logo Mira compact).
- **Open Nextion Genius 2.8** : profil Arduino distinct, destiné aux essais matériels.

Sources : `firmware/source/consolidated/MIRA_PANEL/`. Compilation et tests : `.github/workflows/build-mira.yml`.

## Waveshare 4,3 pouces

**L'ancien firmware Waveshare 4.3C Arduino a été retiré de `firmware-beta`.** Son pilote, son build GitHub Actions, ses binaires et son manifeste USB ont été supprimés. Le port **ESP-IDF** 4.3C sera intégré séparément après compilation et validation sur l'écran réel.

Les modifications de cette branche ne changent pas les anciens firmwares publiés depuis `main`, ne déclenchent pas de mise à jour sur les écrans déjà installés et ne touchent pas au développement ESP-IDF 7 pouces.

## Versions / distribution

Les binaires `firmware/releases/0.7.37/` encore présents concernent WT32 et Nextion. Ces binaires ont été produits **avant** les derniers correctifs WT32 JSON/Rose/logo ; ils ne sont donc pas équivalents au code bêta actuel. Les nouveaux artefacts ne sont pas publiés automatiquement.

`firmware/targets.json` ne liste plus la cible Waveshare Arduino. L'installateur sous `docs/` reflète les profils Arduino restants dans cette branche. La version publique sur `main` est inchangée.

Sauvegarde de la branche avant suppression : `backup/firmware-beta-before-wave-arduino-removal-20261106`. Sauvegarde de la bêta WT32 validée : `backup/wt32-dimitri-rose-logo-valide-20261106`.
