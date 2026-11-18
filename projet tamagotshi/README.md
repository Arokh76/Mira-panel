# Projet Tamagotshi – V2 🐣

Firmware autonome pour le **LilyGO T-Display V1.1 original** (ESP32, ST7789, portrait **135 × 240**).

## Nouveautés V2
- Interface portrait, personnage anime et clignement des yeux
- Cinq jauges : faim (**F**), joie (**J**), energie (**E**), proprete (**P**), sante (**S**)
- Deux boutons integres : **GPIO35 = menu suivant**, **GPIO0 (BOOT) = valider**
- Actions : MANGER, JOUER, DODO, LAVER, INFOS
- Cycle de vie accelere pour les tests : **oeuf pendant 1 minute**, **bebe jusqu'a 7 minutes**, puis **enfant**
- Besoins et humeur qui evoluent automatiquement
- Sauvegarde dans la NVS (memoire flash) sur les actions, l'evolution et toutes les 60 secondes
- 100 % hors ligne : ni Wi-Fi, ni MQTT, ni Jeedom, ni IA

**Important** : l'age avance uniquement quand l'ESP32 reste alimente (pas de RTC ni de NTP). La sauvegarde conserve l'etat lors d'un redemarrage normal, mais le flash complet peut reinitialiser les donnees.

## BIN a flasher (Espressif Flash Download Tools)

Le workflow [Tamagotchi V2](../../actions/workflows/tamagotchi-build.yml) compile les firmwares :

- [`Tamagotchi_V2_TDisplay_V11_full.bin`](firmware/Tamagotchi_V2_TDisplay_V11_full.bin) : image complete, a l'adresse **`0x0000`**. A utiliser pour installer cette V2.
- [`Tamagotchi_V2_TDisplay_V11_app.bin`](firmware/Tamagotchi_V2_TDisplay_V11_app.bin) : application seule, a l'adresse **`0x10000`**. Pour des mises a jour ulterieures avec la meme table de partitions, en preservant la NVS.

Reglages : puce **ESP32**, **DIO**, **40 MHz**, **32 Mbit (4 Mo)**, **UART**. Baud 115200.

Le fichier V1 est conserve dans ce dossier pour revenir a la version de test precedent si besoin.

**Attention** : ce firmware ne convient pas aux T-Display S3.

## Structure
- `src/main.cpp` : logique de la bestiole, graphiques, menus, sauvegarde
- `platformio.ini` : configuration de la carte et de l'ecran
- `.github/workflows/tamagotchi-build.yml` : compilation automatique

Projet independant de Mira Panel.
