# Mira Camera beta 0.7.23 — bouton simple + JPEG direct

**Branche expérimentale uniquement. Ne pas fusionner avec main/stable avant essais physiques.**

## Interface — sans miniature

Le widget Caméra reprend exactement l'apparence d'un bouton Mira classique :
petit pictogramme caméra à gauche, **nom à droite**. Pas de vignette JPEG,
d'aperçu, de rafraîchissement ni de requête HTTP sur la page principale.

- Appui : affiche une vue plein écran et lance le téléchargement des images.
- Retour, veille ou reprogrammation : arrêt du téléchargement et libération
  des images en PSRAM.
- Rafraîchissement JPEG/BMP lancé au plus toutes les **250 ms** (objectif jusqu'à ~4 images/s suivant Wi-Fi, résolution et temps de décodage), pas de flux RTSP/MJPEG/H.264 continu.
- Tous les écrans (WT32, Waveshare, Nextion) partagent le même code UI.

## Configuration Jeedom

**Plugin beta 1.2.28-beta.4 minimum** et **firmware 0.7.23 beta**.

1. Dans « Programmer un écran ESP32 », créer **Caméra**, nom et page
   uniquement, puis programmer l'écran.
2. Ouvrir l'équipement Mira dans Jeedom, onglet **Commandes**.
3. Dans la commande « Caméra : nom », colonne Paramètres, renseigner
   une adresse d'image **JPEG HTTP** ou **BMP RGB24 HTTP**, puis sauvegarder.
4. Sur l'écran, appuyer sur le bouton caméra.

### MotionEye JPEG (sans proxy)

```text
http://192.168.1.24:8765/picture/2/current
```

Il s'agit d'un exemple du LAN de test. L'image doit être accessible
sans cookie de session inaccessible à l'ESP. Si MotionEye nécessite une
authentification navigateur, la prise d'image doit d'abord être autorisée.

Le firmware identifie le JPEG par sa signature `FF D8`, le BMP par `BM`.
Les JPEG sont décodés localement avec JPEGDEC et réduits automatiquement
(1/2, 1/4 ou 1/8) pour tenir sur l'écran. Le BMP RGB24 reste pris en charge
pour tester le proxy initial. Les JPEG dépassant 900 KiB sont refusés.

## Recette terrain

- Page d'accueil : seul « icône caméra + nom » est affiché, sans requêtes réseau.
- Appui : chargement plein écran, image JPEG MotionEye affichée.
- Retour/reprise/veille : aucun téléchargement persistant.
- Tester URL absente et lien cassé : afficher une erreur sans figer Mira.
- Sur WT32, tester les 20 ouvertures/fermetures et surveiller la PSRAM.
- Sur Waveshare et Nextion, tester affichage, navigation et retour.
- Vérifier que les autres widgets Jeedom fonctionnent normalement.

Le succès de compilation/CI ne vaut pas validation sur les écrans réels.

## Correctif couleurs 0.7.23

Le firmware 0.7.18 affichait les JPEG avec des couleurs artificielles sur le WT32 :
le JPEGDEC utilisait `RGB565_LITTLE_ENDIAN` alors que LVGL WT32 était configuré
avec `LV_COLOR_16_SWAP=1`. Les trames JPEG **et BMP** utilisent désormais l'ordre
RGB565 attendu par LVGL selon chaque cible, sans modifier la palette ni les
styles des autres widgets. Le bouton caméra reste identique ; seules les images
en plein écran sont concernées. Validation sur le matériel physique recommandée.

## Image plein écran & rafraîchissement 0.7.23

- **Mode paysage conservé** même si le panneau est en portrait : aucun pivot de 90°.
- Zoom proportionnel avec LVGL pour occuper la largeur utile en affichage complet,
  sans déformation ni recadrage.
- Intervalle entre débuts des requêtes d'image réduit de 4 000 ms à 250 ms.
  Les téléchargements et le décodage restent séquentiels dans leur propre tâche
  FreeRTOS ; la cadence effective dépend de la caméra et du Wi-Fi. Après
  un échec HTTP/JPEG, prochaine tentative après 2 secondes.
- Pas de connexion ni de rafraîchissement réseau lorsque la tuile Caméra n'est
  pas ouverte ; sortie et veille stoppent le travail caméra.

## Retour de la page Énergie — 0.7.23

Page **Énergie** rétablie (identifiant **4**), après les pages originales :
Accueil 0, Lumières 1, Températures 2, Paramètres 3. Le menu Pages liste
Accueil, Lumières, Températures, Énergie et Paramètres. Les favoris déjà
synchronisés dans la mémoire NVS sont conservés. Le serveur accepte de nouveau
Énergie (4) depuis le plugin Jeedom et l'UI charge les widgets envoyés sur
`emplacement: "energie"` ; les commandes et la vue Caméra de 0.7.20 restent
inchangées. Le plugin beta.6 rend cette page sélectionnable dans la
programmation classique et le designer.

## Page Énergie sans texte — 0.7.23

La page Énergie reste accessible avec les favoris et dans le menu Pages,
mais **n'affiche aucun texte automatique** sur une page vide : uniquement
les widgets Jeedom configurés. Aucun changement à la caméra ni au plugin
Jeedom beta.6.

## Mira Énergie & Lecteur multimédia — 0.7.23 (prototype)

- `energy` : cinq champs Info par index Jeedom : `<nom>__solar`, `__home`, `__grid`, `__battery` (W), `__soc` (%). Quatre indicateurs et barre de niveau SoC.
- `media` : six champs Info : `__title`, `__artist`, `__album`, `__state`, `__progress` (%), `__volume` (%). Trois actions Jeedom Button : `__previous`, `__playpause`, `__next` et un Range `__volume_cmd` (0-100).

Les états Jeedom ou MQTT sont purement informatifs. Les commandes ne sont exécutées que lors d'un geste tactile explicite. Les champs Jeedom peuvent être associés via `calcul` aux capteurs et lecteurs existants. Les informations acceptent aussi le topic MQTT `mira/<ecran>/States/<nom>__<champ>` ; les actions sont publiées vers `mira/<ecran>/Actions/<nom>__<action>`.

Le prototype multimédia utilise encore une illustration LVGL de pochette, pas une image distante, et les courbes historiques ne sont pas encore implémentées. Ce lecteur contrôle un appareil Jeedom/MQTT : il ne lit aucun son sur l'écran.

## Mira 0.7.24 — page Multimédia, masquage des pages et unités Énergie

- Page 5 **Multimédia** accessible dans le menu Pages, avec l'emplacement
  `multimedia` utilisable par le designer et le programmateur Jeedom.
- Menu Pages filtré par un masque stocké en NVS `miraui/pageMask`. La
  nouvelle API authentifiée `POST /navpages` accepte `mask` (entier 1..63
  avec bit Accueil) et `GET /navpages` renvoie le masque. Aucun widget n'est
  supprimé par le masquage. Trois favoris indépendants conservés ;
  un favori vers une page masquée disparaît visuellement.
- Widget Énergie : **aucun W ni % ajouté automatiquement**. Jeedom peut
  sélectionner l'unité adaptée (W, kW, kWh, A, etc.) ou aucune unité.
  L'affichage restitue la valeur reçue textuellement. La barre de stockage
  est visible uniquement si la valeur précise explicitement le caractère %.
- Widget Multimédia : état Spotify `true` pris en charge ; un texte
  de piste ne peut plus être affiché comme « progression % ».
- Plugin requis pour la navigation : **Jeedom 1.2.28-beta.9**.
  Le choix de source Spotify et son actualisation événementielle s'effectuent
  exclusivement sur Jeedom.
- Régression obligatoire : vérifier caméra JPEG, navigation favoris, absence
  d'action involontaire pendant une synchronisation, et fonctionnement WT32,
  Waveshare 4.3C et Nextion 2.8.
