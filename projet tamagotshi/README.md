# Projet Tamagotshi V5.1 - Hibernation et affichage batterie

Pour LilyGO T-Display V1.1 original, ecran portrait 135x240, batterie LiPo 1S. Conserve tous les jeux et fonctionnalites de V4/V5.

## Etat batterie faible
- Sous 3,48 V, au moins 3 mesures a 30 secondes d'intervalle, une **grosse batterie vide rouge** apparait **2 secondes**, puis l'ecran s'eteint et l'ESP32 passe en **deep sleep**.
- L'etat (age, besoins, amitie, parties, etc.) est sauvegarde en NVS avant l'hibernation et conserve au reveil.
- **Appui GPIO35 pendant l'hibernation** : la carte redemarre, mesure la tension et, si elle est toujours trop basse, affiche encore le pictogramme **2 secondes**, puis repart en deep sleep. Le bouton ne lance aucun jeu tant que la batterie est faible.
- Controle silencieux automatique toutes les 5 minutes ; reprise quand la tension mesuree est >= 3,70 V.
- Pas de consommation reduite garantie au niveau d'un appareil commercial : les essais materiels sont necessaires.

## Charge / USB
- Le T-Display utilise un **TP4054** dont la sortie **CHRG** commande la **LED bleue de charge**, mais cette sortie n'est pas reliee d'origine a un GPIO accessible par le firmware. Il est donc **impossible de confirmer charge en cours / charge terminee sans modification materielle**.
- La lecture GPIO34 est fiable sur batterie mais peut lire la tension d'alimentation USB pendant la recharge. Quand elle depasse **4,40 V**, le firmware suppose **USB probable** ; il affiche une grosse batterie animee quand le Tama n'est pas utilise pendant 2 minutes. Le message est volontairement **« ALIMENTATION USB »**, pas « batterie pleine ».
- La detection USB est une **heuristique**, particulierement peu fiable en debut de charge (la mesure peut rester vers 4,20 V) : elle doit etre verifiee sur votre carte.
- En l'absence de detection USB, l'ecran s'eteint apres 2 minutes pour economiser la batterie. Un premier appui le rallume sans executer d'action.
- La ligne **GPIO14 reste HIGH pendant le fonctionnement sur batterie**, car elle fait partie du circuit de mise sous tension. Elle ne passe LOW qu'au moment de la veille profonde.

## Firmware
- [**Tamagotchi_V5_1_TDisplay_V11_full.bin**](firmware/Tamagotchi_V5_1_TDisplay_V11_full.bin) : image complete pour Flash Download Tools a **0x0000**.
- [**Tamagotchi_V5_1_TDisplay_V11_app.bin**](firmware/Tamagotchi_V5_1_TDisplay_V11_app.bin) : application seule a **0x10000** depuis V4/V5, sans ERASE pour conserver la NVS.
- Reglages ESP32 / UART / 40 MHz / DIO / 32 Mbit / 115200.
- L'age n'avance pas pendant un vrai deep sleep ni pendant une coupure d'alimentation.

**Ce comportement n'est valide qu'a la compilation et doit etre teste sur la carte reelle avant de compter dessus pour proteger la LiPo.** La protection propre a la batterie reste necessaire.
