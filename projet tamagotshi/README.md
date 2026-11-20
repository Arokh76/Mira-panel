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

## V8.2.1 — Correction du cadrage du T-Display S3

Le retour utilisateur (photo du 20 novembre 2026) montre un paysage deformé et un personnage mal intégré. La V8.2 avait en réalité deux bases de coordonnées : le décor hérité du TTGO était étiré par `TamaNativeSprite` de 135x240 à 170x320, tandis que les 18 sprites étaient placés directement en pixels réels. **La V8.2.1 remplace l'accueil S3 par une scène entièrement native 170x320** sans appliquer la conversion historique, en conservant le code ancien du TTGO V1.1. Les 18 sprites sont ceux de la même partition `tama_art` : aucun nouveau flash des images n'est nécessaire pour la V8.2.1. La zone des créatures va jusqu'à y=237, nom à y=255, menu réservé à y=281..319 ; aucun objet du décor n'est peint par-dessus les créatures. La chambre, le jardin, les messages et plusieurs animations utilisent désormais ces coordonnées natives sur l'accueil.

**Mise à jour sans effacement** : conserver la partition `tama_art` existante et flasher uniquement `Tamagotchi_V8_2_1_TDisplay_S3_LCD_BASE_app.bin` à `0x10000`, case ERASE décochée. Pour une première installation, utiliser les partitions et l'asset pack de V8.2.0 (inchangés). Le binaire BASE complet ne contient pas les images : ne pas l'utiliser seul comme firmware complet avec art. Résultat réel à confirmer sur l'écran.

## V8.2.2 — Correctif ciblé des bandes S3 et contrôle de framebuffer

Photo utilisateur du 20 novembre 2026 : grosses bandes horizontales sur le décor et le bas d'écran, alors que le personnage art s'affiche. La V8.2 utilisait `gfx.pushSprite(0,0)` directement depuis le sprite écran 170x320 16 bits en PSRAM. La V8.1.3 utilisait un tampon RGB565 séparé et `lcd.pushImage`, avec un rendu plus cohérent. Cette version rétablit **la copie contrôlée des pixels via `gfx.readPixel(x,y)` vers un tampon RGB565 de 170x320** et un seul `lcd.pushImage(0,0,170,320,...) ` avec `setSwapBytes(true)` ; sur échec d'allocation, l'ancien `pushSprite` est conservé comme secours.

Pour identifier une éventuelle autre cause, le terminal à **115200 bauds** affiche au premier rendu le format du framebuffer et les couleurs des pixels du fond de menu, du bouton et du décor AVANT transfert. En cas de bandes persistantes, ces lignes permettent de trancher entre corruption du dessin en mémoire et corruption de transfert LCD.

**Le moteur de jeu, les 18 images en partition `tama_art`, le cadrage S3 et le firmware TTGO V1.1 sont inchangés.** Installer uniquement le binaire application `Tamagotchi_V8_2_2_TDisplay_S3_LCD_BASE_app.bin` à **0x10000**, sans ERASE et sans toucher aux partitions ni aux images.


## V8.2.3 — Correction de cause racine : double echelle virtuelle

Le log V8.2.2 transmis par l'utilisateur indique `fond_menu=6D74 bouton=6D74`, alors que les valeurs attendues sont `08C6` et `118B`. `6D74` est EXACTEMENT la couleur de la colline `rgb565(107,173,164)` du jardin. Une colline dessinee pres du sol est donc presente dans les pixels du menu : le framebuffer est faux avant `lcd.pushImage()`.

**Cause racine trouvee dans les sources** : `TamaNativeSprite` redefinissait `drawFastHLine`, `drawPixel`, `fillRect` etc. pour multiplier les coordonnees 135x240 en 170x320. La V8.2.1 faisait `TFT_eSprite &ui = static_cast<TFT_eSprite&>(gfx)` en supposant supprimer l'echelle, MAIS ces methodes sont **virtuelles** dans `TFT_eSPI`. Les primitives `fillEllipse`, `drawCentreString`, les lignes du degrade et du menu faisaient toujours un dispatch vers l'ancien multiplicateur (deux echelles dans une meme image), d'ou les bandes horizontales, fond de menu couvert par la colline, et elements hors ecran.

**Correction** : abandon de la classe `TamaNativeSprite` et emploi direct de `TFT_eSprite gfx(&lcd)`, a l'echelle native 170x320 pour le S3. Les 18 sprites sont ecrits avec `drawPixel` natif. Les autres pages du jeu, historiquement 135x240, sont temporairement dessinees sans agrandissement dans un viewport centre 135x240 pour ne rien etirer. Le code TTGO V1.1 demeure intact du point de vue de l'affichage. La partition `tama_art` reste strictement identique.

Le log diagnostic de la V8.2.3 ajoute `[TAMA-S3] Validation coord. natifs: OK | menu 08C6/08C6 bouton 118B/118B | sprite 170x320` si les deux pixels clefs du menu sont enfin corrects. Une valeur differente fera apparaitre `ALERTE`, utile pour la suite.

**Flash** : uniquement `Tamagotchi_V8_2_3_TDisplay_S3_LCD_BASE_app.bin` a l'adresse **0x10000**, sans ERASE. Conserver toutes les autres partitions et les images V8.2.0. La reussite de la compilation ne suffit pas : valider le rendu sur le LCD physique.


## V8.3.0 — 18 sprites COMPLETS et 3 images d'animation par creature

Les visuels V8.2 avaient ete decoupes dans des boites trop courtes : par exemple Sylvan etait coupe avant ses petites pattes (original : y=110..220, V8.2 : y=122..211). Le personnage etait ensuite simplement translate de quelques pixels vers le haut ou le bas, sans expression propre.

V8.3 introduit un nouveau pack **TAMASPR2** : 18 creatures x 3 images = **54 images en RGB565 RLE**, issues de la meme planche utilisateur. Chaque creature a maintenant un etat normal, un clignement des yeux et un mouvement subtil du corps, des nageoires, des feuilles ou des ailes. Les pattes ont ete restituees/completees pour les ados et adultes lorsque les cartouches de noms les masquaient dans la planche. L'animation est plantee au sol (suppression du bond vertical factice), mais un petit pas horizontal subsiste durant les promenades.

La V8.3 lit aussi l'ancien pack **TAMASPR1** (18 images) : cela permet de mettre a jour le firmware seul, mais **les animations n'apparaissent qu'avec le nouveau pack**. Le code ne change ni le moteur graphique 170x320 valide V8.2.3, ni les sauvegardes NVS, ni le code historique du TTGO.

**Flasher deux binaires sans effacement :**
- `Tamagotchi_V8_3_0_TDisplay_S3_LCD_BASE_app.bin` a **0x10000**
- `Tamagotchi_V8_3_Art_18x3_S3.bin` a **0xC90000**
- conserver la table des partitions existante de la V8.2.0 ; ne pas cocher ERASE.

Le pack d'animations doit etre reconstruit depuis l'image de reference `a_detailed_colorful_clean_tamagotchi_evolution.png` (voir script fourni en annexe de livraison). Le firmware en soi ne contient pas les pixels : ils restent dans la partition `tama_art`.

La compilation S3 ne remplace pas un essai physique : verifier l'absence de morceaux d'etiquettes de la planche et la fidelite des pattes sur l'ecran.


## V8.3.1 — Vrais sprites chibi sur le LilyGO T-Display S3

Cette version n'est PAS une maquette : le moteur S3 existant charge des images RGB565
palettisees et compressees depuis la partition **tama_art** a l'adresse **0xC90000**.
Le nouveau format `TAMASPR3` contient 108 images : 18 creatures x 6 poses
(repos, clignement, mouvement, repas, mastication, joie). L'image est transparente
et posee au sol, au-dessus du nom et du menu. Les sprites proviennent du rendu
chibi approuve, notamment les images de Sylvan, avec les autres personnages
issus de la planche des 18 evolutions (animations secondaires encore provisoires).

**IMPORTANT : il faut mettre a jour l'application ET le pack d'images.**
Le firmware V8.3.0 ne reconnait pas TAMASPR3 ; mettre a jour sans pack affiche
l'ancien art ou les dessins de secours. La sauvegarde NVS n'est pas effacee.

- S3 uniquement : `firmware/Tamagotchi_V8_3_1_TDisplay_S3_LCD_BASE_app.bin` a **0x10000**.
- S3 uniquement : `Tamagotchi_V8_3_1_S3_CHIBI_18x6_ART.bin` a **0xC90000**.
- Ne pas cocher **ERASE** ; ne pas flasher le `_BASE_full.bin` sur une installation
  existante avec sprites, puisqu'il n'inclut pas le pack graphique.
- Conserver partitions V8.2.0+ ; le partitionnement n'a pas change.
- Les anciennes partitions TAMASPR1/TAMASPR2 restent compatibles avec cette
  version de l'application ; dans ce cas les anciennes animations sont conservees.
- Un essai sur le vrai LCD est encore necessaire pour confirmer couleurs,
  cadrage et animation du repas. La compilation seule ne valide pas le rendu.

Le S3 utilise toujours le moteur TFT_eSprite natif 170x320 corrige en V8.2.3 ;
le TTGO V1.1, les sauvegardes et le projet Mira Panel sont inchanges.


## V8.3.2 — Test de vrais sprites et animations chibi (S3)

**Objectif immediat : Sylvan.** Le rendu adopte le style chibi valide, avec des
membres complets et un personnage qui se couche reellement pour dormir.
Le nouveau format `TAMASPR4` contient **216 images (18 creatures x 12 poses)**.
Seul **Sylvan** dispose actuellement des 12 images chibi distinctes : repos,
clignement, respiration, attente du repas, gamelle, morsure, mastication,
contentement, sommeil couche, calin, jeu, bain. Les **17 autres creatures**
conservent provisoirement les visuels V8.3.1, reutilises comme images de
secours pour remplir les 12 emplacements ; cela ne constitue PAS 216 nouvelles
animations achevees.

Le code S3 selectionne une pose adaptee a l'effet en cours : les calins et cadeaux
ne montrent plus la gamelle ; le cadeau recoit sa propre boite animee ; le bain
affiche une douche et des bulles bien visibles. Le moteur TFT_eSprite V8.2.3,
les menus, la partition NVS et le TTGO V1.1 restent inchanges. Les anciens packs
TAMASPR1/2/3 restent compatibles avec le nouveau firmware.

**Deux fichiers, S3 uniquement, sans ERASE** :

1. `firmware/Tamagotchi_V8_3_2_TDisplay_S3_LCD_BASE_app.bin` a l'offset
   `0x10000` (nouvelle application publiee par le workflow GitHub).
2. `Tamagotchi_V8_3_2_S3_CHIBI_18x12_ART.bin` a l'offset `0xC90000`
   (pack graphique independant du firmware, a flasher en meme temps).

Ne pas utiliser le firmware V8.3.1 avec le pack TAMASPR4, car le vieux chargeur
ne connait pas ce format. Si le nouveau pack n'est pas present, le firmware
utilise l'ancien pack et les anciennes poses. Le pack a ete verifie par
relecture/decompression des 216 frames en Python, **pas sur l'ecran physique**.
Les positions exactes, les couleurs et le rendu temps reel restent a valider
sur le T-Display S3 de l'utilisateur avant de generaliser la methode aux autres
17 creatures.


## V8.3.3 — premier personnage anime EN CODE (S3 uniquement)

Cette version ne decoupe plus d'illustration pour l'adulte **Sylvan**
(famille Terre, branche A). Le personnage est genere a chaque image depuis
\`src/tama_sylvan_s3.h\` en primitives natives TFT_eSprite : feuilles, yeux,
masque du visage, queue, pattes, corps et accessoires. Aucun PNG, pack ART
supplementaire ni suppression du jeu n'est requis pour voir ce Sylvan.

Animations procedurales : respiration et clignement en repos ; bouche,
tete et patte articulees au repas ; silhouette reellement couchee au sommeil ;
pattes/ballon pendant le jeu ; coeurs et bras pendant les calins ; boite et
couvercle mobiles au cadeau ; douche et bulles au bain. Les positions restent
sur le canvas 170x320, loin des bords et au-dessus du nom y=255.

**Test :** mettre a jour uniquement le firmware S3
\`firmware/Tamagotchi_V8_3_3_TDisplay_S3_LCD_BASE_app.bin\` a l'adresse
**0x10000**, sans ERASE. Conserver l'ancienne partition \`tama_art\` a
\`0xC90000\` : elle sert encore aux 17 autres formes, qui restent inchanges.
Le firmware reste compatible avec TAMASPR1/2/3/4. La sauvegarde existante,
les seuils d'evolution, menus, commandes et moteur S3 de V8.2.3 sont conserves.
Le firmware TTGO V1.1 n'utilise pas ce fichier de dessin S3.

Pour verifier la bascule : le journal serie 115200 affiche
\`[TAMA-S3] V8.3.3 Sylvan procedural actif, aucun sprite RLE\`
quand un Sylvan adulte A est affiche. Le personnage doit etre teste sur
l'ecran physique avant de dupliquer le rendu aux 17 autres.


## V8.3.4 — corrections d'apres la video 2468.mp4

Retour reel sur le LilyGO S3 : les dessins proceduraux s'affichent et
les activites se declenchent, mais la collerette du personnage recouvrait
sa bouche, les feuilles et les pattes etaient excessivement geometriques
et les « Z » du sommeil flottaient pres de la fenetre.

Correctifs : feuilles dessinees par bandes courbes et contours nets,
collerette sous le menton, pattes plus petites, ventre arrondi pendant le
sommeil et « Z » animes pres de la tete. Un petit morceau de nourriture
se deplace maintenant jusqu'a la bouche pendant le repas. Le moteur LCD,
les sauvegardes NVS, le TTGO V1.1 et les autres creatures restent inchanges.

Sur S3 deja installe : flasher uniquement le binaire application
`firmware/Tamagotchi_V8_3_4_TDisplay_S3_LCD_BASE_app.bin` a `0x10000`
**sans ERASE** ; ne pas flasher le BIN FULL et ne pas remplacer `tama_art`.
La compilation et le rendu physique doivent etre verifies avant de
declarer les corrections validees.
