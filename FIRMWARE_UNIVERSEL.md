# Mira Panel : un projet, plusieurs matériels

## Principe

Une seule base applicative **Mira Panel 0.6.52**, avec **un binaire spécifique à chaque cible**, compilé et publié ensemble. Il serait dangereux de rendre les images binaires interchangeables entre le WT32, le Waveshare et un futur Nextion, car les interfaces graphiques, le partitionnement et les pilotes ne sont pas les mêmes.

| Profil | Matériel | Flash | Particularités | Validation |
| --- | --- | ---: | --- | --- |
| `wt32-sc01-plus` | WT32-SC01 Plus, 320×480 | 8 Mo | LVGL 8.3.11, allocations LVGL en PSRAM | 0.6.52 testée |
| `waveshare-esp32s3-touch-lcd-4.3c` | Waveshare 4.3C, RGB 800×480 | 16 Mo | LVGL 8.4 fournisseur, triple-buffer et 12 MHz RGB | 0.6.52 compilée, validation matérielle en cours |
| `opennextion-onx2432g028` | Open Nextion 2,8 pouces, 240×320 | 16 Mo | Pilote ST7789, tactile CST826 | Projet futur, non publié |

## Publication unique

1. `.github/workflows/build-mira.yml` reconstruit le code commun et compile séparément les cibles officiellement supportées.
2. Les artefacts GitHub Actions de chaque cible comprennent OTA et FACTORY.
3. Le workflow de publication vérifie que les deux artefacts proviennent d'un build **réussi sur le même commit de `main`**.
4. `firmware/scripts/publish_multi.py` prépare une nouvelle version atomique : il valide tous les binaires avant de modifier les manifestes du dépôt.
5. `firmware/targets.json` publie les chemins OTA/FACTORY, tailles et SHA-256 pour les deux profils ; le Web Installer expose des manifestes séparés.

Si l'un des firmwares manque ou échoue à la validation, **aucune publication partielle**.

## Installation et identification du matériel

**Mise à jour OTA :** conserver la configuration SPIFFS et envoyer uniquement le binaire OTA correspondant au profil réel de l'écran. Un firmware Waveshare ne doit jamais être envoyé au WT32, et réciproquement.

**Flash USB usine :** le navigateur utilise [Web Installer](https://arokh76.github.io/Mira-panel/) et un choix explicite de modèle. L'identification du processeur ESP32-S3, de la flash ou de la PSRAM ne permet pas à elle seule de déduire le modèle de la carte. Les images FACTORY peuvent effacer l'installation présente.

Les anciennes URL `firmware/version.json`, `firmware/Mira_Panel.bin` et `docs/manifest.json` sont conservées **uniquement pour la rétrocompatibilité WT32**.

L'auto-détection certaine du matériel et les mises à jour orchestrées par Jeedom restent des évolutions à réaliser, pas des fonctions déjà garanties.

## Choix techniques à conserver

- WT32 : ne pas réintroduire le pool statique LVGL de 96 Ko en DRAM interne ; l'allocation des objets LVGL en PSRAM résout le problème de marge mémoire observé sur l'écran réel en 0.6.52.
- Waveshare : garder le port RGB fournisseur 800×480, LVGL 8.4, pixel clock 12 MHz, triple-buffer, les buffers DMA et son partitionnement 16 Mo.
- Jeedom : les retours d'état et la restauration après démarrage ne doivent **jamais** exécuter une commande physique à la place de l'utilisateur.
- Build : ne pas supprimer les scripts de reconstruction historiques tant qu'un snapshot consolidé n'a pas produit et validé les deux firmwares.

## Prochaines évolutions

Les évolutions suivantes repartent de la **0.6.52 commune**. Le port Open Nextion n'entrera dans les manifests publiés qu'après son build et les essais matériels. La rationalisation ultérieure des sources se fera séparément pour ne pas casser la chaîne de build actuellement reproductible.

Références : [Open Nextion officiel](https://open-nextion-app-store.nextion.tech/devices/onx2432g028/), [projet Open Nextion](https://github.com/OpenNextion/OpenNextion-SKU-ONX2432G028), [esptool-js](https://github.com/espressif/esptool-js).
