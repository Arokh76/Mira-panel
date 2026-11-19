# Projet Tamagotshi V6.3 — alerte batterie discrete

Pour LilyGO / TTGO T-Display V1.1 ORIGINAL (ESP32 ST7789 135 × 240, portrait).

## Changement V6.3

- **Aucun indicateur de batterie sur l'ecran principal** en utilisation normale. L'icone verte et le pourcentage ont ete supprimes de l'accueil ; le petit texte « USB » sur l'accueil aussi.
- **Batterie faible, 10 % ou moins (estimation par tension)** : une seule **icone de batterie vide rouge** apparait en bas, juste au-dessus du menu. Pas de pourcentage sur l'accueil.
- **INFOS reste inchange** : pourcentage estime, tension (V) et indication USB probable si detectee.
- **Gestion basse tension inchangee** : trois releves sous 3,48 V puis grande batterie rouge 2 secondes et deep sleep, controles periodiques, appui bouton en hibernation.
- Le seuil 10 % est un choix volontaire : une alerte a 5 % laisserait trop peu de marge avant l'hibernation. Le pourcentage est approximatif car calcule par la tension, et la mesure sur USB ne garantit pas l'etat de charge.

## Tout le reste conserve

Paysage dynamique V6.1, Tama plus petit pendant ses promenades, contrastes, Z animes et bulle de ronflement V6.2, quatre personnalites et quatre evolutions adultes, inventaire, mini-jeux, animation d'alimentation USB au repos, sauvegardes NVS.

## Flash Download Tools

- [Tamagotchi_V6_3_TDisplay_V11_app.bin](firmware/Tamagotchi_V6_3_TDisplay_V11_app.bin) a **0x10000** sans ERASE pour mettre a jour depuis V6/V6.1/V6.2 en gardant le Tama.
- [Tamagotchi_V6_3_TDisplay_V11_full.bin](firmware/Tamagotchi_V6_3_TDisplay_V11_full.bin) a **0x0000** pour reinstallation complete. ATTENTION : cette image peut effacer la sauvegarde.

ESP32 classique / DIO / 40MHz / 4MB / 115200. Compilation via GitHub Actions. Le comportement batterie doit etre confirme sur la vraie carte.
