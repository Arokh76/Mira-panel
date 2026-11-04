# Mira Panel — audit de parité firmware-beta ↔ ESP-IDF Waveshare 4.3C

Référence historique : `firmware-beta` (`9790a1f1`), y compris `MiraPanelServer.cpp`, `MiraPanelUI.cpp`, `MiraMqtt.cpp`, et le plugin `jeedom-beta` (`5f1a0846`).
Cible : `experiment/mira-idf-mira-ui-43c`, ESP-IDF 5.5.6 et LVGL 9.6.
Ce document sépare **code présent**, **tests statiques/CI**, et **fonctionnement matériel vérifié**.

## Analyse du 4 novembre 2026

| Domaine | Présent dans IDF | Contrat vérifié | Essais réels restant nécessaires |
| --- | --- | --- | --- |
| Serveur Web | 48/48 routes GET et POST de la bêta, avec chemins adaptés si nécessaire | Diff source → déclarations IDF, modèles HTML bêta intacts | Soumission de chaque formulaire, import/export, redémarrage |
| GPIO Waveshare | DI0, DI1, DO0, DO1, `/gpio/config`, `/gpio/link`, `/gpio/raw`, `/gpio/multi/config`, `/gpio/set` et `/gpio/state` | Vérification des chemins, permissions, modes entrées/sorties et rejet du déclenchement pour une liaison lecture seule | Polarité d'isolement, inversion, boot, contacts, déclenchement de gestes, sorties sous charge de test |
| Jeedom HTTP | `/getindex`, `/programui`, association IDs, file FreeRTOS et envoi HTTP | `jeedom-beta::event()` répond avec l'ID ; IDF attend exactement ce retour, trois tentatives espacées de trois secondes, association liée à la somme du JSON | Association plugin sur Jeedom réel, accusés sous charge, reprogrammation, pas de double commande |
| MQTT | States/Actions et onze `Display/<clé>/state` avec `retain` | Ensemble des 11 noms comparé au code `MiraMqtt.cpp` de la bêta | Reconnexions, valeurs, droits broker, compatibilité jMQTT |
| OTA IDF | `/api/checkupdate`, `/updatemodule` POST, `/api/otastatus`, OTA manuel | Vérification de version et installation partagent le même validateur ; HTTPS CA, cible exacte, taille, SHA-256, description d'image et partition inactive | Publication d'un binaire IDF officiel, installation et redémarrage réels, reprise après coupure |
| Configuration initiale | Page clé → page Wi-Fi sans redirection, AP de secours, Wi-Fi DHCP et adresse fixe | Route/HTML et sécurité vérifiés dans les tests | Installation vierge, détection captive, sauvegarde réseau et reconnexion |
| LVGL | Les 14 types de widgets bêta reconnus, navigation, favoris, veille, thème, rétroéclairage, caméra JPEG/BMP/MJPEG HTTPS | Liste des types comparée directement à `MiraPanelUI.cpp` | Parité pixel/gestes, états passifs, rechargement à chaud, 10–15 FPS, absence de tearing |
| Stockage/migration | Reprise partielle bêta NVS/SPIFFS, données chiffrées conditionnelles | Absence de formatage de SPIFFS non vierge, chemins typés | Test sur ancien appareil avec sauvegarde et secrets chiffrés |

## Écarts corrigés après la première version du portail

- `mira_gpio43_action_mapped` interceptait aussi les liens lecture seule `kind=2` et pouvait empêcher la commande standard du bouton : réservé maintenant aux liaisons d'action `kind=0/1`.
- La vérification de version OTA acceptait des manifestes que l'installateur refusait ensuite : `/api/checkupdate` appelle désormais le même `ota_manifest_entry()` que l'installateur.
- `tests/check_beta_web_parity.py` compare maintenant **toutes** les routes bêta, y compris GPIO, et protège les accords ID/Jeedom/OTA.

## Limites à ne pas dissimuler

1. La présence d'un handler ne démontre pas la compatibilité de ses effets ; les tests automatiques sont majoritairement structurels.
2. Les GPIO WT32 sont différents physiquement des E/S isolées Waveshare : parité des *capacités compatibles*, pas des numéros de broche.
3. L'OTA automatique exige une publication officielle pour la cible `waveshare-esp32s3-touch-lcd-4.3c-idf` avec empreinte SHA-256 ; sans manifeste éligible, il doit refuser l'installation.
4. La validation des flux caméra, la synchronisation Jeedom réelle, les gestes tactiles, la migration de secrets historiques et les performances d'affichage exigent le matériel et des services de test ; aucune de ces validations n'est revendiquée ici.

## Politique de branches

Ne modifier ni `main` ni `firmware-beta`, ni la bêta Dimitri, ni les branches IDF du 7 pouces. Une intégration vers une branche stable exige la parité fonctionnelle observée et les tests matériels concluants.
