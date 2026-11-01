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
- Nouveau : Wi-Fi AP+STA, configuration volontaire par `POST /mira/wifi`, sans effacer `reseau.json`. Formulaires accessibles sur `/mira`.
- Sécurité du banc : écriture réseau/MQTT, import SPIFFS et OTA accessibles uniquement aux clients connectés à l'AP de récupération (192.168.4.1). L'AP de test utilise encore un mot de passe connu : ce n'est **pas** une authentification de production.
- Couche HTTP Jeedom **partielle** : `/testmodule`, `/getmodule`, `/getlvgl`, `/getindex`, `/programui`, `/displayinfo`, `/updatenr`. Format des commandes et de la programmation vérifié dans le plugin `jeedom-beta`. La clé de l'écran doit être provisionnée **sur l'AP de secours** depuis `/mira` ; le portage ne déchiffre pas automatiquement l'ancien mot de passe.
- La programmation `/programui` réécrit uniquement `/miralvgl.json` via fichier temporaire puis demande le rechargement sur la tâche LVGL ; aucun effacement SPIFFS et aucun redémarrage forcé.
- Builds GitHub Actions natifs épinglés 5.5.6 et 9.6 ; surveiller le dernier commit, ne jamais confondre un ancien succès avec la validation du HEAD.

## Reste à porter avant un firmware Mira complet
1. Compléter la parité Jeedom **au-delà des routes d'identification, programmation, index et états passifs** : paramètres `/getparam`, `/addlistparam`, synchronisation des IDs Jeedom, authentification et commandes sortantes. Pour l'instant, la couche d'entrée existe mais **le plugin n'est pas encore intégralement compatible** ; ne pas flasher pour piloter l'installation.
2. Parité complète de l'interface 0.7.37 (widgets composites énergie, multimédia, caméra, thermostat riche, favoris et styles exacts).
3. Import automatique et sécurisé des paramètres réseau/MQTT conservés dans `reseau.json` et `params.json`, y compris les valeurs héritées chiffrées.
4. Réglages de veille, luminosité et pilotage d'affichage, OTA/publishing de production.
5. Tests matériels de régression, upload, reboot, tactile, défilement et fonctions Jeedom/MQTT.
6. Portage du 7" dans le même projet avec sa configuration matérielle propre, **après** stabilisation du 4,3".

Le prototype compile parfois avec succès mais **n'est pas encore un firmware de production**. Ne pas annoncer que le portage est terminé tant que cette liste n'est pas vérifiée.
