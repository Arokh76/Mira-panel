# Projet Tamagotshi - V4 🐣

Pour le **LilyGO T-Display V1.1 original**, ESP32 et ecran ST7789 **135x240 portrait**.

## Nouveautes V4
- La ligne `35 > MENU / 0 OK` a disparu de l'ecran principal : seulement le nom de l'action en bas.
- Un **second mini-jeu, REFLEXE** : appuyer quand la zone devient verte (« TOP ! »), 3 manches.
- Une nouvelle action **CALIN**, qui augmente la joie et l'amitie, avec coeurs animes.
- **Amitie de 0 a 100** : augmente avec les calins, les jeux et les cadeaux, decroit doucement.
- **Petites surprises autonomes** environ toutes les 3 minutes d'activite, avec reaction du Tama.
- Des **danses spontanees** avec notes de musique quand il est content, et une couleur speciale pour l'adulte tres attache.
- Nouvelle page **SOUVENIRS** : amitie, nombre de calins, surprises, parties et victoires aux deux mini-jeux.
- La scene principale reste dediee a un grand personnage anime. Les 5 jauges ne sont que dans INFOS.
- Toutes les statistiques sont sauvegardees en NVS ; les sauvegardes V2/V3 restent lisibles.
- Cycle accelere de test conserve : oeuf (1 min), bebe (7 min au total), enfant (30 min), adulte.

## Commandes (deux boutons integres)

| Ecran | Bouton GPIO35 (defiler) | Bouton GPIO0 (valider) |
|---|---|---|
| Accueil | Menu suivant | Executer action |
| INFOS / SOUVENIRS | Alterner les pages | Retour accueil |
| ETOILE | Quitter | Attraper l'etoile au centre |
| REFLEXE | Quitter | Valider au signal « TOP ! » |

Le programme fonctionne entierement **hors ligne** et ne requiert ni cablage supplementaire ni serveur. L'age n'avance pas quand la carte est eteinte.

## Telecharger / flasher

La [compilation automatique](../../actions/workflows/tamagotchi-build.yml) produit :
- [**Tamagotchi_V4_TDisplay_V11_full.bin**](firmware/Tamagotchi_V4_TDisplay_V11_full.bin) : image complete a flasher a **0x0000**.
- [**Tamagotchi_V4_TDisplay_V11_app.bin**](firmware/Tamagotchi_V4_TDisplay_V11_app.bin) : application seule a **0x10000**, pour mettre a jour depuis V3 sans effacer la NVS si la disposition de partitions reste identique.

Flash Download Tools : **ESP32**, **UART**, **DIO**, **40 MHz**, **32 Mbit / 4 Mo**, **115200 bauds**. Eviter l'option ERASE avant une mise a jour application seule.

Les anciennes versions et leurs BIN restent dans le dossier firmware. Aucun changement au projet Mira Panel.
