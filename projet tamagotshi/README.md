# Projet Tamagotshi 🐣 — V3 grand écran

**Matériel :** LilyGO **T-Display original V1.1**, ESP32 classique, LCD ST7789 135 × 240 en portrait, deux boutons intégrés.

## Interface et comportement
- Scène principale sans barres ni jauges : personnage bien plus grand, expressions animées et réactions.
- **4 stades** : œuf (0–1 minute), bébé (1–7 minutes), enfant (7–30 minutes), adulte ensuite. Durées volontairement accélérées pour le test.
- Clignements, regards, petits sauts, déplacements, bras qui saluent, larmes, sommeil avec Zzz et effets visuels lors des actions.
- États selon les besoins : content, neutre, triste, malade, endormi.
- Menu 7 actions : **MANGER, JOUER, DODO, BAIN, SOINS, CADEAU, INFOS**.
- Mini-jeu d'adresse **Attrape l'étoile** : valider quand l'étoile est au centre, 3 tentatives, points de joie.
- Les cinq jauges sont **uniquement sur la page INFOS**, ainsi que l'âge et le score de jeu.
- Sauvegarde régulière en NVS et après les actions. Données compatibles avec la V2.

### Commandes
- Bouton **GPIO35** : action suivante ; depuis INFOS, retour ; pendant le mini-jeu, quitter.
- Bouton **GPIO0 (BOOT)** : valider ; depuis INFOS, retour ; pendant le mini-jeu, attraper l'étoile.

Aucune connexion Wi-Fi, aucun câble supplémentaire. Pas de temps réel pendant une coupure électrique (pas de RTC) : l'âge progresse uniquement lorsque la carte reste alimentée.

## Flasher
La compilation GitHub Actions crée deux images :
- **[Tamagotchi_V3_TDisplay_V11_full.bin](firmware/Tamagotchi_V3_TDisplay_V11_full.bin)** : pour Flash Download Tools à l'adresse **0x0000**, installation complète.
- **[Tamagotchi_V3_TDisplay_V11_app.bin](firmware/Tamagotchi_V3_TDisplay_V11_app.bin)** : application seule à **0x10000**, à privilégier pour une mise à jour depuis V2 afin de conserver les paramètres NVS si le flash n'est pas effacé.

Sélectionner **ESP32**, UART, DIO, 40 MHz, 32 Mbit (4MB), 115200 bauds.

Les firmwares V1 et V2 restent disponibles dans le dossier firmware. Projet indépendant de Mira Panel.
