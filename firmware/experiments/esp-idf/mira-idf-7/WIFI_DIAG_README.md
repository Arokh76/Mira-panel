# Mira 7 STD — étape 3 : graphique validé + test Wi-Fi indépendant

Cette branche est **un test isolé**, pas un firmware Mira complet.
Elle reprend **sans les modifier** les paramètres RGB/CH422G/GT911/LVGL/PSRAM
de l'étape 2, validés physiquement (tactile et interface de démarrage fluides).

## Ce qui démarre
- ESP-IDF 5.5.6, LVGL 9.6 et driver Waveshare 7 standard.
- L'interface Mira depuis l'éventuel JSON SPIFFS existant, **sans formatage**.
- Un petit repère vert mobile dans le coin de l'écran.
- Un **point d'accès temporaire ouvert** : \`Mira Panel TEST\`.
- Un serveur HTTP minimal sur \`http://192.168.4.1/\` et \`/status\`.
- DNS captif et quelques routes de détection Android/iOS/Windows.
- Journaux USB \`NET_DIAG_READY\`, \`AP_STACONNECTED\` et \`AP_STAIPASSIGNED\`.

## Ce qui reste désactivé
Aucune connexion à la Livebox, aucune restauration de mot de passe, aucun Jeedom,
MQTT, caméra, worker album, API de modification des paramètres, OTA ou
pilotage d'équipements. C'est un AP **local et temporaire** sans Internet.

## Comment flasher
1. Ouvrir le build GitHub Actions réussi de cette branche.
2. Télécharger l'artefact \`MIRA7_WIFI_DIAG_FACTORY_OTA\`.
3. Utiliser \`MIRA7_WIFI_DIAG_FACTORY_USB_0x0.bin\` **à l'adresse 0x0**.
4. **Ne PAS cocher Erase Flash / effacement complet** : conserver NVS/SPIFFS.

## Validation
1. L'interface Mira et le repère vert continuent de fonctionner sans noir
   ni décalages, pendant au moins quelques minutes après activation Wi-Fi.
2. Depuis Windows ou un téléphone : rejoindre \`Mira Panel TEST\` (ouvert).
3. Accepter l'avertissement « connexion sans Internet ».
4. Ouvrir manuellement \`http://192.168.4.1/\`. Le portail automatique peut
   s'ouvrir ou non ; l'adresse directe est le test de référence.
5. Vérifier \`http://192.168.4.1/status\` : JSON avec refreshes LVGL, GT911,
   états AP et mémoire DMA encore disponible.

Un second échec AP sous cette version n'accuserait plus la gestion Jeedom,
MQTT ou les caméras, puisqu'ils ne sont pas démarrés. S'il reste un écran noir
ici, comparer les logs \`OFFLINE_UI_READY\` vs \`NET_DIAG_READY\` et la quantité
de mémoire DMA libre avant le démarrage Wi-Fi.

## Essai DMA 10 lignes (après logs ESP_ERR_NO_MEM)
- Logs transmis le 7 novembre 2026 : `DMA-internal free=106839`,
  `largest=53248`; `lcd_rgb_panel_alloc_frame_buffers: no mem for bounce buffer`.
- Le code précédent passait 16000 PIXELS par bounce buffer (2 × 32000 octets).
- Cet essai passe réellement 800 × 10 = 8000 PIXELS, donc 2 × 16000 octets.
- **Aucun autre réglage d'affichage ou de réseau n'est modifié.**
- Écran noir et Wi-Fi absent sur l'ancienne version : elle s'arrêtait
  avant la création du réseau. Le Wi-Fi reste **à valider physiquement**.
- Flasher `MIRA7_WIFI_DMA10_FACTORY_USB_0x0.bin` à l'adresse 0x0,
  **Erase Flash désactivé**.
