# Projet Tamagotshi V6.1 — monde anime et promenades

Cette version conserve **toutes les fonctions de la V6** et ajoute un jardin vivant derriere le Tama sur son **LilyGO T-Display V1.1 original, ESP32, ST7789, 135 × 240**.

## V6.1 : decor dynamique

- **Ciel, soleil, nuages, collines, arbres, herbe et petites fleurs** en pixel art leger, dessines directement dans le sprite TFT. Aucun telechargement ou fichier image necessaire.
- **Cycle jour / crepuscule / nuit / aube** durant 12 minutes d'age actif pour faciliter les tests ; lorsque le Tama est couche (DODO), le decor est nocturne avec lune et etoiles.
- **Balade spontanee** : le Tama se met a marcher pendant une phase de son animation existante (environ 7 secondes sur 29). Le sol et les fleurs defilent plus vite que les collines, pour creer un effet de profondeur (parallaxe), sans saut lors de la fin de balade.
- **Nuages doux en mouvement** meme quand le personnage ne se deplace pas.
- Le paysage reste derriere le personnage ; bulle de dialogue, menu bas, jauge batterie et animations de la creature restent au premier plan.
- **Ecrans INFOS, PROFIL, SOUVENIRS, SAC, mini-jeux et USB** non modifies.
- Pas de sauvegarde supplementaire pour le decor : le temps du jour suit l'age actif existant, et la distance de balade ne change pas la progression du Tama.

## Rappel V6

Quatre personnalites (gourmand / joueur / calin / reveur), quatre formes adultes (gourmand / joueur / calin / sage), inventaire (biscuit / ballon / doudou), deux mini-jeux et pages d'information. Les evolutions restent accelerees pour les tests.

## Batterie et sauvegarde

Conservation du namespace NVS `tama_v2` et de la cle `magic` : le Tama de V4/V5/V5.1/V6 garde son age, sa personnalite, son evolution adulte et son sac si l'on flashe l'application **sans ERASE**.

La gestion batterie de V6 reste en place : ecran eteint apres 2 min d'inactivite sur batterie, animation USB probable, alerte batterie rouge et deep sleep a basse tension. La mesure et les seuils necessitent encore validation materielle ; l'affichage « alimentation USB » n'est pas une indication de charge terminee.

## Flash Download Tools

- [**Tamagotchi_V6_1_TDisplay_V11_app.bin**](firmware/Tamagotchi_V6_1_TDisplay_V11_app.bin) à flasher à **0x10000**, **sans ERASE**, depuis V4/V5/V6.
- [**Tamagotchi_V6_1_TDisplay_V11_full.bin**](firmware/Tamagotchi_V6_1_TDisplay_V11_full.bin) à **0x0000** pour installation complète (peut effacer les sauvegardes NVS).

Parametres : ESP32 / DIO / 40MHz / 4MB / 115200 bauds.

**La communication ESP-NOW entre les deux Tamas n'est pas encore presente dans cette version.** La compilation GitHub valide le binaire, mais le rendu devra etre confirme sur le TTGO reel.
