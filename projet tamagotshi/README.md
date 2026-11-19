# Tamagotchi V7.3 — Tama un peu plus petit dans la chambre

Compatible **LilyGO / TTGO T-Display V1.1 original** (ESP32, ST7789 135 × 240).

## Retouche visuelle

- **Dans la chambre** : le corps du Tama est réduit d'environ **15 %** (adulte 108 × 135 px auparavant, maintenant 92 × 118 px). Il est positionné un peu plus bas pour laisser voir davantage la fenêtre, l'horloge, l'étagère et le lit.
- **Le jardin ne change pas** : grand personnage au repos, version réduite durant les promenades.
- **Pendant DODO** : il rejoint toujours automatiquement la chambre, avec cette taille adaptée et ses petits Z animés.
- Le **double ventre blanc et gris clair de V7.2**, valide par l'utilisateur, est conserve, simplement ajuste proportionnellement a la taille de la chambre.
- Oreilles, bras, joues et bouche restent alignes sur la nouvelle silhouette. Les 4 accessoires et couleurs d'evolution restent identiques.

## Tout le reste conserve

Menu MAISON et decorations (fleurs / ballon / doudou), vraie douche avec pommeau et jets d'eau, animations de repas, soins, cadeaux et calins, mini-jeux, sac, evolutions, gestion LiPo et USB.

Aucune modification des sauvegardes NVS (`tama_v2`) ou des commandes.

## Flasher depuis V7.2

- [Tamagotchi_V7_3_TDisplay_V11_app.bin](firmware/Tamagotchi_V7_3_TDisplay_V11_app.bin) a **0x10000, sans ERASE** pour conserver l'animal.
- [Tamagotchi_V7_3_TDisplay_V11_full.bin](firmware/Tamagotchi_V7_3_TDisplay_V11_full.bin) a **0x0000** uniquement pour reinstaller completement, risque d'effacer les donnees NVS.

ESP32 classique, DIO, 40 MHz, 4 MB, 115200 bauds. Compilation automatique via GitHub Actions. Le rendu doit encore etre valide sur le materiel reel.
