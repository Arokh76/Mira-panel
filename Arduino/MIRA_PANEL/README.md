# Mira Panel firmware V0.4.3


Rebranding complet du firmware WT32-SC01 Plus vers **Mira Panel**.

- interface Web : Mira Panel ;
- footer : logo Mira uniquement, sans URL ;
- préfixe MQTT : `mira/<nom_ecran>/Actions/...` et `mira/<nom_ecran>/States/...` ;
- client MQTT : `mira-panel-XXXXXX` ;
- stockage LVGL : `/miralvgl.json` ;
- classes internes renommées `MiraPanelServer`, `MiraPanelUI`, `MiraMqtt`.

# WT32 Mira rebuild V0.3.6 – correction directe du clone Classic

Cette version repart de la V0.3.4 et corrige directement les ecarts encore visibles sur les dernieres photos du WT32, sans ajouter de modernisation.

Modifications V0.3.6 :

- **MQTT reste desactive a l'execution**, mais tout le code et les parametres sont conserves pour une reactivation ulterieure ;
- les lignes `info` utilisent maintenant une geometrie fixe en deux colonnes, sans flex automatique : `Temperature: 22.3 °C`, `Pression: ...`, `Luminosite: ...` restent sur une seule ligne et ne peuvent plus se couper autour d'un accent ;
- les libelles `info` ont une zone de 154 px alignee a droite et les valeurs une zone de 132 px alignee a gauche, ce qui rapproche leur centrage du firmware officiel ;
- la hauteur des lignes d'information passe a 36 px et l'espacement vertical des pages principales est legerement resserre pour retrouver le rendu plus compact de la video ;
- le titre **Parametres** recoit une largeur fixe, centrage explicite et mode `CLIP`, afin d'eviter toute coupure du mot ;
- le menu `Mode` du thermostat reste gere par le widget `list` Mira normal : aucun faux `Mode/Confort` n'est ajoute dans le firmware ; son titre est centre et le dropdown est rendu plus plat, comme sur la capture officielle ;
- les corrections V0.3.4 sont conservees : accents francais, ordre de navigation officiel, cadran sans grosse pastille blanche, veille heure seule + anti-burn, MQTT runtime coupe.

Le but reste **clone Classic d'abord**. Les animations et ameliorations modernes viendront uniquement apres validation de cette base.

---

# WT32 Mira rebuild V0.3.4 – reprise des captures officielles + accents

Cette version repart **du firmware V0.3.2 fourni par l'utilisateur**, puis conserve le correctif V0.3.3 de veille. Elle sert surtout a recuperer les decisions perdues dans le fil de discussion et a rapprocher la disposition des captures officielles.

Modifications V0.3.4 :

- **MQTT runtime temporairement desactive**, tout le code `MiraMqtt`, les parametres Web et la possibilite de le reactiver sont conserves ; une seule constante `MIRA_MQTT_RUNTIME_ENABLED` permet de le remettre en service ;
- correction des **accents francais** (`é`, `è`, `à`, `ç`, `ù`, `ô`, `î`, `ï`, `û`, `ü`, `œ`, majuscules usuelles...) avec trois petites polices LVGL de fallback 12/14/16 px ;
- les polices Montserrat standard restent utilisees pour les caracteres ASCII ;
- accueil : carte date/heure plus proche de l'original, heure en **Montserrat 40**, date centree, infos plus compactes et centrees, ajout visuel du `:` aux libelles de type `info` ;
- navigation : ordre des trois destinations recale sur les captures officielles, avec icones ampoule et thermometre a la place des anciens symboles texte ;
- temperature : cadran legerement reduit pour laisser la place au retour de temperature et a un eventuel `Mode`, fond circulaire sombre, suppression visuelle de la grosse pastille blanche du curseur, valeur centrale affichee `19°` plutot que `19 °C` ;
- listes / mode : titre et menu deroulant centres pour se rapprocher de la capture officielle `Mode / Confort` ;
- veille : **date supprimee**, heure seule en Montserrat 48, positionnee un peu au-dessus du centre comme sur la capture, luminosite visuelle attenuee et deplacement anti-burn de quelques pixels toutes les 60 s ;
- la geometrie de la page **Parametres n'est pas reprise** : les corrections de chevauchement deja validees en V0.3.2 sont conservees.

La ligne de fete/prenom visible sous la date dans une capture officielle (par ex. `St Geoffroy`) n'est pas fabriquee : aucune source equivalente n'est fournie par le JSON Mira actuel.

> Important : V0.3.4 active `LV_FONT_MONTSERRAT_40` et `LV_FONT_MONTSERRAT_48`. Arduino/LVGL cherchant chez toi `lv_conf.h` a cote du dossier `lvgl`, recopier le `lv_conf.h` fourni vers `Documents\Arduino\libraries\lv_conf.h` avant compilation.

> Environnement conserve : ESP32 core **2.0.17**, carte **ESP32S3 Dev Module**, flash **8 MB QIO 80 MHz**, **QSPI PSRAM**, partition **8M with spiffs (3MB APP/1.5MB SPIFFS)**.

---

# WT32 Mira rebuild V0.3.3 – veille rapprochee du firmware officiel

Cette version repart **strictement de la V0.3.2 fournie et validee sur le WT32**.
La page Parametres et ses corrections de chevauchement sont conservees telles quelles.

Modifications V0.3.3 uniquement :

- suppression de la **date** sur l'ecran de veille ;
- heure de veille agrandie avec **Montserrat 48** ;
- heure centree au passage en veille ;
- **anti-burn** : deplacement discret de l'heure toutes les 60 s sur une grille de 9 positions (±12 px horizontalement, ±8 px verticalement) ;
- aucun changement au protocole Mira, aux widgets, a MQTT ni a la page Parametres.

> Important : cette version active `LV_FONT_MONTSERRAT_48`. Si Arduino utilise le `lv_conf.h` place a cote du dossier `lvgl`, recopier le `lv_conf.h` fourni avec cette V0.3.3 vers `Documents\Arduino\libraries\lv_conf.h` avant compilation.

---

# WT32 Mira rebuild V0.3.2 – rendu inspiré de la vidéo

Cette version repart de la **V0.3.1 validée sur l’écran** et ne change pas le chemin Mira/Jeedom. La passe est principalement visuelle, à partir de la séquence de la vidéo autour de **4:20–5:30** :

- suppression conservée du bandeau supérieur ;
- barre noire en bas avec **3 icônes seulement** ;
- l’icône de la page courante est masquée, comme dans la vidéo ;
- pages principales sans cadres/cartes visibles ;
- toggle : libellé à gauche, interrupteur seul à droite ;
- `roundrange` transformé en **grand cadran circulaire** bleu/noir ;
- horloge d’accueil plus grande, date séparée et ligne de séparation ;
- police Montserrat 28 activée pour les gros chiffres ;
- page Paramètres de la V0.3.1 conservée telle quelle.

**MQTT :** le bloc MQTT natif de la V0.3.1 est encore présent pour ne pas mélanger modification fonctionnelle et modification graphique dans la même passe, mais il reste désactivé par défaut. Suite à ta confirmation, on considère maintenant que l’architecture d’origine passait par **jMQTT dans Jeedom**, pas par un MQTT géré directement par Mira.

---

# Correctif V0.3.1

Cette version repart **strictement de la V0.3 MQTT** et conserve `MiraMqtt.cpp/.h`, la page Web MQTT, les topics Actions/States et les retours Mira HTTP.

Modifications UI uniquement :
- suppression du bandeau `Mira · Ecran domotique` ;
- pages de 434 px de haut jusqu'a la barre de navigation ;
- page Params en flex vertical ;
- cartes compactees et libelles raccourcis pour supprimer les chevauchements.

# WT32 Mira rebuild V0.3.1 – MQTT + UI compacte

Cible : **Smart Panlee / Wireless-Tag WT32-SC01 Plus – ZX3D50CE08S-V16-USRC – ESP32-S3 WROVER**.

Cette V0.3 repart de la V0.2 validee fonctionnellement sur l'ecran et ajoute le bloc MQTT retrouve dans le dernier firmware Mira et confirme par les captures de la page Web MQTT.

## Nouveautes V0.3

- Ajout d'un **client MQTT integre au sketch**, sans bibliotheque MQTT Arduino supplementaire a installer.
- Nouvelle page Web **MQTT** dans le menu du serveur Mira.
- Parametres persistants :
  - MQTT Actif
  - MQTT Nom
  - MQTT Serveur
  - MQTT Port
  - MQTT Utilisateur
  - MQTT Mot de passe
- Arborescence reprise de la page du firmware original :

```text
mira/<MQTT_Nom>/Actions/#topic#
mira/<MQTT_Nom>/States/#topic#
```

- Client ID de la forme `mira-panel-XXXXXX`, comme la signature retrouvee dans le binaire original.
- Les widgets programmes dans Mira apparaissent dans la table de la page MQTT avec leur **Nom / topic** et leur **Type**.
- Une interaction tactile continue a fonctionner via Mira/Jeedom HTTP **et** publie en MQTT si MQTT est actif.
- Un message recu dans `States/...` ne declenche **jamais** `IndexSetEvent()` : il met uniquement a jour l'etat interne et l'affichage. Cela evite les boucles et surtout les commandes involontaires au reboot.
- La configuration MQTT est relue a chaud : apres sauvegarde de la page Web, pas besoin de redemarrer le WT32.
- Affichage serie de la taille reelle de Flash et de PSRAM au demarrage.

## Formats MQTT de cette reconstruction

L'arborescence `Actions` / `States` est confirmee par les captures du firmware original. Le payload exact de toutes les commandes du firmware original n'est pas encore confirme ; la V0.3 utilise donc volontairement des formats simples et faciles a tester :

| Widget | Action publiee | State accepte |
|---|---|---|
| button | `1` | - |
| toggle | `0` / `1` | `0`, `1`, `off`, `on`, `false`, `true`, `inactif`, `actif` |
| range / roundrange | nombre | nombre |
| info | - | texte ou nombre |
| list / listbutton | libelle de l'option | libelle ou index numerique |
| color | `#RRGGBB` | `#RRGGBB` ou `R,G,B` |

En reception, `{"value": ...}` est egalement accepte pour faciliter les essais avec jMQTT / Node-RED.

## Test MQTT le plus simple

1. Flasher la V0.3 en gardant la configuration SPIFFS.
2. Ouvrir l'interface Web de l'ecran puis **MQTT**.
3. Renseigner le broker et activer MQTT.
4. Pour un toggle nomme `Cuisine`, ecouter :

```text
mira/panel/Actions/Cuisine
```

5. Toucher le toggle : le payload doit etre `1` ou `0`.
6. Publier manuellement :

```text
topic   : mira/panel/States/Cuisine
payload : 1
```

Le toggle de l'ecran doit passer ON **sans executer une nouvelle action Jeedom**.

Pour une info `Temperature` :

```text
topic   : mira/panel/States/Temperature
payload : 21.7
```

La valeur doit s'afficher directement sur le WT32.

## Reglages Arduino IDE

Utiliser le core **ESP32 by Espressif 2.0.17**.

- Board : `ESP32S3 Dev Module`
- USB CDC On Boot : `Enabled`
- CPU Frequency : `240MHz (WiFi)`
- Flash Mode : `QIO 80MHz`
- Flash Size : `8MB (64Mb)` pour le premier essai, comme la V0.1/V0.2 validee
- Partition Scheme : `8M with spiffs (3MB APP/1.5MB SPIFFS)`
- PSRAM : **QSPI PSRAM**
- Upload Mode : `UART0 / Hardware CDC`
- USB Mode : `Hardware CDC and JTAG`
- **Erase All Flash Before Sketch Upload : Disabled**

La V0.3 affiche ensuite la Flash et la PSRAM reellement detectees dans le moniteur serie. Cela permettra de verifier le point 8/16 Mo sans prendre le risque de flasher directement les binaires originaux.

## Bibliotheques

Conserver exactement l'environnement qui compile deja la V0.1/V0.2 :

- LVGL 8.3.x
- LovyanGFX >= 1.1.9
- ArduinoJson 6.x
- AsyncTCP 1.1.4
- AsyncHTTPRequest_Generic 1.13.0
- ESPAsyncWebServer compatible ESP32 core 2.0.x
- `Cipher.h` / esp32-Encrypt

**Aucune bibliotheque MQTT supplementaire n'est necessaire.**

## Fonctions V0.2 conservees

- reconstruction a chaud apres `savelistlvgl`
- page Params sur le tactile
- luminosite / mode sombre / veille / horloge de veille
- `roundrange` circulaire
- retours d'etat HTTP Mira

## Encore a confirmer / reconstruire

- payload MQTT exact du dernier firmware original pour chaque type de widget
- comportement exact de `MQTT Refresh` vu dans le binaire
- graphismes et icones exacts de la video
- `shieldrelay`, `shieldalarm`, `shieldtoggle`

## Correctif V0.3.6
- correction racine de la police française : `adv_w` converti de 1/16 px vers pixels dans le callback LVGL ;
- supprime les grands espaces après `é/è/à/...` et les morceaux de mots (`Paramè ... tres`, `rature`, `osité`) ;
- lignes d'infos remises en flex centré naturel, sans colonnes fixes/clipping de la V0.3.5 ;
- MQTT reste désactivé à l'exécution comme précédemment, code conservé.


## V0.4.3

- interface Web Système / mise à jour harmonisée avec la page MQTT ;
- libellés Web simplifiés et francisés ;
- traces série normalisées (`[MIRA][...]`) ;
- suppression de traces pouvant exposer des données déchiffrées ;
- nettoyage des traces parasites issues du déchiffrement SPIFFS ;
- aucune modification du protocole MQTT Mira.

## V0.4.2
- MQTT runtime active : connexion uniquement si l'option MQTT est activee dans la page Web.
- Topics : `mira/<nom_ecran>/Actions/...` et `mira/<nom_ecran>/States/...`.
- Client ID : `mira-panel-XXXXXX`.
- Page MQTT renommee avec des libelles plus lisibles.
- Verification des mises a jour sur GitHub `Arokh76/Mira-panel`.
- `firmware/version.json` fournit la version et l'URL du binaire.
- Mise a jour manuelle par fichier `.bin` conservee.
