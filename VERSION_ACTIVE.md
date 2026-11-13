# Versions officielles de développement — Mira Panel firmware-beta

**Lire ce fichier avant toute modification du firmware.** Les seuls chemins de travail officiels sont `Arduino/MIRA_PANEL/` pour WT32 et `esp-idf/` pour Waveshare 7 STD.

## WT32-SC01 Plus : Arduino 0.7.44
- **Sources compilées vérifiées** : [`Arduino/MIRA_PANEL/`](Arduino/MIRA_PANEL/), copies identiques aux 62 fichiers du commit `dacb114deefcc9495ab781161449def50e288b8e`.
- **Compilation originale validée** : [GitHub Actions 43027107986](https://github.com/Arokh76/Mira-panel/actions/runs/43027107986), le 11 novembre 2026.
- **OTA officiel** : [`Arduino/bin/Mira_Panel_WT32_SC01_Plus_0.7.44.bin`](Arduino/bin/Mira_Panel_WT32_SC01_Plus_0.7.44.bin). SHA-256 : `60eef0eac915d72b027b4aa86dad5a345700f07cecae9e5a49f9219a1ff1c4b7`.
- **FACTORY officiel** : [`Arduino/bin/Mira_Panel_WT32_SC01_Plus_0.7.44_FACTORY.bin`](Arduino/bin/Mira_Panel_WT32_SC01_Plus_0.7.44_FACTORY.bin). SHA-256 : `2c2f306ecbd80e1219e4efe16f0b1e2eee6b5c4801d5b1c45f7ac20ead31a29e`.
- **Workflow à utiliser** : [`.github/workflows/build-mira.yml`](.github/workflows/build-mira.yml). Il lit les sources `Arduino/MIRA_PANEL/`, sans remplacer les binaires figés.

## Waveshare 7" STD : ESP-IDF BOOT_RESYNC_HTTP24K
- **Sources compilées vérifiées** : [`esp-idf/`](esp-idf/). Les **64 fichiers du projet** (dont `main/CMakeLists.txt`) sont identiques au commit de compilation **`fe6867f5d6ef09d9fe96d0de04181942020c790d`**, après copie vers le nouveau chemin.
- **Compilation originale validée** : [GitHub Actions 42899018718](https://github.com/Arokh76/Mira-panel/actions/runs/42899018718), le 10 novembre 2026.
- **OTA officiel** : [`esp-idf/bin/MIRA7_ETATS_COULEURS_BOOT_RESYNC_HTTP24K_OTA.bin`](esp-idf/bin/MIRA7_ETATS_COULEURS_BOOT_RESYNC_HTTP24K_OTA.bin). SHA-256 : `698d1f3a7cc7960215366cbfea84ee06a19f27929e0208b9776f9541aa2b1326`, taille 2 184 752 octets.
- **FACTORY USB 0x0000** associée : conservée dans la bibliothèque de fichiers ChatGPT, SHA-256 `9d773419d17903b5f5801ecd7961c9564fc466ec6da2547666bd14a63bdcc614`. Elle **n'est pas** encore dans ce GitHub. Ne pas substituer une image usine d'une autre compilation.
- **Workflow de développement** : [`.github/workflows/build-mira-idf-waveshare7.yml`](.github/workflows/build-mira-idf-waveshare7.yml). Le binaire généré porte le nom **`MIRA7_IDF_DEV_OTA.bin`**, distinct du binaire officiel.
- **Piles** : source de référence : `xTaskCreateWithCaps(jeedom_action_worker, "mira_jeedom_http", 24576, ...)`. Le champ `http_stack` de `/status` suit la pile HTTPD ; ne pas le confondre avec celle du travailleur Jeedom.

## Règles obligatoires
1. **Portage fidèle Arduino → ESP-IDF** : aucune modification fonctionnelle sans demande explicite. Même UI, configuration, clé secrète, Jeedom, widgets, états et commandes ; adapter seulement HAL, réseau, mémoire, piles et API.
2. **Ne jamais repartir d'une branche `experiment/` ou `test/` par défaut.** L'unique base de travail est `firmware-beta` + dossier cible.
3. **Ne jamais écraser les binaires de référence.** Toute nouvelle compilation a un autre nom et sa propre provenance.
4. **OTA ≠ FACTORY**. Ne jamais flasher FACTORY depuis l'interface OTA ni effacer les partitions de configuration pour un test.
5. Garder les protections anti-commandes Jeedom involontaires, les protections flash/SPIFFS, et la pile Jeedom de 24 KiB.
6. Toute correction du 7 pouces doit être validée en compilation puis sur écran avant d'être considérée stable ; des redémarrages ont été rapportés chez Dimitri.

## Historique et nettoyage
- Ancien chemin Arduino : `firmware/source/consolidated/MIRA_PANEL/` — **ancien doublon supprimé le 13/11/2026 après compilations réussies**.
- Ancien chemin ESP-IDF 7" : `firmware/experiments/esp-idf/mira-idf-7/` — **ancien doublon supprimé le 13/11/2026 après compilations réussies**.
- Le projet ESP-IDF 4,3" séparé (`esp-idf-4.3C/`) n'est **pas** la source du 7" et reste conservé pour éviter d'effacer ce travail.
- Les anciennes branches ne constituent pas des bases de développement : leur suppression doit préserver leurs commits utiles.

## Réorganisation vérifiée le 13 novembre 2026

- Les deux builds canoniques WT32 et 7" ont réussi après suppression des doublons.
- 40 branches temporaires ont été remplacées par 40 tags d'archive avant suppression ; 18 branches utiles restent.
- L'ancien code 4,3 pouces est conservé dans `esp-idf-4.3C/`, sans workflow de build actif.
- Les workflows ponctuels d'importation binaire et de nettoyage sont conservés dans `.github/archive/workflows/` pour audit, non exécutés automatiquement.

## Waveshare 4,3C ESP-IDF — projet conservé

- Sources exactes (54 fichiers) dans [`esp-idf-4.3C/`](esp-idf-4.3C/), identiques au commit `2a9811cb885698401ee0652f2033e156d122f599`.
- Compilations OTA et FACTORY d'origine : [run GitHub Actions 42285250664](https://github.com/Arokh76/Mira-panel/actions/runs/42285250664), le 6/11/2026 ; fichiers attendus dans [`esp-idf-4.3C/bin/`](esp-idf-4.3C/bin/), SHA-256 dans le README de ce répertoire.
- **Ce projet ne doit pas être supprimé** même si le développement courant porte sur le 7 pouces.
