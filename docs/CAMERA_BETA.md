# Mira Camera beta 0.7.19 — bouton simple + JPEG direct

**Branche expérimentale uniquement. Ne pas fusionner avec main/stable avant essais physiques.**

## Interface — sans miniature

Le widget Caméra reprend exactement l'apparence d'un bouton Mira classique :
petit pictogramme caméra à gauche, **nom à droite**. Pas de vignette JPEG,
d'aperçu, de rafraîchissement ni de requête HTTP sur la page principale.

- Appui : affiche une vue plein écran et lance le téléchargement des images.
- Retour, veille ou reprogrammation : arrêt du téléchargement et libération
  des images en PSRAM.
- Une image toutes les 4 secondes, pas de RTSP/MJPEG/H.264 continu.
- Tous les écrans (WT32, Waveshare, Nextion) partagent le même code UI.

## Configuration Jeedom

**Plugin beta 1.2.28-beta.4 minimum** et **firmware 0.7.19 beta**.

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

## Correctif couleurs 0.7.19

Le firmware 0.7.18 affichait les JPEG avec des couleurs artificielles sur le WT32 :
le JPEGDEC utilisait `RGB565_LITTLE_ENDIAN` alors que LVGL WT32 était configuré
avec `LV_COLOR_16_SWAP=1`. Les trames JPEG **et BMP** utilisent désormais l'ordre
RGB565 attendu par LVGL selon chaque cible, sans modifier la palette ni les
styles des autres widgets. Le bouton caméra reste identique ; seules les images
en plein écran sont concernées. Validation sur le matériel physique recommandée.
