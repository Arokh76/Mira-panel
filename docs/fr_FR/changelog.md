## 1.2.21 — Correction ouverture équipement

- Les événements `change` synthétiques de Jeedom lors du chargement des sélecteurs de favoris sont ignorés : plus de fenêtre d'alerte ni de modification automatique.
- Lors d'un changement manuel, les doublons affichent une indication discrète plutôt qu'une alerte modale.
- Aucun changement du thermostat, des commandes ni du protocole de synchronisation.

## 1.2.18 — Protection au redémarrage

- La commande init_cmd et le rafraîchissement ne déclenchent plus d’actions physiques à partir des états par défaut de Mira.
- La fonction refresh conserve uniquement la vérification de connectivité.
- Les états affichés doivent encore être resynchronisés en lecture seule depuis Jeedom dans une étape distincte.

## 1.2.17 — Confirmation après écriture

Correction du message de timeout lorsque l'ESP32 a effectivement sauvegardé sa configuration : vérification de `/getlvgl` après une perte d'accusé HTTP, avec comparaison des widgets et de leurs réglages. Les vrais échecs restent signalés. Aucune seconde programmation ni modification des binaires des écrans.

<!-- Préversion 1.2.16 : ajustement d’interface et diagnostic (sans modification du firmware) -->
## 1.2.16 — Préversion simplifiée

- Tableau de programmation simplifié : type, affichage, page, nom affiché et disposition ; paramètres techniques préservés mais masqués dans le tableau.
- Pour les widgets Info : Nom, Icône ou Nom + icône, avec choix d’icône contextuel ; les modes Auto historiques restent compatibles.
- Ajout de noms affichés répétés avec création d'un nom technique unique et champ `displayName` dans le JSON.
- Attention : Mira 0.6.49 ignore encore `displayName`. L'affichage de doublons strictement identiques sur le matériel exige la prise en charge de ce champ par un firmware ultérieur ; la synchronisation Jeedom reste fondée sur le nom technique.
- Disposition courante limitée à Auto ou Manuel via designer ; conservation des anciens modes de placement lors du chargement.
- Avertissement dans le designer lorsqu'une tuile dépasse la première hauteur d'écran ; le défilement effectif dépend du firmware.

## 1.2.15

- Widgets **Info** : nouveaux réglages d’affichage (Auto, nom + valeur, icône + valeur, icône + nom + valeur).
- Proposition automatique d’icône **côté Jeedom** selon le nom et l’unité (température, humidité, pression, luminosité, énergie, prise, porte/fenêtre), avec possibilité de la remplacer manuellement.
- Modification des réglages sur les widgets existants sans nouvelle colonne ni modification du designer.
- Envoi HTTP groupé `/programui` enrichi des champs `labelMode` et `icon` déjà reconnus par Mira 0.6.49 ; chargement de `/getlvgl` compatible avec les écrans plus anciens.
- Aucun changement des commandes Jeedom, de leur synchronisation, de MQTT ou des profils WT32/Waveshare.

## 1.2.14

- Corrige l’erreur Jeedom `Unexpected end of JSON input` lors de la sauvegarde d’un écran encore vide.
- Un `/getindex` valide avec `actions: []` est désormais accepté.
- Suppression du `die()` qui interrompait `eqLogic.ajax.php?action=save`.
- Durcissement du décodage de `/getindex` et des appels d’initialisation.

## 1.2.13

- La programmation n'est plus liée à un modèle d'écran : tout firmware Mira exposant `/testmodule`, `/displayinfo`, `/getlvgl` et `/programui` est accepté.
- Validation et normalisation du profil reçu via `/displayinfo` (résolution, zone utile, navigation et coordonnées logiques).
- Prise en charge explicite des écrans paysage, notamment le Waveshare ESP32-S3-Touch-LCD-4.3C en 800×480.
- Le designer s'élargit jusqu'à 600 px pour les profils paysage tout en conservant le comportement historique du WT32-SC01 Plus.
- Les messages d'erreur de programmation indiquent désormais le profil détecté et les capacités HTTP attendues.

## 1.2.12

- Retour à un aperçu affichant une hauteur d'écran complète.
- Le scroll n'apparaît que si des widgets dépassent le premier écran.
- La zone de travail est limitée à deux hauteurs d'écran maximum.
- Sur WT32-SC01 Plus : 320×434 visibles, 320×868 maximum en page défilante.
- L'auto-scroll pendant déplacement/redimensionnement est conservé.

## 1.2.11

- L’aperçu reste compact tout en conservant le canvas complet 320×434.
- Ajout d’un défilement vertical interne pour atteindre les widgets placés plus bas.
- Ajout de l’auto-scroll pendant le déplacement ou le redimensionnement près du haut/bas de l’aperçu.
- La barre d’actions reste hors du canvas et accessible pendant l’édition.

## 1.2.10

- Designer visuel rendu beaucoup plus compact et recentré.
- Suppression de la fausse barre d’icônes de navigation sous l’aperçu.
- Ajout d’un bouton **Enregistrer sur l'écran** directement dans le designer.
- Le bouton **Vue tableau** ne sert plus qu’à revenir à l’éditeur tabulaire.
- Alignements et dimensions du designer revus pour éviter le grand aperçu 700 px.

## 1.2.9

- Programmation de l’interface en une seule requête HTTP (`/programui`) au lieu d’une requête par widget.
- Chargement de la configuration et du profil écran en parallèle.
- Une page passée en placement libre est figée entièrement pour éviter le mélange Auto/Libre.
- Les nouveaux widgets sont ajoutés après le contenu existant sur une page libre au lieu de revenir en haut.

# Changelog

## 1.2.8
- Ajout du designer visuel Mira : aperçu selon le profil réel de l’écran, déplacement et redimensionnement des widgets, grille magnétique et édition numérique.
- Le designer interroge `/displayinfo` en HTTP et reste désactivé sur les firmwares ne supportant pas le placement libre.
- Les positions libres sont stockées en coordonnées logiques pour rester adaptables à d’autres résolutions.
- Le mode automatique historique reste le comportement par défaut.

# 1.2.7

- Ajout d’une **Disposition** indépendante du style pour les widgets Info.
- Choix : **Auto**, **Tuile**, **Pleine largeur** et **Mise en avant**.
- Le mode Auto conserve le placement intelligent actuel et reste le comportement par défaut des configurations existantes.
- La disposition est transmise au firmware sans modifier les commandes Jeedom, HTTP ou MQTT du widget.

# 1.2.6

- Corrige l’affichage `undefined` lorsque la configuration LVGL reçue de l’écran contient un champ manquant.
- Vérifie les listes et listes vers boutons avant toute programmation.
- Une liste vide bloque désormais l’envoi avant `/removelistlvgl`, afin de ne jamais effacer une configuration valide par erreur.
- Normalisation des champs reçus de l’écran pour éviter de renvoyer littéralement `undefined`.

# 1.2.5

- Ajout du bouton **Installer Mira Panel** dans la page Gestion, entre **Programmer un écran ESP32** et **Configuration**.
- Le bouton ouvre le Web Installer Mira Panel dans un nouvel onglet.
- Aucune modification du bouton d'ajout d'équipement ni de la programmation des widgets.

# 1.2.4
- Programmation écran rendue asynchrone et séquentielle : la page Jeedom ne se fige plus pendant les requêtes HTTP.
- Timeouts réseau ajoutés aux appels vers Mira Panel pour éviter les attentes infinies pendant un reboot ou une perte réseau.
- Initialisation renforcée lorsque des liaisons de commandes Jeedom sont vides ou manquantes.

# 1.2.3
- Colonne Style élargie.
- Mini/Maxi des Curseurs et Curseurs arrondis modifiables directement dans le tableau de programmation.
- La modification active le bouton d'envoi sans devoir supprimer/recréer le widget.
- Validation Mini < Maxi avant envoi.

# 1.2.2

- La synchronisation des widgets devient bidirectionnelle côté commandes Jeedom : quand un widget est supprimé de l’écran, les commandes générées pour ce widget sont supprimées automatiquement de l’équipement Mira.
- Nettoyage sûr : seules les commandes de widgets gérées par Mira sont concernées ; `Rafraichir`, `Init cmd`, `Redémarrer` et `Statut` sont conservées.
- L’autosynchronisation vérifie désormais l’index exact de l’écran, y compris lors d’une suppression pure, afin de ne pas valider par erreur l’ancien index avant le redémarrage.

# 1.2.1

- Ajout du style **Segmenté** pour « Liste vers boutons ».
- Le style garde les mêmes commandes, le même ordre et la même synchronisation Jeedom/MQTT ; seul le rendu écran change.
- Compatible avec les configurations existantes : le style Standard reste le comportement par défaut.

# 1.2.0

- Ajout du choix **Style du widget** dans la programmation.
- Le style est transmis au firmware et reste indépendant de l’ordre des widgets.
- Premiers styles : Info (Standard / Valeur dominante / Compact), Toggle (Standard / Compact), Bouton/Curseur/Listes (Standard / Compact).

# 1.1.1

- Les trois types de commandes Shield restent présents dans le code mais sont masqués temporairement dans la liste d’ajout de commandes.

# Changelog plugin template

>**IMPORTANT**
>
>S'il n'y a pas d'information sur la mise à jour, c'est que celle-ci concerne uniquement de la mise à jour de documentation, de traduction ou de texte.

# 19/01/2022

- Optimisations V4.2

# 20/11/2020

- Présentation officielle V4
- Ajouts d'éléments d'informations et de paramètres pour les commandes

# 16/11/2020

- version minimale Jeedom: 3.3.39 (dernière MAJ critique)

# 04/11/2020

- Nouvelle présentation de la liste des objets

# 07/08/2020

- Ajout de commentaires

# 17/05/2020

- Mise à jour de la documentation
