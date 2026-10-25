# Mira Camera beta 1 — clic pour ouvrir, zéro streaming en fond

**Branches expérimentales uniquement. Ne pas fusionner vers `main` ou `stable` avant essais.**

Le widget `camera` ne demande **aucune image tant qu'il n'est pas ouvert**.
La vignette est un bouton fixe avec icône caméra (miniature réelle prévue après essai).
Au clic, le firmware crée une vue LVGL plein écran. Une tâche FreeRTOS distincte
télécharge **un BMP 24 bits non compressé toutes les 4 secondes** ;
la conversion en RGB565 s'effectue dans la tâche réseau, puis seule
la boucle LVGL affiche la nouvelle image. Retour/veille/reprogrammation
ferment le flux et libèrent les images en PSRAM.

Le BMP doit être au plus aux dimensions de l'écran (la requête ajoute
automatiquement `w=` et `h=`). Le firmware **ne décode pas le JPEG**
directement dans cette beta. Pas de RTSP, MJPEG ni vidéo fluide.

## Tester sans caméra

Sur une machine Debian / Raspberry Pi du réseau :

```bash
sudo apt install python3-pil
python3 tools/mira_camera_proxy.py
```

Dans Jeedom beta, ajouter un widget **Caméra**, avec le nom `Portail`
et l'URL `http://IP_DU_PI:8098/snapshot.bmp`. Programmer l'écran
avec le firmware caméra beta. Un appui affiche l'image de démonstration ;
Retour ferme immédiatement la demande d'images.

## Tester avec une vraie caméra

```bash
CAMERA_SNAPSHOT_URL='http://IP_CAMERA/snapshot.jpg' python3 tools/mira_camera_proxy.py
```

Pour une caméra nécessitant HTTP Basic Auth, ajouter les variables
`CAMERA_USER` et `CAMERA_PASSWORD`. Le proxy prend un snapshot JPEG
à chaque demande, le redimensionne dans le cadre demandé et renvoie un
BMP RGB24. Tester d'abord l'URL dans un navigateur.

**Sécurité :** ce prototype sert les images **sans authentification**
sur le réseau local. Ne pas rediriger le port 8098 sur Internet.
Filtrer les accès au port et éviter toute caméra privée pendant les
premiers essais. L'URL source, éventuellement sensible, reste côté Pi ;
seule l'URL HTTP du proxy est envoyée à l'ESP32.

## Vérifications terrain

- Cliquer sur la vignette, obtenir le BMP, revenir, répéter 20 fois.
- Vérifier que la caméra n'est pas consultée hors de sa vue et en veille.
- Tester avec Wi-Fi indisponible : l'écran doit afficher une erreur sans geler.
- Sur Waveshare, contrôler la navigation, les gestes, le retour et la mémoire.
- Sur Nextion, valider d'abord la stabilité du portage matériel indépendant.
- Comparer la consommation PSRAM avant/pendant/après la vue.

La version expérimentale vise les trois profils de compilation, mais le
Nextion reste non validé sur écran réel.
