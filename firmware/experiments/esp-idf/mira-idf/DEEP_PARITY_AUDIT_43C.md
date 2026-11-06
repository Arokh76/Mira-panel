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

## Comparaison approfondie des comportements Arduino ↔ IDF (4 novembre 2026)

La première vérification avait confirmé la présence des fonctions, mais pas la **forme exacte des données**. La lecture croisée des implémentations Arduino, IDF et Jeedom a révélé puis permis de corriger les divergences suivantes.

| Contrat observable | Référence Arduino | Écart retrouvé dans IDF | Correction |
| --- | --- | --- | --- |
| `GET /get?toggle=...` | `IndexSetToggle()` garde `On`/`Off` | Cache passif brut pouvant répondre `1`/`0`, valeur initiale absente | Renvoyer `On`/`Off`, défaut `Off` |
| Lecture d'un paramètre absent | `GetParamText()` retourne `null`, `GetParamBool()` est `false` | `param_get` forçait `Jeedom=1`, synchronisation lancée si le mode manquait | Ne jamais inventer l'activation Jeedom |
| Nombre de widgets | `MiraPanelUI::MAX_BINDINGS = 40` | IDF et programmeur acceptaient 64 | Limite harmonisée à 40 |
| Valeur de liste dans `/getindex` | `IndexAddList` met le **même nombre entier** dans `value` pour toutes les options de la liste | Les options suivantes avaient une chaîne JSON | Nombre pour toutes les options |
| Action MQTT couleur | `onColor()` envoie `#RRGGBB` | IDF envoyait `r,g,b` | MQTT revient en hexadécimal ; Jeedom HTTP continue de recevoir les composantes RGB |
| Action thermostat | `onThermostatRange()` émet `22`, `22.5`, etc. | IDF émettait `22.0` | Même représentation numérique qu'Arduino |
| État MQTT brut / JSON | `mqttPayloadValue()` retire les espaces de l'enveloppe et décode `{"value": ...}` | IDF refusait un message si l'enveloppe dépassait 159 octets même lorsque la valeur extraite tenait | Limite imposée à la valeur **décodée**, enveloppe bornée à 1024 octets |
| Correspondance des topics MQTT | `normalizeMqttTopicName()` retire les espaces en bordure avant les remplacements | IDF transformait ces espaces en underscores | Trim avant remplacement |
| Nom d'écran MQTT | `cleanScreenName()` traite slash initial/final et caractères MQTT spéciaux | IDF rejetait certains noms compatibles avec la bêta | Normalisation équivalente avant construction des topics |
| Réponse au réglage `Display/.../set` | `publishDisplayStates(true)` après commande | IDF attendait sa période de publication | Forçage de la prochaine publication depuis la tâche LVGL |
| Horaires nuit | Défauts `23:00`–`07:00`; `normalizeClock` comprend `2300`, `23:00`, `23%3A00` | Horaires initialement non définis ; certains formats rejetés | Defaults restaurés et trois formats interprétés |
| Liaison GPIO en lecture seule | Une liaison de lecture n'est pas un actionneur | Interception possible de clics de boutons | Seules les liaisons de sortie exécutables interceptent le clic |
| OTA cible IDF | La vérification et l'installation doivent annoncer la même version installable | Vérification plus permissive que l'installateur | Validateur de manifeste partagé |

**Tests ajoutés.** `tests/check_beta_web_parity.py` vérifie maintenant les types JSON, les bornes historiques, l'absence d'activation implicite de Jeedom, les formats des commandes (couleur, thermostat), les règles MQTT et les horaires, en se fondant sur les fonctions Arduino d'origine. Ces contrôles ne simulent pas un vrai broker ni un vrai Jeedom.

**Limite externe de l'OTA automatique.** `main/firmware/targets.json` n'annonce actuellement *aucune* cible `waveshare-esp32s3-touch-lcd-4.3c-idf` : sans publication officielle compatible et empreinte vérifiable, le mécanisme doit répondre « aucune mise à jour » ; le test d'une installation automatique réelle reste impossible pour l'instant. Le mécanisme natif ne doit pas être confondu avec les binaires Arduino du même matériel.

**État de validation.** Contrôles statiques et CI obligatoires avant de considérer ces changements compilables ; ensuite seulement validation physique. Le rapport ne prétend ni à l'équivalence pixel à pixel, ni à une exécution de toutes les interactions Web/LVGL, ni à une migration intégralement testée avec secrets et paramétrages réels.

### Alignements complémentaires après lecture des formulaires MQTT

- `MiraMqtt::cleanScreenName()` accepte un nom d'écran vide (repli `panel`), des sous-chemins séparés par `/`, et nettoie `+`/`#`. Deux formulaires IDF refusaient encore ces entrées malgré leur acceptation par le moteur MQTT ; alignés sur Arduino.
- `MiraMqtt::refreshConfig()` traite les chaînes littérales `"null"` pour le serveur, l'utilisateur et le mot de passe comme des valeurs absentes ; harmonisé dans le chargeur NVS IDF.
- `MiraMqtt.h` définit `KEEPALIVE_S=20` secondes, contre 30 auparavant dans IDF ; harmonisé à 20 s.

## Divergences encore ouvertes après la lecture croisée

- **Roundrange demi-unité.** La comparaison de `MiraPanelUI::createRoundRange/onRoundRange/handleMqttState` a révélé que le cadran Arduino travaille dans une plage doublée `min×2..max×2` et transmet des valeurs comme `20.5`. L'IDF utilisait auparavant une plage entière : corrigé sur création, état passif, étiquette et action sortante, avec test de source dédié.
- **Curseurs et roue de couleur pendant un glissement.** Arduino `MiraPanelUI::onRange()`, `onRoundRange()` et `onColor()` envoie une action lors des changements de valeur puis une confirmation au relâchement. Dans `mira_full.c`, les contrôles correspondants déclenchent `event_local` seulement sur `LV_EVENT_RELEASED`. La valeur finale est envoyée, mais pas les commandes intermédiaires. Cette différence est **confirmée dans le code**. Reproduire les envois intermédiaires sans coalescence saturerait facilement la file HTTP Jeedom et pourrait perdre la dernière commande ; une vraie parité de transport suppose de concevoir et tester un mécanisme de dernière valeur prioritaire.
- **Capacités des interfaces physiques.** Les E/S isolées Waveshare ne sont pas une traduction électrique des GPIO du WT32 ; le comportement précis de chaque polarité et mode de démarrage dépend d'essais réels.
- **Enveloppes MQTT inhabituelles.** Le parseur IDF accepte désormais jusqu'à 1024 octets d'enveloppe et 159/768 octets décodés ; les limites de paquets de la pile Arduino et d'ESP-MQTT ne sont pas strictement identiques.
- **Protocole d'installation.** La mise à jour automatique ne pourra être validée qu'après publication d'une version native spécifique ; aucun firmware Arduino ne doit être traité comme un OTA IDF.

**Conclusion de l'audit source :** les traitements principaux et leurs formats échangés ont été comparés et les différences documentées ci-dessus restent explicites. Ce rapport ne constitue pas un certificat de parité totale : les scénarios réseau, les effets LVGL et les chemins de réinitialisation restent à éprouver sur le Waveshare physique.

## Limites à ne pas dissimuler

1. La présence d'un handler ne démontre pas la compatibilité de ses effets ; les tests automatiques sont majoritairement structurels.
2. Les GPIO WT32 sont différents physiquement des E/S isolées Waveshare : parité des *capacités compatibles*, pas des numéros de broche.
3. L'OTA automatique exige une publication officielle pour la cible `waveshare-esp32s3-touch-lcd-4.3c-idf` avec empreinte SHA-256 ; sans manifeste éligible, il doit refuser l'installation.
4. La validation des flux caméra, la synchronisation Jeedom réelle, les gestes tactiles, la migration de secrets historiques et les performances d'affichage exigent le matériel et des services de test ; aucune de ces validations n'est revendiquée ici.

## Politique de branches

Ne modifier ni `main` ni `firmware-beta`, ni la bêta Dimitri, ni les branches IDF du 7 pouces. Une intégration vers une branche stable exige la parité fonctionnelle observée et les tests matériels concluants.
