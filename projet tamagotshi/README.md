# Projet Tamagotshi - V5 : batterie et hibernation 🔋🐣

Firmware pour **LilyGO T-Display V1.1 original** (ESP32 classique, 135x240 portrait). Garde toutes les fonctions et mini-jeux de la V4.

## Nouveautés V5

- **Mesure batterie intégrée** : GPIO14 active brièvement le diviseur de tension, puis GPIO34 lit la tension LiPo, sans composants supplémentaires.
- **Petite icône batterie** discrète au-dessus du menu, et tension en volts et pourcentage estimé sur INFOS.
- **Extinction de l'écran après 2 minutes sans bouton** (ce n'est pas un deep sleep) : le Tama continue à vivre, évoluer et se sauvegarder. Un appui sur un bouton réveille l'écran sans déclencher d'action.
- **Batterie faible** : si la tension reste sous **3,48 V** pendant trois contrôles espacés de 30 secondes, l'état est sauvegardé dans NVS, le rétroéclairage est éteint et l'ESP32 passe en **deep sleep**.
- **Réveil automatique toutes les 5 minutes** pour mesurer la batterie : retour à la vie normale uniquement si la tension est remontée à **3,70 V**. Le GPIO35 peut aussi provoquer un réveil, mais si la batterie est encore faible, le Tama se rendort.
- **Sécurité** : mesure hors de la plage 2,70–4,40 V traitée comme indisponible (aucune décision de sommeil), pour éviter de dormir par erreur en USB sans batterie.
- **Progression conservée** grâce à Preferences/NVS, compatible V2/V3/V4. Le deep sleep redémarre le processeur au réveil mais ne remet pas le Tama à zéro.

**Attention** : une batterie LiPo 1S n'est pas un compteur de coulombs ; le pourcentage est une estimation par la tension. Les mesures et les seuils demandent un test sur la carte réelle. Le T-Display lui-même consomme en deep sleep davantage que l'ESP32 seul. Le Tama ne progresse pas pendant un vrai deep sleep (ou une coupure) ; aucun RTC externe n'est présent. Le réveil sur raccordement USB n'est pas instantané : la vérification est périodique (5 min). Le circuit de protection de la batterie doit rester opérationnel ; le firmware ne le remplace pas.

## Utilisation

Les deux boutons intégrés conservent le fonctionnement de la V4 :
- GPIO35 : choix suivant, navigation et réveil
- GPIO0 (BOOT) : valider et réveil

Les jauges du Tama sont dans INFOS ; la page suivante est SOUVENIRS.

## Flash Download Tools

- [Tamagotchi_V5_TDisplay_V11_full.bin](firmware/Tamagotchi_V5_TDisplay_V11_full.bin) : **0x0000**, installation complète.
- [Tamagotchi_V5_TDisplay_V11_app.bin](firmware/Tamagotchi_V5_TDisplay_V11_app.bin) : **0x10000**, mise à jour depuis V4 **sans ERASE**, conservation NVS avec la même table de partitions.

Configuration : ESP32 / UART / 40MHz / DIO / 32 Mbit (4 Mo) / 115200 bauds.

Compilation par [GitHub Actions](../../actions/workflows/tamagotchi-build.yml), fichiers rangés dans le dossier firmware sans toucher aux fichiers de Mira.
