# Versions officielles de développement — Mira Panel firmware-beta

**Lire ce fichier avant toute modification du firmware.** Les seuls chemins de travail officiels sont `Arduino/MIRA_PANEL/` pour WT32 et `esp-idf/` pour Waveshare 7 STD.

## WT32-SC01 Plus : Arduino 0.7.44
- **Sources compilées vérifiées** : [`Arduino/MIRA_PANEL/`](Arduino/MIRA_PANEL/), copies identiques aux 62 fichiers du commit `dacb114deefcc9495ab781161449def50e288b8e`.
- **Compilation originale validée** : [GitHub Actions 43027107986](https://github.com/Arokh76/Mira-panel/actions/runs/43027107986), le 11 novembre 2026.
- **OTA officiel** : [`Arduino/bin/Mira_Panel_WT32_SC01_Plus_0.7.44.bin`](Arduino/bin/Mira_Panel_WT32_SC01_Plus_0.7.44.bin). SHA-256 : `60eef0eac915d72b027b4aa86dad5a345700f07cecae9e5a49f9219a1ff1c4b7`.
- **FACTORY officiel** : [`Arduino/bin/Mira_Panel_WT32_SC01_Plus_0.7.44_FACTORY.bin`](Arduino/bin/Mira_Panel_WT32_SC01_Plus_0.7.44_FACTORY.bin). SHA-256 : `2c2f306ecbd80e1219e4efe16f0b1e2eee6b5c4801d5b1c45f7ac20ead31a29e`.
- **Workflow à utiliser** : [`.github/workflows/build-mira.yml`](.github/workflows/build-mira.yml). Il lit les sources `Arduino/MIRA_PANEL/`, sans remplacer les binaires figés.

## Waveshare 7" STD : ESP-IDF BOOT_RESYNC_HTTP24K
- **Sources exactes du binaire HTTP24K archivé** : commit de compilation **`fe6867f5d6ef09d9fe96d0de04181942020c790d`** (64 fichiers, y compris `main/CMakeLists.txt`). Le dossier [`esp-idf/`](esp-idf/) était sa copie fidèle lors de la réorganisation, **mais il sert depuis de source de développement et peut évoluer**. Ne pas prétendre que le binaire figé a été compilé depuis les dernières modifications de ce dossier.
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
- Les nettoyages ont conservé les anciens historiques dans 50 tags Git (dont 2 pour le renommage Jeedom) ; 9 branches restent. La bêta du plugin est à nouveau **`beta`** (nom nécessaire pour les installations Jeedom), la bêta des écrans reste `firmware-beta`.
- Le code 4,3 pouces est conservé dans `esp-idf-4.3C/` avec son propre workflow actif `.github/workflows/build-mira-idf-waveshare43c.yml`.
- Les workflows ponctuels d'importation binaire et de nettoyage sont conservés dans `.github/archive/workflows/` pour audit, non exécutés automatiquement.

## Waveshare 4,3C ESP-IDF — projet conservé

- Sources d'application/configuration conservées dans [`esp-idf-4.3C/`](esp-idf-4.3C/), issues du commit `2a9811cb885698401ee0652f2033e156d122f599` (seul le test de parité a été adapté aux chemins et à la référence Arduino historique).
- Compilations OTA et FACTORY d'origine : [run GitHub Actions 42285250664](https://github.com/Arokh76/Mira-panel/actions/runs/42285250664), le 6/11/2026 ; fichiers **présents** dans [`esp-idf-4.3C/bin/`](esp-idf-4.3C/bin/), SHA-256 dans le README de ce répertoire.
- **Ce projet ne doit pas être supprimé** même si le développement courant porte sur le 7 pouces.

## Correctif de développement du 7 pouces — 13 novembre 2026

- Commit du **premier correctif post-réorganisation** : `7dc44006503071c68a93acc48a501dabb54418e4`.
- Dans `esp-idf/main/mira_beta_setup.c` : en première installation, la **clé Mira manquante peut être reconfigurée depuis l'AP local même si un profil Wi-Fi STA existait déjà**. Lorsque ce profil est présent, le formulaire confirme la clé et demande de redémarrer, sans imposer de ressaisir ou effacer le Wi-Fi.
- Dans `esp-idf/main/mira_recovery.c` : l'AP local de configuration reste accessible tant que la configuration est incomplète, même lorsque la station Wi-Fi a déjà une IP.
- **Aucune suppression NVS/SPIFFS, aucun changement Jeedom/MQTT, ni de partitionnement** dans ce correctif. Le binaire de référence `esp-idf/bin/MIRA7_ETATS_COULEURS_BOOT_RESYNC_HTTP24K_OTA.bin` reste inchangé.
- Le correctif produit **un binaire de développement distinct** via `.github/workflows/build-mira-idf-waveshare7.yml` : `MIRA7_IDF_DEV_OTA.bin`, à tester sur l'écran. Une compilation ne constitue pas une validation physique.
- Limite : cette correction vise **la récupération de clé**, et n'explique pas à elle seule les redémarrages causés éventuellement par la mémoire/PSRAM ou la création des widgets.

## Diagnostic `/status` pour distinguer la cause de `setup_required` (13 novembre 2026)

- Nouveau commit de développement : `76a6ceac8a3f3b99f40a9c2a9452870d1bc4d101`.
- Une lecture unique des états ajoute `setup_key_saved`, `setup_wifi_saved`, `setup_key_read_err` et `setup_wifi_read_err` au JSON de `/status`. Les erreurs sont les codes numériques ESP-IDF / NVS (0 signifie succès). Aucune donnée d'identification ni secret n'est affiché.
- **Contexte matériel** : un écran affichait `setup_required=true` malgré `widgets=9` et `jeedom_actions_ready=true`. Comme le boot initial ne lance pas les widgets si la configuration manque, il faut distinguer une lecture Wi-Fi transitoirement impossible d'une clé NVS réellement absente. Aucun diagnostic de perte de NVS n'est encore prouvé.
- Le champ `ssid: "Mira Panel"` du vieux JSON `/status` est une chaîne fixe, **pas** le SSID Wi-Fi actuellement connecté.
- Même si la compilation réussit, **ne pas remplacer le binaire HTTP24K de référence ni diffuser sans validation matérielle**. Pour comparer, recueillir deux appels `/status` sans redémarrage et les logs série lors du redémarrage, sans effacer la flash.

## Stabilisation de l'administration 7" — correctif suivant, non installé (13 novembre 2026)

**Référence terrain inchangée :** la version OTA compilée au commit `76a6ceac8a3f3b99f40a9c2a9452870d1bc4d101`, workflow réussi [43506935788](https://github.com/Arokh76/Mira-panel/actions/runs/43506935788), a affiché chez Dimitri `setup_required=false`, `setup_key_saved=true`, `setup_wifi_saved=true`, les deux erreurs à 0, 9 widgets, et Jeedom actif. Cela ne constitue **pas** une validation de stabilité longue durée. Ce binaire ne doit pas être remplacé automatiquement.

**Correctif de développement supplémentaire :**

- Une session qui **a démarré configurée** garde la page d'administration authentifiée : une lecture NVS transitoirement impossible ne ramène plus la racine `/` vers « Créer la clé locale ». Une première installation conserve son parcours de création de clé + Wi-Fi.
- Lorsque la clé serveur ne peut pas être lue, la vérification HTTP Basic renvoie **503** sans challenge `WWW-Authenticate` à la place d'un 401 répétitif. Aucune connexion n'est autorisée et aucune clé n'est mise en cache. Une clé disponible avec un mauvais mot de passe renvoie toujours 401.
- Trois lectures de configuration négatives consécutives (sondage du superviseur toutes les 3 s) sont nécessaires avant d'activer l'AP de récupération **si le Wi-Fi STA est encore connecté**. Une perte Wi-Fi réactive toujours immédiatement l'AP au cycle suivant.
- `/status` ajoute `setup_runtime_configured`, `auth_read_failures` et `auth_last_read_error` pour diagnostiquer les retours 503 sans exposer de secret.
- Les protections AP de `/save` et `/update`, les fichiers NVS/SPIFFS, les partitions OTA, les workers Jeedom et les widgets ne sont pas modifiés. Aucun effacement automatique.
- **Tests à effectuer avant installation :** compilation CI, vrai test avec identifiants corrects/incorrects, vérification de `/status` en longue durée, test du mode secours depuis l'AP, conservation des 9 widgets de Dimitri et de son mode IP manuel.

## 13 novembre 2026 — Parité de clé d'authentification WT32 → ESP-IDF

Le WT32 chargeait /reseau.json au démarrage et conservait la clé locale déchiffrée dans Uncrypt_Pass en RAM. Cette version de développement charge de même la clé NVS **une fois avant de démarrer Wi-Fi et HTTP**, puis authentifie exclusivement depuis la RAM. Les modifications explicites de clé (AP, assistant, API Jeedom, import ancien format) mettent à jour la copie RAM **après un nvs_commit réussi**. Le contrôle NVS de /status reste diagnostique, sans rediriger l'interface. Nouveau champ /status : auth_cached (booléen uniquement).

Un écran réellement dépourvu de clé **au redémarrage** conserve la procédure initiale via AP Mira ; la connexion domestique ne donne pas le droit de réinitialiser la clé. Aucune écriture/effacement NVS ou SPIFFS automatique. Pas de changement Jeedom, widgets, IP ni partition OTA. Tester l'OTA sur un écran avant de l'envoyer à Dimitri ; garder le binaire opérationnel du run 43506935788.
