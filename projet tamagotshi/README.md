# Tamagotchi V7.2 — retour du double ventre et vraie douche

Pour le **LilyGO / TTGO T-Display V1.1 original** (ESP32, ST7789 135 x 240).

## Corrections demandées

- **Ventre** : retour du dessin prefere de V7.0, avec un grand ovale **blanc** et un second ovale **gris clair** a l'interieur. Les deux ovales sont simplement **descendus** pour ne plus toucher le visage, aussi bien en plein format que pendant les promenades.
- **Joueur** : le triangle jaune **sous la bouche reste supprime** ; l'etoile sur la tete conservee.
- **Bain** : ajout d'un vrai **pommeau de douche** visible en haut a gauche, de jets d'eau qui tombent et s'animent sur le personnage, de mousse au sol. Les bulles continuent de monter, mais ne sont plus le seul effet affiche.
- Le reste des quatre formes, leurs couleurs et accessoires, les animations et les jeux ne change pas.

## Fonctions conservees

V7.0 : petits gestes lors des repas, soins, cadeau et calin ; Maison Jardin/Chambre, decorations debloquables, jardin anime, promenade, sommeil Zzz, jeux et inventaire, NVS et gestion batterie. ESP-NOW reporte.

## Flasher

- [Tamagotchi_V7_2_TDisplay_V11_app.bin](firmware/Tamagotchi_V7_2_TDisplay_V11_app.bin) a **0x10000**, **sans ERASE**, pour conserver ton Tama.
- [Tamagotchi_V7_2_TDisplay_V11_full.bin](firmware/Tamagotchi_V7_2_TDisplay_V11_full.bin) a **0x0000** uniquement pour reinstaller, car ce flash peut effacer les sauvegardes.

Configuration : ESP32 classique, DIO, 40 MHz, 4 Mo, 115200 bauds. La compilation verifie le code, mais le rendu et les animations doivent etre testes sur le vrai TTGO.
