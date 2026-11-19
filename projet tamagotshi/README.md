# Tamagotchi V8.0 — 18 creatures, 3 familles, embranchements A/B

## Projet isole de Mira Panel

Le code du Tamagotchi se trouve dans `projet tamagotshi/` sur **la branche dediee `projet-tamagotshi`** du depot Arokh76/Mira-panel. Les sources et firmwares de Mira Panel restent inchanges ; l'action de compilation GitHub ici est specifique au Tamagotchi.

## Les evolutions (style de face)

| Famille | Bebe | Jeune | Ado A -> Adulte A | Ado B -> Adulte B |
|---|---|---|---|---|
| Terre | Poussin | Feuillu | Bourgeon -> Sylvan | Mousse -> Floralis |
| Eau | Gouti | Nageo | Aquilon -> Mareon | Rivage -> Lagrin |
| Ciel | Plumi | Aelo | Alize -> Zephyr | Eole -> Aerys |

**Oeuf commun** creme rose tachete : il tremble, se fissure, puis eclot. La famille est choisie **au hasard a l'eclosion avec une chance de 1/3** pour chaque milieu. L'adolescent prend ensuite la voie **A** (plutot activites/mini-jeux) ou **B** (plutot repas, hygiene, calins), avec tirage aleatoire si les scores sont proches. La branche est ensuite definitive et sauvegardee.

Temps de test: oeuf 0-1 min, bebe 1-7 min, jeune 7-15 min, adolescent 15-30 min et adulte a partir de 30 min **de temps actif**. Les 18 silhouettes sont des dessins proceduraux de face en RGB565 concus pour le TFT 135x240 ; les courbes et couleurs seront affinees selon le resultat sur le materiel reel. Les differences d'accessoires suivent la planche validee : feuilles/fleurs, nageoires bleues puis orange selon la voie, ailes et petits motifs lunaires.

## Compatibilite

Les sauvegardes des V7 sont lues sans effacer les statistiques, la personnalite, les objets et l'age. Pour un animal existant, une famille est attribuee par migration, avec une correspondance de couleur de l'ancienne forme adulte si possible ; **les nouvelles naissances** utilisent le tirage aleatoire. Les nouvelles cles NVS `family`, `branch`, `evo_v8` sont ajoutees au namespace existant `tama_v2`.

## Autres fonctions conservees

Menus, maison (jardin et chambre), decor, promenade et nuages en parallaxe, repas, bain avec pommeau, calins, cadeaux, 2 mini-jeux, sac, charge USB animee et securites batterie. Le SOIN utilise maintenant une aura et des croix de guerison, sans pansement kitsch. Le PROFIL affiche le nom de la creature, la famille, le stade et la voie.

## Flash

- [Tamagotchi_V8_0_TDisplay_V11_app.bin](firmware/Tamagotchi_V8_0_TDisplay_V11_app.bin) : **0x10000, sans ERASE**, conserve les sauvegardes.
- [Tamagotchi_V8_0_TDisplay_V11_full.bin](firmware/Tamagotchi_V8_0_TDisplay_V11_full.bin) : **0x0000**, reinstallation complete, risque de perdre le Tama.

Materiel : LilyGO / TTGO **T-Display V1.1 ORIGINAL** (ESP32 classique, ST7789 135 x 240), DIO, 40 MHz, flash 4 Mo. ESP-NOW reporte.
