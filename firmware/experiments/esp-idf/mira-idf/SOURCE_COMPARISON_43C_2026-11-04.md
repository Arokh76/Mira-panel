# Mira 4,3C — comparaison source Arduino beta / ESP-IDF (04-11-2026)

**Périmètre unique** : `firmware-beta@9790a1f110fe` (Arduino 0.7.37, Waveshare 4.3C) face à `experiment/mira-idf-mira-ui-43c` (ESP-IDF 5.5.6, LVGL 9.6). Les branches de production et l'écran 7 pouces sont exclus.

**Nature de la conclusion** : inventaire et comparaison du code, non certification matérielle. Les tests CI comparent certaines signatures et données ; ils ne simulent pas Jeedom, un broker MQTT, une dalle GT911 ni un navigateur. Vérifier le SHA du dernier build après tout correctif.

## Inventaire comparé

| Domaine | Référence Arduino | IDF 4,3C | Lecture croisée / épreuve |
|---|---|---|---|
| 14 types de widget | `MiraPanelUI::registerAndCreate` | `mira_full.c` | Les 14 types sont reconnus. Parcours de création, interactions locales et états passifs examinés ci-dessous. |
| `datetime` | Heure/date, météo composite interne, veille | Horloge 96 px, heure locale, météo `__mira_weather_*` | Code présent ; différences de rendu/position à observer sur la dalle |
| `info` | Variantes compact/value/hero/wide, icônes, `MAJ hh:mm` | Variantes, polices FR, icônes et horodatage | Contrat de données comparé, raster et cache à observer |
| `button` / `toggle` | ID d'action, bascule On/Off, liens locaux | `event_local`, Jeedom/MQTT, EXIO DO | Suppression de double envoi pour liaison locale ; polarité matérielle à mesurer |
| `range` | Envoi sur chaque `VALUE_CHANGED` et au `RELEASED` | `event_live_range` | Corrigé : plus de 180 ms inventés |
| `roundrange` | Arc 2× pour les demi-unités, état passif | Arc 2×, texte et action 0,5 | Corrigé, y compris commandes intermédiaires |
| `thermostat` | Consigne 0,5°, 130 ms sur glissement, `__consigne`, modes | Arc, file HTTP, MQTT composite | Corrigé : `__consigne`, décimale et 130 ms ; confirmation finale au relâchement |
| `list` / `listbutton` | Jusqu'à 12 options, libellé ou index passif | Menu, boutons, même format de choix | Valeurs de `getindex` typées nombre et étiquettes comparées |
| `multiaction` | Appui long 900 ms, agrégation 370 ms, 4 actions ; tuile masquée sur Waveshare | Même chronologie ; EXIO facultatif | Corrigé : chronologie et visibilité opt-in |
| `color` | Anneau hue 150 px, sorties `#RRGGBB`, changement et relâchement | Anneau RGB565, saturation et valeur séparées, sorties hex | Fonction RGB présente, mais interface LVGL9 pas pixel-identique au colorwheel LVGL8 |
| `energy` | 7 infos composites, batteries conditionnelles | 7 infos, jauges, hauteur variable | Champs et modes passifs comparés ; géométrie à observer |
| `media` | 9 infos, cover, previous/play/pause/next, volume, appareil | 9 infos, JPEG cover, actions et liste | Canaux et types d'actions comparés ; flux réels à essayer |
| `camera` | BMP/JPEG/MJPEG, HTTP/HTTPS, écran dédié | Tâche de réception, JPEG/BMP/MJPEG et TLS, écran dédié | Chemins présents ; flux, certificats et mémoire PSRAM non testés physiquement |
| Navigation/veille | 6 pages, 3 favoris, retour 60 s, interrupteur veille, horloge et luminosité | Pages, favoris, GT911, switches et timing | Corrigé : curseur de luminosité en direct, switch veille, héritage `miraui` |
| HTTP pages/formulaires | `MiraPanelServer.cpp`, `HTML/*.h` | `mira_beta_web.c`, setup, recovery, jeedom | 48/48 routes GET/POST déclarées ; 9 modèles HTML comparés aux originaux par CI |
| Jeedom | `getindex`, programmation, ACK, 3 essais à 3 s | Index dynamique, queue, ACK exact | Identités et formats source comparés ; association réelle non exécutée |
| MQTT | 11 états DISPLAY, topics States/Actions | `mira_mqtt.c`, cache et reconnection | 11 clés, décodage valeur JSON, keepalive 20 s, prefixes et commandes composites comparés |
| Wi-Fi & secrets | `reseau.json`, `params.json`, chiffrement historique, AP | Import en lecture seule, legacy AES, NVS, AP/DNS, DHCP/fixe | Chemins codés ; migration chiffrée sur sauvegarde réelle non prouvée |
| Mise à jour | Portail et mises à jour Arduino | OTA manuel et manifeste cible IDF uniquement | Politique de signature/tailles/hash comparée ; publication IDF nécessaire |

## Corrections retrouvées par comparaison des implémentations

- Range, Roundrange et thermostat : envoi live et au relâchement avec temporisation **uniquement 130 ms sur le thermostat** (source `onThermostatRange`).
- Anneau couleur : RGB publié lors du geste, HSV complet disponible et état passif synchronisé.
- Multiaction : 900 ms sur pression, 370 ms après relâchement, tuile invisible tant que l'extension DI ne l'active pas.
- Consigne thermostat MQTT : `<nom>__consigne`, texte décimal `String(value,1)` ; HTTP Jeedom reste indexé au widget.
- Priorité utilisateur 250 ms pendant manipulation et 350 ms après relâchement : évite qu'un ancien retour Jeedom déplace la commande sous le doigt.
- Luminosité : mise à jour en direct à `VALUE_CHANGED`, avec persistance NVS uniquement au relâchement.
- Veille : activation séparée de son délai, reprise `miraui/bright`, `miraui/sleep`, `miraui/clock`; réception de `DISPLAY_Delai` recalcule l'activation.

## Différences qui **ne peuvent pas être certifiées identiques** par le seul code

1. `/events` est un flux SSE *snapshot/reconnexion* sous IDF, alors qu'Arduino garde une connexion persistante. La réception reste possible, mais la latence du rafraîchissement navigateur n'est pas strictement identique.
2. LVGL 9 ne propose plus le même widget `lv_colorwheel` que LVGL 8 ; le cadran RGB565 et les curseurs HSV en reproduisent les commandes sans identité graphique exacte.
3. Le firmware IDF privilégie un seul transport actif pour éviter le double pilotage HTTP Jeedom + MQTT ; le code Arduino peut publier via les deux transports lorsqu'ils sont tous deux activés. Ce choix est intentionnel mais **pas une identité de comportement**.
4. Les E/S du Waveshare sont deux DI et deux DO isolées ; ce ne sont pas les broches EXT du WT32. Leurs modes électriques ne peuvent être copiés à l'identique.
5. Le manifeste officiel ne contient pas encore une cible OTA IDF publiée ; le chemin de mise à jour distante est volontairement verrouillé.
6. Les tests de source ne prouvent ni les FPS/tearing/tactile, ni les échanges réels Jeedom/MQTT, ni les sauvegardes chiffrées d'un appareil, ni les binaires du dernier commit.

## Critère de remise pour le débogage physique

1. GitHub Actions ESP-IDF 5.5.6/LVGL 9.6 **succès sur le HEAD exact** ;
2. contrôles `tests/check_beta_web_parity.py` réussis sur le même HEAD ;
3. fichiers `FACTORY_USB_0x0.bin` et `OTA.bin` présents dans l'artifact de ce build ;
4. ne pas déclarer une stricte parité *matérielle* avant le vrai test écran, et ne pas publier en stable.

**Ce document clôt l'inventaire comparatif des sous-systèmes principaux en code, mais ne remplace pas un test d'intégration.** Si l'objectif est une identité complète y compris SSE, routage double et rendu LVGL8, ces divergences doivent être traitées comme des tâches distinctes plutôt que masquées.
