# Tamagotchi V6 — personnalites, evolutions et sac

Pour **LilyGO T-Display V1.1 original** (ESP32, ST7789, 135 x 240). Tous les jeux, ecrans, sauvegardes et comportements batterie de la V5.1 sont preserves.

## Nouveautes V6

- **Quatre personnalites** : GOURMAND, JOUEUR, CALIN, REVEUR. Attribuees a l'eclosion, puis sauvegardees. Elles changent les besoins, les reactions et les messages.
- **Quatre formes adultes** : GOURMAND (biscuit orange), JOUEUR (etoile cyan), CALIN (coeur rose), SAGE (couronne verte). Determination a 30 minutes de temps actif selon le nombre de repas, jeux, calins, soins. Forme definitive.
- **Inventaire** (nouveau menu SAC) : un biscuit (+23 faim), ballon (+26 joie, -3 energie), doudou (+20 energie, +12 amitie). Un biscuit au depart. Gagner un ballon avec 3/3 au jeu ETOILE ; un doudou avec 3/3 au jeu REFLEXE. Les surprises peuvent donner des objets. Max 9 par type.
- **Page PROFIL** : INFOS -> SOUVENIRS -> PROFIL -> INFOS (bouton suivant). Voir sa personnalite, son evolution et les activites.
- **Migration automatique V5.1** : sauvegarde Preferences `tama_v2` intacte. Un ancien Tama deja ne recoit une personnalite au premier demarrage et les adultes recoivent une forme a partir des statistiques existantes.

**Commandes** : GPIO35 suivant ; GPIO0 valider. Dans le SAC, selectionner BISCUIT / BALLON / DOUDOU / RETOUR puis valider.

## Evolution acceleree pour developpement

Oeuf : 1 minute ; bebe : 1 a 7 minutes ; enfant : 7 a 30 minutes ; adulte a 30 minutes de fonctionnement actif. Pas d'age cumule pendant les deep sleeps. On pourra ralentir cette croissance une fois le jeu valide.

## Batterie

Gestion V5.1 preservee, a tester sur la carte : batterie vide rouge 2 sec lors d'un bouton en hibernation, sauvegarde puis deep sleep, reessai periodique, extinction ecran inactif. La detection USB est indicative, elle ne detecte pas une charge terminee.

## Flash Download Tools

- [Firmware V6 complet](firmware/Tamagotchi_V6_TDisplay_V11_full.bin) a **0x0000**.
- [Application seule V6](firmware/Tamagotchi_V6_TDisplay_V11_app.bin) a **0x10000** pour mettre a jour V4/V5/V5.1 **sans ERASE** et conserver l'animal.

Compilation : PlatformIO, `platformio.ini`, ESP32 classique, 4 Mo, DIO, 40 MHz, 115200.

**ESP-NOW non inclus** : les rencontres sans fil entre deux Tamagotchi seront ajoutees apres validation de la V6 sur deux cartes.
