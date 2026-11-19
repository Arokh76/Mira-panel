# Projet Tamagotshi V7.0 — scenes de vie et petite maison

Pour LilyGO / TTGO T-Display **V1.1 ORIGINAL** (ESP32, ST7789, portrait 135 × 240).

## Les nouveautes V7.0

- **Repas anime** : le biscuit se rapproche de la bouche du Tama ; il mache avant de finir.
- **Bain anime** : eau, mousse et petites bulles montantes.
- **Soins** : pose d'un pansement et de petites croix de guerison.
- **Cadeau** : un paquet apparait, son couvercle s'ouvre et des etincelles s'envolent.
- **Calin** : coeurs flottants et animation d'etreinte.
- **Jeu / ballon** : ballon rebondissant (mini-jeux precedents toujours disponibles).
- **MAISON** : nouveau menu pour choisir **JARDIN** ou **CHAMBRE**, avec fenetre (jour/nuit), petit lit, horloge et etagere. Lorsque le Tama dort, il rejoint automatiquement la chambre (sans changer le choix enregistre). Le jardin conserve ses collines en mouvement et ses nuages.
- **Decorations debloquables**, choisies dans MAISON : FLEURS apres 3 repas, BALLON apres une partie ETOILE parfaite, DOUDOU apres une partie REFLEXE parfaite **ou trois surprises**. Une seule decoration a la fois, et le choix de jardin/chambre ainsi que l'objet choisi sont sauvegardes.

## Commandes (2 boutons)

Sur l'accueil, GPIO35 fait defiler le menu (qui inclut **MAISON** en dernier) ; GPIO0 valide. Dans MAISON, GPIO35 fait defiler les lieux et decorations ; GPIO0 les selectionne. "X" indique une decoration encore verrouillee ; "OK" indique l'option selectionnee. Selectionner RETOUR pour revenir sans changement.

Les anciennes fonctions sont conservees : personnalite, forme adulte, sac, mini-jeux, lissage du decor, taille reduite en balade, Z et ronflement pendant le sommeil, gestion batterie, animation USB, alertes 10 %, sauvegarde NVS.

## Compatibilite et sauvegarde

Namespace `tama_v2` et MAGIC V2 conserves. Deux nouvelles preferences sont ajoutees : `room` et `decor` ; elles prennent la valeur 0 si absentes. Les jeux, repas, inventaire, evolution et age restent preserves. Les decorations s'appuient sur les **compteurs deja enregistres**, pas sur un nouveau systeme de points.

## Flash Download Tools

- [Tamagotchi_V7_0_TDisplay_V11_app.bin](firmware/Tamagotchi_V7_0_TDisplay_V11_app.bin) : **adresse 0x10000, sans ERASE** pour conserver l'animal actuel.
- [Tamagotchi_V7_0_TDisplay_V11_full.bin](firmware/Tamagotchi_V7_0_TDisplay_V11_full.bin) : **adresse 0x0000**, reinstallation complete pouvant effacer les sauvegardes.

ESP32 classique, DIO, 40 MHz, 4 Mo, 115200 baud.

**ESP-NOW, reproduction/generations et sons ne font PAS encore partie de cette version.** La compilation automatisee valide le firmware, mais le rendu visuel devra etre teste sur la carte reelle.
