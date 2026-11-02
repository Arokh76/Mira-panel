# Mira Panel 4.3C — parité obligatoire avec firmware-beta 0.7.37

> État réel au 2 novembre 2026 : ce document décrit une cible de parité, pas une
> homologation. La branche a depuis porté les vraies pages Web beta (Accueil,
> Paramètres, MQTT, Wi-Fi, ConfigAP, Système, Gestion des données, réseaux
> enregistrés), l'import MQTT, les commandes d'affichage et les routes Jeedom
> /get, /events, /sync/toggle. Les six routes historiques /addlistlvgl,
> /removelistlvgl, /savelistlvgl, /screenadd, /screenformat, /screensave utilisent
> désormais un document JSON temporaire (écriture atomique au "save").
> Leurs tests de contrat statiques sont intégrés à la CI.
>
> **Toujours manquant / à valider** : mise à jour automatique IDF spécifique
> (/api/checkupdate, /updatemodule, GET /update) : NE JAMAIS installer un OTA
> Arduino de la même cible Waveshare sur un port IDF; MJPEG multipart et HTTPS
> pour la caméra; fidélité de chaque widget, styles et interactions, migration
> des secrets sur un appareil réel, sécurité des APIs et essais physiques
> (premier démarrage, Wi-Fi, tactile GT911, actions Jeedom, stabilité PSRAM,
> veille, FPS, reboot). Les endpoints GPIO optionnels de l'Arduino ne sont
> pas actifs sur la 4.3C et ne sont pas un obstacle.
>
> **Complément portage du 2 novembre (suite)** : HTTPS caméra ajouté avec
> vérification CA Espressif, pour les snapshots JPEG et les flux MJPEG
> multipart. Une session MJPEG persistante sur une seule connexion TCP/TLS
> est désormais codée, avec détection des images JPEG SOI/EOI entre les blocs
> réseau, tampon PSRAM borné, 4 FPS maximum et arrêt sur changement de caméra.
> Sa compilation au SHA courant et ses tests physiques restent à qualifier.
> Les diagnostics IDF ont été déplacés sous `/recovery/mira` ; `/mira`
> redirige vers Mira, et `POST /mira/state` exige maintenant une
> authentification avant l'injection d'un état passif.
> Les routes historiques `/api/checkupdate`, `GET /update`,
> `/updatemodule` ont un équivalent sécurisé : la vérification HTTPS
> accepte seulement le profil `waveshare-esp32s3-touch-lcd-4.3c-idf`
> dans le manifeste GitHub. **Aucun OTA Arduino n'est sélectionné
> automatiquement**. L'activation réelle de mise à jour automatique
> reste conditionnée à la publication d'un binaire officiel IDF.
> La version de référence IDF demeure expérimentale et n'est pas distribuée.
>
> Le dernier build doit être vérifié sur **son SHA exact** : un ancien build
> réussi ne suffit pas à valider les nouveaux commits. N'attribuer le statut
> "firmware prêt à tester" qu'après validation du build courant et cohérence
> de tous les parcours critiques.


**Source de vérité :** `firmware-beta/firmware/source/consolidated/MIRA_PANEL/` (sources `MiraPanelUI.cpp`, `MiraPanelServer.cpp`, `MiraDisplayProfile.h`, `SPIFFS_Process.h`, `HTML/*.h`). Ce portage ESP-IDF doit conserver les parcours et contrats, pas inventer un nouveau Mira.

## Étapes déjà implémentées dans experiment/mira-idf-mira-ui-43c

- RGB 800 × 480 / PCLK 12 MHz, GT911, LVGL 9.6, polices françaises, horloge 96px.
- Préférence HTTP/Jeedom par défaut ; MQTT strictement optionnel, événements Jeedom passifs.
- Import et rendu des widgets connus depuis `miralvgl.json`, synchronisation HTTP et commandes locales en files FreeRTOS.
- Configuration initiale beta `HTML_Key.h` et `HTML_ManageurWiFiSansR.h` : étape clé et Wi-Fi, scan de 10 réseaux et saisie manuelle, IP Auto/IPv4 fixe contrôlée, portail DNS AP 192.168.4.1.
- Widgets bouton, toggle, range, roundrange, thermostat 0,5 °C, list, listbutton, multiaction, énergie, media, couleur HSV -> RGB et caméra BMP/JPEG.
- Routes de navigation `/navpages` et `/navfavorites` GET/POST, stockage historique `miraui`, mise à jour sans redémarrage. `/mira-health` et `/reboot` ont leur équivalent natif.
- `/displayinfo` utilise l'identifiant `waveshare-esp32s3-touch-lcd-4.3c`, une grille logique 1000×1000 et les dimensions **430px de contenu + 50px de navigation** de la bêta.
- Firmware factory USB / OTA avec partition SPIFFS initialisée seulement si la partition entière est vierge.

**Important :** ces implémentations restent à qualifier par compilation du dernier commit **et** essais sur écran 4,3C. Un build réussi n'est pas un test de parité.

## Écarts fonctionnels connus restant à traiter

1. **Interface Web après configuration** : remplacer les pages de banc d'essai par les pages et les routes de `HTML_Index.h`, `HTML_Parametres.h`, `HTML_ManageurWiFi.h`, `HTML_ConfigAP.h`, `HTML_MQTT.h`, `HTML_Sys.h`, `HTML_Donnees.h`, `HTML_Message.h`; conserver l'authentification historique.
2. **Routes secondaires de l'ancien serveur, encore absentes** (inventaire par comparaison `server.on` vs `httpd_uri_t` ; vérification manuelle nécessaire) : `/addlistlvgl`, `/addparam`, `/api/checkupdate`, `/get`, `/getallparams`, `/removelistlvgl`, `/savelistlvgl`, `/screenadd`, `/screenformat`, `/screensave`, `/sync/toggle`, `GET /update`, `/updatemodule`, `/updateparam`, `POST /display-save`, `POST /doUpdateFile`. Les routes GPIO ne concernent pas la cible 4.3C car désactivées dans la bêta de ce matériel.
3. **Widgets avancés et visuels** : revoir chaque `create*` et chaque traitement d'état de `MiraPanelUI.cpp` avec le rendu LVGL natif. Le contrôle RGB actuel n'est pas l'ancienne roue graphique; des styles avancés et la parité complète de l'UI restent à vérifier.
4. **Caméra** : HTTP JPEG/BMP existe ; MJPEG multipart et HTTPS manquent.
5. **Sécurité/maintenance** : Wi-Fi configuré, clé écran, export et reset, mise à jour OTA manuelle et version doivent respecter les mêmes actions que la bêta ; routes de banc d'essai séparées de l'interface normale.
6. **Validation physique** : premier démarrage avec erase flash, AP + DNS + pages clé/Wi-Fi, IP DHCP et fixe, GT911, gestes, rendu, réveil et veille, commandes Jeedom HTTP, reprogrammation, camera, et absence de fuite mémoire ou reboot.

## Règles non négociables

- Ne pas modifier `main/stable`, `firmware-beta`, la bêta Dimitri ni les releases 7 pouces.
- Ne pas remplacer le serveur HTTP par MQTT ; MQTT n'est qu'une option avancée.
- Ne pas annoncer le portage « terminé » avant la parité fonctionnelle et des tests réels concluants.
- Chaque correction d'écart passe par la compilation native ESP-IDF 5.5.6/LVGL 9.6, puis par une validation sur écran physique, sans binaire périmé.
