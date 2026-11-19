# Projet Tamagotshi V7.1 — silhouettes harmonisees

Pour **LilyGO / TTGO T-Display V1.1 ORIGINAL** (ESP32, ST7789, 135 × 240 portrait).

## Changements V7.1

- Suppression totale du **triangle jaune sous la bouche** du Joueur, en grande taille **et pendant la promenade**.
- Ventre blanc **descendu sous la zone du visage**, avec une forme arrondie unique et un contour discret. L'ancien double cercle blanc/gris a ete supprime.
- Meme silhouette de base pour les quatre formes adultes, qui gardent **leur couleur et leurs accessoires de tete** : biscuit (Gourmand), etoile (Joueur), coeur (Calin), couronne (Sage).
- Adaptation du placement et des proportions du ventre pour l'Enfant, l'Adulte et le petit personnage de promenade.
- Aucun changement de menus, commandes, humeur, seuils batterie ni comportement des animations.

## Fonctions V7 conservees

Animations de repas/bain/soins/cadeau/calin, menu **MAISON** avec Jardin ou Chambre, decorations debloquables par les activites (fleurs, ballon, doudou), mini-jeux, inventaire, personnalites et evolutions, nuages et collines en parallaxe, sommeil avec Z, animation USB, batterie rouge sous 10 %.

La sauvegarde reste dans le **namespace NVS `tama_v2`**, avec les preferences existantes d'age, evolutions, objets, lieu et decoration. Aucune migration destructive.

## Flash Download Tools

- [Tamagotchi_V7_1_TDisplay_V11_app.bin](firmware/Tamagotchi_V7_1_TDisplay_V11_app.bin) : flasher a **0x10000, sans ERASE** depuis V7.0, pour conserver l'animal.
- [Tamagotchi_V7_1_TDisplay_V11_full.bin](firmware/Tamagotchi_V7_1_TDisplay_V11_full.bin) : flasher a **0x0000**, pour reinstallation complete (risque d'effacement des sauvegardes).

ESP32 classique / DIO / 40 MHz / 4 Mo / 115200 bauds.

La compilation automatisee confirme le firmware, mais le rendu exact doit etre verifie sur le TTGO. ESP-NOW est toujours reporte.
