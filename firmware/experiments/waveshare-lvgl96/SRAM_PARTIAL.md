## Test LVGL 9.6 — SRAM partial 20 lignes

Essai ciblé après les 5,8 FPS mesurés en LVGL 9.6 / double framebuffer direct PSRAM. L'interface de 35 cartes et le défilement automatique restent identiques pour comparaison.

**Seul changement graphique mesuré** : LVGL dessine dans deux tampons internes SRAM de 800 × 20 × 2 octets (32 Ko chacun), puis transfère chaque zone via `esp_lcd_panel_draw_bitmap` dans les framebuffers RGB en PSRAM. Il utilise `LV_DISPLAY_RENDER_MODE_PARTIAL` plutôt que `DIRECT`. Le PCLK reste 12 MHz, format RGB565, 800×480. Wi-Fi de récupération Mira-LVGL9-Test conservé. Les fichiers de test ne sont pas des firmwares complets Mira. Si panneau noir, repasser par le portail Wi-Fi de récupération ou l'USB.

**Risques** : le chemin RGB partiel n'est pas encore validé physiquement, risque de tearing/affichage noir ou consommation interne de RAM. Aucun merge vers `firmware-beta`/stable. Ne reflasher que si restauration disponible.

**Objectif** : déterminer si les 5,8 FPS proviennent du raster direct en PSRAM. Résultat attendu non garanti ; mesure FPS à l'écran et télémetrie `/perf`.
