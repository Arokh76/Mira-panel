# Mira Panel — LVGL 9.6.0 / Waveshare 4.3C (preuve de concept)

**Statut : EXPÉRIMENTAL, pas encore un firmware Mira utilisable.**
Aucun changement à la stable 0.7.37, aux firmwares WT32/Nextion ou aux fichiers du plugin Jeedom.

## Pourquoi

Les mesures J sous LVGL 8.4.0, en défilement continu, montrent couramment :
- traitement dessin de **100–143 ms/image**, souvent près de 140 ms ;
- synchronisation de buffers PSRAM **0–0,1 ms**, sauf une pointe à 45,9 ms ;
- attente RGB VSYNC entre **0 et 34 ms** ;
- environ **5–6 images/s** en pratique.

La migration vers **LVGL 9.6.0** est donc justifiée comme essai du moteur de rendu logiciel amélioré, **sans garantie de gain**.

## Étape 1 : test de compatibilité matériel (ce dossier)

- Nouvelle compilation GitHub Actions **Waveshare 4.3C uniquement**, isolée du workflow multi-cibles.
- LVGL **v9.6.0** épinglé ; Arduino ESP32 core **3.3.11**, PSRAM OPI.
- LCD RGB **800×480 RGB565, PCLK 12 MHz**, mêmes broches/timings, 2 framebuffers PSRAM.
- Adaptation des **nouvelles API LVGL 9** : `lv_display_create`,
  `lv_display_set_buffers(... size_in_bytes ... LV_DISPLAY_RENDER_MODE_DIRECT)`,
  `lv_display_set_flush_cb`, `lv_display_flush_ready`.
- Scène de 35 cartes avec arrondis, ombres, textes et défilement, similaire en complexité aux widgets Mira.
- Essai du tactile GT911 par lecture directe I2C, sans utiliser le pilote LVGL 8 de Waveshare.
- Compteur série d'images envoyées, timeouts VSYNC, RAM disponible.

**Ne pas flasher le binaire de cette étape sur l'appareil qui sert à piloter Jeedom.**
Cette application de test **remplace temporairement Mira**, n'est **pas** un OTA de production et n'a pas encore été testée physiquement. À ce stade, un succès de compilation ne garantit pas que l'écran fonctionne.

## Étape 2 : migration de Mira

Une fois le port RVB/tactile validé, adapter progressivement les API des widgets de
`MiraPanelUI` et du moteur d'affichage à LVGL 9.6 : couleurs RGB565, images,
gestion des événements, timers, snapshots, polices, angles de rotation et
configuration de mémoire. Réintroduire l'ensemble des fonctions Mira,
puis récupérer une mesure de FPS sur **la même page** que sous LVGL 8.

## Garde-fous

- Branches **draft PR uniquement**, aucune modification de la stable.
- La preuve de concept est **sans Jeedom et sans réglages Mira**, ne pas la présenter comme un firmware final.
- Avant un premier flash, prévoir accès USB, sauvegarde et image usine de restauration.
- Ne pas exporter un firmware 7 pouces ou WT32 depuis cet essai 4,3C.

## Source

LVGL v9.6.0: https://github.com/lvgl/lvgl/releases/tag/v9.6.0
LVGL migration 8 vers 9 : https://docs.lvgl.io/master/details/integration/migration/v8_v9.html
