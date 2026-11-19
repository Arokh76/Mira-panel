# Tamagotchi V6.2 — promenade lisible et sommeil vivant 🐣

Pour LilyGO/TTGO T-Display V1.1 **original** (ESP32 ST7789 135 × 240 portrait). Cette version conserve le systeme V6 (personnalites, evolution, sac, mini-jeux, batterie, sauvegardes).

## Nouveautes visuelles

- **Sommeil anime** : trois petits Z flottent et serpentent vers le haut, de tailles et couleurs differentes, accompagnes d'une discrete bulle de ronflement. L'animation se termine des le reveil.
- **Promenade avec Tama plus petit** : pendant les phases de balade automatiques (~7 secondes par cycle de 28,8 secondes), le personnage passe a environ 75–80 % de sa grande taille. Cela libere l'arriere-plan : ciel, nuages, collines, arbres et fleurs.
- **La grande taille au repos est conservee**, avec les expressions habituelles, les danses et les evolutions adultes.
- **Contraste renforce** : collines legerement plus bleutees, sol sable/herbe, et fin liseret fonce autour du personnage. Les quatre couleurs de forme adulte restent presentes.
- Le monde V6.1 conserve sa parallaxe et son cycle jour/nuit accelere. L'ecran de charge USB, les statistiques et les menus ne changent pas.

## Controle

- GPIO35 : defiler dans le menu, suivant.
- GPIO0 : selectionner, valider.
- Menus et ecrans V6 conserves.

## Flash Download Tools

- [Tamagotchi_V6_2_TDisplay_V11_app.bin](firmware/Tamagotchi_V6_2_TDisplay_V11_app.bin) a **0x10000**, **sans ERASE** depuis V6 ou V6.1 : conserve la sauvegarde `tama_v2` (animal, age, evolution, objets).
- [Tamagotchi_V6_2_TDisplay_V11_full.bin](firmware/Tamagotchi_V6_2_TDisplay_V11_full.bin) a **0x0000** pour une installation complete : risque d'effacement NVS.

ESP32 / DIO / 40 MHz / 4 Mo / 115200 bauds.

**La compilation ne remplace pas les essais reels.** Verifier notamment le rendu des Z, le contraste et la fluidite de l'animation. ESP-NOW n'est toujours pas integre.
