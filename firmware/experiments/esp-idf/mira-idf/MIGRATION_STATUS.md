# Mira Panel — Source de vérité du portage natif ESP-IDF

Mise à jour : 1er novembre 2026. Une seule branche de développement du portage 4,3" :
`experiment/mira-idf-mira-ui-43c`.

## Règles de conservation validées
- `stable` et `main` : jamais modifiées par ce portage.
- `beta`, `firmware-beta`, `jeedom-beta`, `test/wt32-json-dimitri-b1` : conserver les derniers essais Dimitri, vérifier leur identité avant toute consolidation.
- `release/*` : toutes conservées, publications encore utilisées.
- 102 branches anciennes (`feat/`, `feature/`, `fix/` et 15 expériences Waveshare non-IDF) supprimées après sauvegarde de leurs commits dans `archive/mira-preclean-20261101`.
- Les 4 branches IDF 7 pouces sont conservées, pas fusionnées ici.
- Ne pas fusionner vers production avant preuve de parité fonctionnelle et tests physiques.

## Matériel validé / conservé
- Waveshare 4.3C : ESP32-S3, RGB565 **800x480**, `LCD_PCLK_HZ=12 MHz`, PSRAM, LVGL **9.6.0**, ESP-IDF **5.5.6**, écran GT911 I2C `0x5D` (fallback `0x14`), reset via IO expander EXIO1, GPIO4 IRQ.
- Waveshare 7" : profil préservé dans `experiment/mira-idf-waveshare7-pclk13-stable`. PCLK **13 MHz**, CH422G IO expander, reset GT911 spécifique au 7". **Ne jamais copier l'initialisation CH422G du 7" sur le 4.3C.**
- Vérifier sur matériel tearing, scrolling, press/release, rotation, sommeil et mémoire avant de qualifier « validé ».

## Déjà disponible dans le port ESP-IDF 4.3C
- Pilote RGB + GT911 + instrumentation tactile/VSYNC et LVGL 9.6.
- Rendu à partir du `/spiffs/miralvgl.json` original, navigation et tuiles principales; le SPIFFS existant n'est pas formaté.
- AP de secours et OTA HTTP pour revenir à Mira; partitionnement inchangé.
- Nouveau : `POST /mira/state` (JSON `{"name":"Nom","value":"Valeur"}`) vers une file FreeRTOS consommée **uniquement** par la tâche LVGL, sans déclencher d'action.
- Nouveau : MQTT natif optionnel sur `mira/<screen>/States/#` et `mira/<screen>/Actions/<nom>`, publication non bloquante en réponse **uniquement aux événements tactiles locaux**; configuration stockée séparément en NVS.
- Nouveau : **transport Jeedom HTTP sortant** sur tâche FreeRTOS dédiée (file bornée). Les actions utilisateur seules peuvent créer une requête, jamais les states HTTP/MQTT passifs.
- **Correspondances sûres** : récupération depuis l'API Jeedom existante `value=json` sur la commande `init_cmd`, vérification exacte des types, noms et IDs de tous les widgets avant publication; les associations sont liées à l'empreinte du `miralvgl.json`. Sans correspondance valide, aucune commande HTTP ne part.
- **Diagnostic** : `GET /mira/jeedom/status?key=...`, également accessible par le bouton dans le portail de récupération.
- Les widgets `energy` (7 valeurs) et `media` (9 valeurs) affichent désormais leurs champs `Nom__champ` transmis en information passive. Le widget multimédia possède maintenant des actions réelles `previous` / `play` / `pause` / `next`, un curseur `volume_cmd` et un sélecteur `device_cmd` alignés sur les identifiants historiques. La réception des informations `state`, `volume` et `devices` ne déclenche jamais de commande. **Caméra, météo riche et certains widgets avancés restent à porter.**
- Les événements locaux utilisent désormais **soit** le transport HTTP Jeedom **soit** le transport MQTT suivant le paramètre `Jeedom`, pour éviter l'envoi double. Les associations HTTP non synchronisées sont refusées.
- Rétroéclairage 4,3″ : portage de l'échelle Arduino validée sur EXIO2 et PWM CH32 registre `0x05` à l'adresse `0x24`. Curseur LVGL réellement actif et valeur enregistrée en NVS, sans changement RGB/PCLK/GT911. Le réglage matériel reste à vérifier sur l'écran physique.
- **Caméra native 4,3″ (première tranche)** : `POST /camera/config` du plugin historique (formulaire `key,name,url`) avec les mêmes clés NVS `miracam` FNV-1a et une validation de l'existence du widget. Un bouton caméra LVGL ouvre une vue plein écran ; une tâche FreeRTOS dédiée télécharge les snapshots HTTP **BMP RGB24 non compressés et JPEG** et les convertit en RGB565 dans la PSRAM, puis transfère les images exclusivement à la tâche LVGL. Le retour ferme la vue et interrompt l'acquisition. **JPEG est maintenant pris en charge par le composant Espressif `esp_jpeg` épinglé en version 1.3.2** (RGB565 avec réduction 1/2, 1/4 et 1/8 si nécessaire). **MJPEG multipart et HTTPS ne sont pas encore pris en charge** ; validation caméra physique toujours nécessaire. Le firmware historique peut toujours être restauré, sans effacement du stockage.
- **Veille native (première tranche)** : délai 0–3600 secondes, luminosité 0–100 % et horloge en veille réglables dans l'écran Paramètres et stockés sous `mira_idf_ui` en NVS. Le premier appui GT911 réveille l'écran **sans envoyer d'action domotique**. Le retour à l'accueil à 60 secondes ne démarre un nouveau cycle de veille qu'après son propre délai. Synchronisation avancée des paramètres `DISPLAY_*` jour/nuit via la page Jeedom historique restant à finaliser.
- **Heure locale** : démarrage SNTP `pool.ntp.org` uniquement après obtention de l'IP station, fuseau Europe/Paris avec heure d'été/hiver. Date du widget actualisée et horloge de veille indépendante de la présence d'un widget date/heure. Le réseau Wi-Fi n'est pas bloqué pendant l'attente d'un serveur horaire.
- Nouveau : Wi-Fi AP+STA, configuration volontaire par `POST /mira/wifi`, sans effacer `reseau.json`. Formulaires accessibles sur `/mira`.
- Sécurité du banc : écriture réseau/MQTT, import SPIFFS et OTA accessibles uniquement aux clients connectés à l'AP de récupération (192.168.4.1). L'AP de test utilise encore un mot de passe connu : ce n'est **pas** une authentification de production.
- Couche HTTP Jeedom **partielle** : `/testmodule`, `/getmodule`, `/getlvgl`, `/getindex`, `/programui`, `/displayinfo`, `/updatenr`. Format des commandes et de la programmation vérifié dans le plugin `jeedom-beta`. La clé de l'écran doit être provisionnée **sur l'AP de secours** depuis `/mira` ; le portage ne déchiffre pas automatiquement l'ancien mot de passe.
- En complément : `/getparam`, `/addlistparam`, `/updatelistparam`, `/savelistparam`, `/createlistparam`, `/setjeedomid`, `/setjeedomtoggleid`, `/sendjeedomorderid`. Paramètres et mappings conservés dans des namespaces NVS distincts, sans toucher à l'ancien `/params.json`. **Attention : stockage NVS non chiffré des paramètres sensibles dans ce prototype**, ne pas le traiter comme production.
- La programmation `/programui` réécrit uniquement `/miralvgl.json` via fichier temporaire puis demande le rechargement sur la tâche LVGL ; aucun effacement SPIFFS et aucun redémarrage forcé.
- Builds GitHub Actions natifs épinglés 5.5.6 et 9.6 ; surveiller le dernier commit, ne jamais confondre un ancien succès avec la validation du HEAD.

## Reste à porter avant un firmware Mira complet
1. **Valider matériellement** le cycle d'initialisation Jeedom, les tables `value=json` et les actions réelles avec un équipement de test avant toute utilisation domotique. Ajouter SSE, authentification/stockage sûr des secrets et vérifier l'absence de double commande MQTT/HTTP.
2. Parité complète de l'interface 0.7.37 : composites complexes, flux caméra **MJPEG multipart**, thermostat riche, favoris, réglages jour/nuit et styles exacts. Les fonctions déjà codées doivent être vérifiées sur matériel.
3. Import automatique et sécurisé des paramètres réseau/MQTT conservés dans `reseau.json` et `params.json`, y compris les valeurs héritées chiffrées.
4. Réglages de veille, luminosité et pilotage d'affichage, OTA/publishing de production.
5. Tests matériels de régression, upload, reboot, tactile, défilement et fonctions Jeedom/MQTT.
6. Portage du 7" dans le même projet avec sa configuration matérielle propre, **après** stabilisation du 4,3".

Le prototype compile parfois avec succès mais **n'est pas encore un firmware de production**. Ne pas annoncer que le portage est terminé tant que cette liste n'est pas vérifiée.

## Référence de configuration : `firmware-beta` (1er novembre 2026)

Le code Arduino de référence se trouve dans `firmware/source/consolidated/MIRA_PANEL/`.
La branche `firmware-beta` définit les schémas source :
- `SPIFFS_Process.h` : `/params.json` avec `params[]` (`param`, `value`, `type`), `/reseau.json` avec `reseau[]` (`ssid`, `pass`, `ip_mode`, `ip`, `mask`, `gateway`, `dns1`, `dns2`) et `ap[0].passcrypt`.
- `MiraPanelServer.cpp` : paramètres `Jeedom`, `Jeedom_IP`, `Jeedom_Key_mira`, `Jeedom_Refresh`, `Jeedom_Init_Cmd` ; mode historique HTTP par défaut pour le portage. Les paramètres DISPLAY de la bêta sont la référence pour la veille, les luminosités jour/nuit et la plage horaire.
- `Cipher.cpp` : encodage AES-128-ECB historique des champs marqués `crypted-text` et des mots de passe réseau. La reprise est conditionnée à un déchiffrement valide ; jamais de copie de texte chiffré brut comme mot de passe.

**Porté dans le nouveau firmware (expérimental, validation matérielle encore nécessaire)** :
- Import en lecture seule des paramètres HTTP/Jeedom depuis le fichier bêta `params.json`, uniquement lorsque la clé NVS ESP-IDF correspondante n'existe pas déjà.
- Import conditionnel des valeurs historiquement chiffrées, avec vérification des octets déchiffrés ; si illisibles, conserver NVS et demander un provisionnement via l'AP de récupération.
- Réutilisation conditionnelle de la clé d'écran/plugin `ap[0].passcrypt` ; pas d'affichage des secrets dans les logs.
- Reprise du dernier profil Wi-Fi **DHCP** issu de `reseau.json` uniquement si le Wi-Fi ESP-IDF n'est pas déjà configuré.
- Reprise bornée de `DISPLAY_Delai` (jusqu'à 3600 s), `DISPLAY_Veille` et `DISPLAY_HorlogeVeille` sans toucher à la luminosité matérielle déjà validée.
- Aucun fichier SPIFFS historique n'est effacé, réécrit ou formaté.

**Encore à réaliser/valider** : profils `manual` et `fixed_auto` (IP statique), plages de luminosité jour/nuit, fidélité de tous les widgets, reprise de tous les paramètres et tests physiques 4,3″ / 7″. **Un build qui passe ne prouve pas le bon fonctionnement des données chiffrées provenant d'un appareil réel.**

## Lot suivant : réseau IP fixe et horaires jour/nuit (ESP-IDF expérimental)

- Reprise du dernier profil bêta `manual` : adresse IPv4, masque, passerelle et DNS validés avant l'application via `esp_netif`. Aucun écrasement du réseau ESP-IDF déjà configuré.
- Reprise de `fixed_auto` : DHCP initial, vérification que l'adresse demandée est sur le réseau DHCP, puis utilisation de cette adresse avec les paramètres réseau obtenus. Retour DHCP si l'application échoue ; vérification répétée lors des reconnexions.
- Les imports Wi-Fi de la bêta sont marqués en NVS ; un nouveau provisionnement par l'utilisateur désactive ce profil migré et réactive DHCP.
- `GET /mira/wifi` expose le mode IP et le statut d'application, sans exposer mot de passe, clé ou adresse.
- Horaires de luminosité `DISPLAY_DebutNuit` / `DISPLAY_FinNuit`, activation `DISPLAY_NuitAuto`, pourcentages jour/nuit : lecture depuis les paramètres bêta, calcul sur l'horloge locale après synchronisation de l'heure.
- La luminosité manuelle règle le niveau de la période active si l'automatisation est sélectionnée. La veille conserve sa luminosité indépendante.
- Les endpoints HTTP hérités `/getparam` / `/updatelistparam` et `/addlistparam` reconnaissent les huit paramètres `DISPLAY_*` migrés, les enregistrent en NVS typé et signalent à la tâche LVGL d'appliquer les changements sans interaction directe depuis HTTP.
- `DISPLAY_Delai` accepte désormais jusqu'à 86400 secondes, comme la bêta ; la lecture d'anciennes valeurs NVS `u16` reste possible.

**Prudence** : ces fonctions sont codées mais restent **à vérifier physiquement** ; une réussite du compilateur ne valide ni le basculement DHCP/static, ni la stabilité du rétroéclairage, ni les heures de nuit. Le portage 7 pouces et la parité visuelle complète sont toujours en cours.
