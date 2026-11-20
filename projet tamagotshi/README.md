# Tamagotchi V8.0 — 18 creatures, 3 familles, embranchements A/B

## Projet isole de Mira Panel

Le code du Tamagotchi se trouve dans `projet tamagotshi/` sur **la branche dediee `projet-tamagotshi`** du depot Arokh76/Mira-panel. Les sources et firmwares de Mira Panel restent inchanges ; l'action de compilation GitHub ici est specifique au Tamagotchi.

## Les evolutions (style de face)

| Famille | Bebe | Jeune | Ado A -> Adulte A | Ado B -> Adulte B |
|---|---|---|---|---|
| Terre | Poussin | Feuillu | Bourgeon -> Sylvan | Mousse -> Floralis |
| Eau | Gouti | Nageo | Aquilon -> Mareon | Rivage -> Lagrin |
| Ciel | Plumi | Aelo | Alize -> Zephyr | Eole -> Aerys |

**Oeuf commun** creme rose tachete : il tremble, se fissure, puis eclot. La famille est choisie **au hasard a l'eclosion avec une chance de 1/3** pour chaque milieu. L'adolescent prend ensuite la voie **A** (plutot activites/mini-jeux) ou **B** (plutot repas, hygiene, calins), avec tirage aleatoire si les scores sont proches. La branche est ensuite definitive et sauvegardee.

Temps de test: oeuf 0-1 min, bebe 1-7 min, jeune 7-15 min, adolescent 15-30 min et adulte a partir de 30 min **de temps actif**. Les 18 silhouettes sont des dessins proceduraux de face en RGB565 concus pour le TFT 135x240 ; les courbes et couleurs seront affinees selon le resultat sur le materiel reel. Les differences d'accessoires suivent la planche validee : feuilles/fleurs, nageoires bleues puis orange selon la voie, ailes et petits motifs lunaires.

## Compatibilite

Les sauvegardes des V7 sont lues sans effacer les statistiques, la personnalite, les objets et l'age. Pour un animal existant, une famille est attribuee par migration, avec une correspondance de couleur de l'ancienne forme adulte si possible ; **les nouvelles naissances** utilisent le tirage aleatoire. Les nouvelles cles NVS `family`, `branch`, `evo_v8` sont ajoutees au namespace existant `tama_v2`.

## Autres fonctions conservees

Menus, maison (jardin et chambre), decor, promenade et nuages en parallaxe, repas, bain avec pommeau, calins, cadeaux, 2 mini-jeux, sac, charge USB animee et securites batterie. Le SOIN utilise maintenant une aura et des croix de guerison, sans pansement kitsch. Le PROFIL affiche le nom de la creature, la famille, le stade et la voie.

## Flash

- [Tamagotchi_V8_0_TDisplay_V11_app.bin](firmware/Tamagotchi_V8_0_TDisplay_V11_app.bin) : **0x10000, sans ERASE**, conserve les sauvegardes.
- [Tamagotchi_V8_0_TDisplay_V11_full.bin](firmware/Tamagotchi_V8_0_TDisplay_V11_full.bin) : **0x0000**, reinstallation complete, risque de perdre le Tama.

Materiel : LilyGO / TTGO **T-Display V1.1 ORIGINAL** (ESP32 classique, ST7789 135 x 240), DIO, 40 MHz, flash 4 Mo. ESP-NOW reporte.

## V8.1 : portage sur le nouveau LilyGO T-Display S3 LCD 1,9 pouces

**Deux firmwares DISTINCTS** cohabitent sur la branche `projet-tamagotshi`, sans toucher au projet Mira Panel :

| Modele | ESP | LCD | Boutons (suivant/valider) | Reglage PlatformIO |
|---|---|---|---|---|
| TTGO T-Display V1.1 original | ESP32 | 135x240, SPI | GPIO35 / GPIO0 | `tdisplay_v11` |
| LilyGO T-Display S3 LCD 1,9" | ESP32-S3R8 | 170x320, parallele 8 bits | GPIO14 / GPIO0 | `tdisplay_s3_lcd` |

### Installation premiere fois : S3 LCD UNIQUEMENT

- **S3_app.bin** a `0x10000` seulement si le partitionnement est deja le bon.
- **S3_full.bin** a `0x0000` pour une premiere installation recommandee (16MB, bootloader S3 et partitions compris). Cela remplace le firmware de demonstration Wi-Fi et peut effacer ses donnees.
- **NE JAMAIS flasher le binaire TTGO V1.1 sur le S3.** Parametres ESP32-S3, QIO, 80MHz, 16MB.
- Si la carte ne passe pas en mode telechargement : maintenir **BOOT**, appuyer sur **RST**, relacher **RST** puis **BOOT** ; flasher et appuyer sur RST.

### Affichage et autonomie

- L'interface du Tama V8 reste dessinee logiquement en **135x240** pour conserver le meme cadrage et les memes animations ; le S3 la redimensionne en **170x300** (pixel-art, sans lissage) avec 10 pixels de marge haute et basse.
- GPIO15 est mis **HIGH** pour autoriser l'alimentation ecran ; GPIO38 commande le retroeclairage. Le controle batterie est sur GPIO4 avec un diviseur par 2 (contrairement au TTGO original, GPIO14 ne doit jamais servir de sortie !).
- **Precaution V8.1 S3** : LilyGO precise que GPIO4 ne donne PAS une tension cellule fiable lorsque l'USB-C est branche. Le pourcentage n'est affiché que si la mesure est plausible ; sur le S3 **la mise en hibernation automatique pour batterie faible est provisoirement desactivee**, le temps d'une validation sur batterie sans USB. L'ecran s'eteint toujours apres inactivite en mode batterie ; la protection physique de la cellule LiPo reste necessaire.
- Sur S3, l'animation "USB" reste **heuristique** (mesure batterie invalide), pas une indication de charge terminee. Aucun Wi-Fi n'est utilise par le jeu.

Code et build confinés a la branche `projet-tamagotshi`, sous-dossier `projet tamagotshi/` plus workflow Tamagotchi. La branche `main` n'est pas modifiee.


## V8.1.1 — S3 redémarrage en boucle : correctif TFT_eSPI

LilyGO documente des plantages de `TFT_eSPI` avec les cores Arduino ESP32 **apres 2.0.14**.
Le premier firmware S3 avait ete produit sur **2.0.17** via `espressif32@6.10.0`.
Ce correctif compile **seulement le S3 avec `espressif32@6.5.0` (Arduino 2.0.14)**, sans changer le moteur ni le binaire de l'ancien TTGO V1.1.

- Nouvelle image complete `Tamagotchi_V8_1_1_TDisplay_S3_LCD_full.bin` a **0x0000**.
- Ajout de messages `[TAMA-S3]` dans le terminal serie USB a 115200 bauds (boot, NVS, TFT, sprite, boucle).
- USB CDC active au demarrage pour faciliter l'identification de la cause si le reboot persiste.
- Si un ecran noir et/ou un redemarrage persiste : ne pas tenter plusieurs flashs a l'aveugle ; noter les **20 dernieres lignes de log** apres RST. En cas de souci, il est possible de restaurer la demonstration officielle.

Documentation : [LilyGO T-Display-S3](https://wiki.lilygo.cc/products/t-display-series/t-display-s3/) et [Core Arduino 2.0.14](https://community.platformio.org/t/confused-about-versions-using-esp32/38053).


## V8.1.2 — Correctif du watchdog de boot ROM S3
Le log utilisateur du **20 novembre 2026** montre une repetition de `rst:0x7 (TG0WDT_SYS_RST)`, `mode:QIO, clock div:1`, `load:0x3fce3808,len:0x44c`, `ets_loader.c 78`, sans aucun message `[TAMA-S3]`. Ce symptome intervient **avant l'application**.

Cause identifiee : la commande d'assemblage du firmware S3 complet forçait par erreur `--flash_mode qio`, alors que le **boot ROM du S3 ne peut charger le bootloader logiciel en QIO** ; utiliser DIO pour cette premiere phase, puis le bootloader peut activer QIO. Source : documentation officielle Espressif, [Boot Mode Selection](https://docs.espressif.com/projects/esptool/en/latest/esp32s3/advanced-topics/boot-mode-selection.html#software-bootloader-header-info).

Correction : `esptool --chip esp32s3 merge_bin --flash_mode dio --flash_freq 80m --flash_size 16MB`. En-tete attendu du firmware complet `E9 03 02 4F`. Une verification automatique du firmware assemblé interdit le retour accidentel en QIO.

**Flasher `Tamagotchi_V8_1_2_TDisplay_S3_LCD_full.bin` a l'adresse 0x0000**. Ne pas utiliser les anciens fichiers V8.1 ou V8.1.1 du S3 qui conservent l'en-tete incorrect. **Ne pas modifier les eFuses, ni les parametres dans ESP Flash Download Tools.** Le TTGO ESP32 original reste strictement inchange. La correction du boot ROM n'a pas encore ete validee en execution reelle sur la carte ; si le reset persiste, capturer le log 115200 du nouveau firmware.

## V8.1.3 — Correction des couleurs sur le S3 LCD

La V8.1.2 démarre sur le S3R8, mais le rose de l'œuf apparaît vert et les couleurs du décor sont incohérentes. Le sprite 16 bits est lu pixel par pixel avec `TFT_eSprite::readPixel()`, ce qui fournit la valeur RGB565 en ordre naturel. En revanche, lors de `lcd.pushImage()`, le chemin par défaut du pilote TFT_eSPI attend des pixels byte-swappés, comme ceux présents dans la RAM interne des sprites. **Correction : `lcd.setSwapBytes(true)` avant `pushImage()` et restauration de la valeur précédente ensuite.** Ceci s'applique uniquement au S3 et au tampon de redimensionnement 170x300, pas au TTGO V1.1. Le correctif DIO du bootloader (V8.1.2) reste en place.

Télécharger `firmware/Tamagotchi_V8_1_3_TDisplay_S3_LCD_full.bin` et flasher à **0x0000** sur le S3 pour ce test. Compiler sans erreur ne prouve pas à lui seul que l'affichage est corrigé : à valider par photo sur l'écran réel.


## V8.2.0 - S3 170x320 natif et graphismes de la planche
Le S3 n'agrandit plus une capture 135x240 : tous les decors, traits et elements du jeu sont **dessines nativement** dans un sprite 170x320, tout en conservant les coordonnees logiques de la V8. Les 18 creatures sont **extraites de la planche originale validee** et converties en images a palette RGB565 30 couleurs/RLE. La famille et le stade choisissent la bonne image, chargee en PSRAM depuis une partition flash `tama_art` dediee (type `data`, subtype `0x40`, offset **0xC90000**).

La compilation automatique GitHub publie un binaire `_BASE` **sans les images** car la planche source est un media de conversation, non publie dans le depot. Le fichier final **`Tamagotchi_V8_2_0_S3_LCD_full_ART.bin`** est cree en assemblant cette base et le bloc d'images dans la partition reservee. **C'est uniquement le firmware ART complet qu'il faut flasher sur un S3 pour voir les vraies creatures.** Flash ESP32-S3 DIO a `0x0000` ; pas besoin d'effacer les sauvegardes NVS. Un firmware BASE lance les anciens dessins de secours. L'ancien TTGO 135x240 reste strictement inchange.
