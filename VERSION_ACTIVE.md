# VERSION ACTIVE — Mira Panel (branche firmware-beta)

**LIRE CE FICHIER AVANT CHAQUE MODIFICATION.** Les chemins ci-dessous sont les références de travail, pas les dossiers historiques `firmware/experiments/`.

## WT32-SC01 Plus — Arduino
- **Sources à modifier** : [`Arduino/MIRA_PANEL/`](Arduino/MIRA_PANEL/) (snapshot Arduino consolidé, copié sans changement).
- **Dernier binaire WT32 connu** : `Mira_Panel_WT32_SC01_Plus_0.7.44.bin` (OTA, 1 900 992 octets).
- SHA-256 : `60eef0eac915d72b027b4aa86dad5a345700f07cecae9e5a49f9219a1ff1c4b7`.
- **Statut de dépôt** : ce fichier binaire exact est dans la bibliothèque de fichiers ChatGPT, **pas encore dans GitHub**. Les deux binaires `Arduino/bin/archives/0.7.37/` sont anciens et ne doivent pas être confondus avec la 0.7.44.

## Waveshare 7" STD — ESP-IDF
- **Sources à modifier** : [`esp-idf/`](esp-idf/) (`CMakeLists.txt`, `main/`, `partitions.csv`, etc.). Copie sans modification du commit `37f6cc94603b14d5b776c52c181aa99006486c13` (branche de test du 11/11/2026).
- **Dernier binaire 7" connu** : `MIRA7_ETATS_COULEURS_BOOT_RESYNC_HTTP24K_OTA.bin` (OTA, 2 184 752 octets).
- SHA-256 : `698d1f3a7cc7960215366cbfea84ee06a19f27929e0208b9776f9541aa2b1326`.
- **Factory USB correspondante** : `MIRA7_ETATS_COULEURS_BOOT_RESYNC_HTTP24K_FACTORY_USB_0x0000.bin` (2 250 288 octets), SHA-256 `9d773419d17903b5f5801ecd7961c9564fc466ec6da2547666bd14a63bdcc614`.
- **Contrôle de correspondance réalisé le 12/11/2026** : la métadonnée interne du binaire identifie une compilation du **10/11/2026 à 20:51:29** (horodatage intégré, fuseau non spécifié), IDF **v5.5.6**, projet `mira_idf_7std`, version applicative `0.7.37`. La sauvegarde `backup/mira7-bootresync-http24k-before-404-fix-20261111` pointe vers un commit du 10/11/2026 à 20:49:22 UTC. Comparaison **64 fichiers** du projet 7 pouces : **63/64 identiques** entre cette sauvegarde et `esp-idf/` ; la seule différence concerne `main/CMakeLists.txt`, où la branche plus récente ajoute `target_compile_definitions(... MIRA_PANEL_7_INCH=1)`. Ce sont de fortes indications sur la source d'origine, **pas une preuve de reproduction binaire bit à bit**.
- **Statut de dépôt** : ces deux binaires exacts sont dans la bibliothèque de fichiers ChatGPT, **pas encore dans GitHub**. **Le lien entre le commit des sources ci-dessus et les binaires n'est pas encore attesté : ne pas annoncer une reconstruction identique sans comparaison.** Aucun de ces binaires ne doit être remplacé par une autre compilation sous le même nom.

## Règles impératives
1. Le portage Arduino → ESP-IDF ne doit changer **aucun comportement** de Mira : UI, sauvegarde des paramètres, clé secrète, protocoles et widgets inchangés. Adapter seulement API/RTOS/pilotes/mémoire.
2. Avant une correction, lire ce fichier et partir **exclusivement du dossier cible** ci-dessus ; vérifier que sa provenance est la bonne avant de compiler.
3. Ne pas reprendre un ancien dossier `firmware/experiments/` ou une branche expérimentale comme vérité implicite.
4. Les anciens chemins subsistent temporairement car des workflows CI les utilisent. **Ne pas les supprimer ni les déplacer sans adaptation ET compilation testée.**
5. Ne jamais utiliser une image `FACTORY` dans l'OTA, ni effacer la configuration pour tester une correction.
6. Tant que les binaires ne sont pas importés dans GitHub, dire explicitement qu'ils manquent : ne pas prétendre que le rangement est totalement terminé.

## Travail restant
- Importer **sans modifier les octets** les 3 binaires actuellement conservés dans la bibliothèque ChatGPT dans `Arduino/bin/` et `esp-idf/bin/`.
- Adapter et valider les workflows de build aux nouveaux chemins, puis supprimer les anciens doublons et nettoyer les branches d'essai **après** sauvegarde et vérification.
- Identifier le commit de compilation exact de chaque binaire par une preuve de build, plutôt que par le nom du fichier.
