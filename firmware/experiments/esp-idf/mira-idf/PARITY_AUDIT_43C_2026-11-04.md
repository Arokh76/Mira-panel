# Mira Panel Waveshare 4.3C — audit comparatif `firmware-beta` → ESP-IDF

**Date : 4 novembre 2026.** Référence Arduino : `firmware-beta` (`9790a1f110fe`, Mira 0.7.37). Branche portée : `experiment/mira-idf-mira-ui-43c` (ESP-IDF 5.5.6 / LVGL 9.6 / Waveshare 4.3C 800×480 RGB 12 MHz / GT911).

**Cette feuille atteste les différences vérifiées dans le code, pas une certification matérielle.** Vérifier le SHA exact de la compilation GitHub Actions avant tout flash. Les branches Arduino, WT32 stable et 7 pouces ne doivent pas être modifiées par le portage.

## 1. Inventaire des interfaces et fonctionnalités

| Sous-système | Référence bêta | Implémentation 4.3C IDF | Qualification / écart |
|---|---|---|---|
| HTTP historique | `MiraPanelServer.cpp`, 48 méthodes/chemins GET/POST | `mira_recovery.c`, `mira_jeedom.c`, `mira_beta_setup.c`, `mira_beta_web.c` | 48/48 méthodes/routes **déclarées**. Test dérivé du code source; réponse et effets encore à tester réellement |
| Pages web | 9 modèles `HTML/*.h` | `mira_beta_web_pages.h` | 9/9 comparées aux originaux dans la CI; rendu/champs/actions dynamiques à confirmer sur navigateur |
| Première installation | Clé secrète locale → formulaire Wi-Fi direct | `mira_beta_setup.c` | La redirection 303 introduite à tort a été retirée; AP-only et clé à revérifier physiquement |
| API Jeedom | `/getindex`, `/programui`, `/updatenr`, `/get`, `/update`, binding `value=json` | `mira_jeedom.c` | Index et métadonnées composite alignés, mapping par hash et nom; vrai cycle d'association encore à valider |
| **Confirmation/reprise Jeedom** | `SendQueud` / `ReponseAsync`, jusqu'à 3 essais et 3 s | `capture_jeedom_ack`, worker HTTP, réponse exactement égale à l'ID distant, trois essais | **Porté dans cette passe** ; la réponse `echo init('id')` a été vérifiée dans `jeedom-beta/core/class/mira.class.php`. Les effets/doublons éventuels sur équipement réel restent à tester |
| Transports autres | `GenericCmd` avec `#value#` | Worker HTTP générique, MQTT optionnel | Source vérifiée. Interdire le double envoi et les actions à partir des états passifs |
| MQTT affichage | `MiraMqtt.cpp`, 11 topics retenus | `mira_mqtt.c` | Les 11 clés comparées directement à la bêta; réception/retained/reconnexion à éprouver |
| Types LVGL | `MiraPanelUI.cpp`, 14 types | `mira_full.c`, 14 types | Tous reconnus; même présence de type **ne prouve pas** un rendu/tactile équivalent |
| Caméra | BMP, JPEG, MJPEG, HTTP/HTTPS | `mira_camera.c`, `mira_album_art.c` | Chemins codés, certificat HTTPS vérifié. Caméra physique, FPS et stabilité PSRAM à vérifier |
| Wi-Fi | `reseau.json`, IP DHCP/manual/fixed_auto, AP de secours | `mira_recovery.c`, `mira_beta_networks.c`, `mira_beta_setup.c` | Reprise des anciens paramètres et routes codée; DHCP/fixe et secrets hérités nécessitent essais réels |
| Thèmes, veille et couleurs | Paramètres DISPLAY_*, thème Mira/Sombre/Glass, roue RGB, favoris | `mira_full.c` | Fonctions présentes et vérifications de source; fidélité graphique et tactile, FPS/tearing non homologués |

## 2. GPIO : adaptation matérielle des quatre routes absentes

L'Arduino WT32 offre six E/S EXT `10,11,12,13,14,21`; ces numéros **ne sont pas** valables pour la carte Waveshare. Le **4.3C possède seulement deux entrées isolées DI0/DI1 (EXIO0/EXIO5) et deux sorties isolées DO0/DO1 (EXIO6/EXIO7)** via le CH32 sur I²C `0x24`.

- `mira_gpio43.c/.h` stocke et restaure explicitement le mode de chaque sortie, l'inversion, la politique de démarrage OFF/restore/ON, les liaisons de commandes et les associations de boutons multiactions, dans des namespaces NVS distincts.
- Les deux sorties restent **désactivées et au niveau bas par défaut**. Une sortie doit d'abord être activée depuis la page d'administration. La direction des deux DI est entrée, et la protection de l'écran/GT911 est maintenue.
- Les routes `POST /gpio/config`, `POST /gpio/link`, `POST /gpio/raw`, `POST /gpio/multi/config` sont enregistrées et exigent Basic Auth + en-tête `X-Mira-GPIO: 1`. `GET /gpio/state` renvoie états, paramètres et associations. `POST /gpio/set` utilise l'état logique enregistré.
- Les gestes physiques DI utilisent anti-rebond 35 ms, appui long 900 ms et fenêtre simple/double/triple 370 ms. Un contact déjà actif au démarrage ne produit pas d'action.
- Une commande locale liée à DO0/DO1 n'est **pas** publiée en plus vers Jeedom/MQTT. La tuile multiaction est visible uniquement si explicitement demandée dans la configuration.

**Réserve physique** : les DI sont **isolées**, pas des entrées ESP32 avec pull-up configurable. Il faut vérifier la tension, la polarité et le circuit d'entrée/sortie réel avant raccordement. L'ancien menu permettant les modes `INPUT_PULLUP` et `INPUT_PULLDOWN` ne peut pas être copié tel quel.

## 3. Jeedom : confirmation des commandes et absence de double envoi

Référence `MiraPanelServer::SendQueud` : événements de commandes sérialisés, `ReponseAsync` confirme l'ID, renvoi possible jusqu'à trois fois (intervalle 3 s).

Le nouveau worker `mira_jeedom.c` :
- conserve la file FreeRTOS et les correspondances de commandes vérifiées par `value=json`, sans deviner des IDs distants ;
- transmet une commande sans bloquer LVGL ;
- lit la réponse HTTP en tampon **borné** et n'acquitte que si HTTP 2xx **et** corps égal à l'ID de la commande exécutée ;
- tente au plus trois envois, espacés de trois secondes ;
- ne réémet jamais une commande depuis une simple mise à jour passive MQTT/HTTP.

**Attention aux essais** : comme dans la bêta, une réponse perdue après exécution peut déclencher un renvoi. Ne pas utiliser d'actionneur critique pendant les premiers essais. Vérifier l'absence de doublons avec une commande de test non dangereuse.

## 4. OTA : installation distante et manuelle séparées des firmwares Arduino

Le manifeste public `main/firmware/targets.json` connu lors de l'audit **ne contient actuellement que des cibles Arduino**, et **pas** la cible `waveshare-esp32s3-touch-lcd-4.3c-idf`. **Aucune mise à jour distante IDF n'est donc installable à ce stade.** C'est un verrou volontaire, pas un échec de transport.

La nouvelle procédure native :
- `GET /api/checkupdate` : vérification HTTPS du manifeste et du profil IDF exact.
- `POST /updatemodule` : déclenchement explicite Basic Auth + en-tête `X-Mira-OTA: 1`, avec un seul worker à la fois. Un GET ne flashe jamais automatiquement.
- `GET /api/otastatus` : progression ou erreur.
- Seule une URL HTTPS du dépôt du projet se terminant par `/waveshare-esp32s3-touch-lcd-4.3c-idf/ota.bin` est autorisée.
- Taille `otaBytes` et empreinte SHA-256 `otaSha256` strictement vérifiées avant sélection de la partition.
- `esp_ota_end` valide l'image ; `esp_ota_get_partition_description` vérifie `project_name=mira_idf_scroll` et la version déclarée du binaire.
- Le **formulaire OTA manuel** est maintenant soumis au même contrôle de `project_name`, afin d'empêcher le flash d'une image Arduino ESP32-S3.

**Dépendance de publication** : il faudra publier un binaire IDF officiel conforme à la convention de nom, ajuster `PROJECT_VER` et le manifeste avant tout test OTA distant. Sans binaire publié, la route refuse l'installation.

## 5. Points ressortis de l'audit qui restent réellement à prouver

1. **SSE /events** : le port IDF envoie un instantané puis ferme la réponse, avec reconnexion `EventSource` toutes les 3 secondes. La bêta Arduino conserve une connexion SSE ; le comportement immédiat n'est **pas strictement identique**.
2. **Parité visuelle** : 14 widgets reconnus, mais les géométries, interactions et gestes (dont multimédia et colorwheel) ne sont pas comparés automatiquement pixel à pixel.
3. **Configuration cryptée** : importer/reprendre une installation existante de l'Arduino impose de tester les valeurs réellement chiffrées, IP fixe et profils multiples. L'import sur fichiers sources ne prouve pas la récupération d'une configuration réelle.
4. **Sécurité des secrets** : certains paramètres secrets restent en NVS non chiffrée dans le prototype; Basic Auth sur HTTP local n'est pas un canal chiffré. Ne pas confondre avec une validation sécurité de production.
5. **Matériel** : LCD 800×480, GT911, EXIO, AP, Wi-Fi, veille/réveil, commandes Jeedom, photos, 10–15 FPS et absence de tearing doivent être contrôlés sur le 4.3C réel.
6. **OTA** : installation distante non testable avant publication d'un IDF officiel dans le manifeste. Une compilation ne prouve pas ce cycle de bout en bout.

## 6. Garde-fous CI et méthode de validation

`tests/check_beta_web_parity.py` compare directement :
- 9 modèles HTML (contenu d'origine conservé) ;
- 48 routes HTTP GET/POST de `MiraPanelServer.cpp` contre les handlers natifs de tous les `.c` ;
- 14 types de widgets de `MiraPanelUI.cpp` ;
- 11 états MQTT de `MiraMqtt.cpp` ;
- contrat de `/getindex` et réponse d'acquittement des commandes Jeedom ;
- chemins GPIO, bornage physique, initialisation sûre, stockage et dispatch ;
- conditions OTA SHA-256, cible et projet, y compris les mises à jour manuelles.

Ces tests gardent uniquement un **niveau de parité source**. Le dernier HEAD doit compiler sur CI ESP-IDF 5.5.6 / LVGL 9.6, puis être vérifié sur le matériel avant qu'un état soit marqué « validé ».

**Décision de projet :** travailler uniquement sur `experiment/mira-idf-mira-ui-43c`. Ne toucher ni à `main/stable`, ni à `firmware-beta`, ni à la bêta Dimitri, ni aux branches 7 pouces.
