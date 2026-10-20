# Mira Panel Jeedom 1.2.24-dev — designer / saisie précise (préversion)

**Patch du plugin uniquement** : aucun changement firmware, plugin Jeedom existant conservé.

- Saisie X / Y / largeur / hauteur sans recalcul lors de chaque caractère.
- Application avec **Entrée**, **Appliquer**, ou à la sortie du groupe de champs.
- Les valeurs entrées au clavier ignorent l'aimantation de la grille.
- Si un rectangle est hors des limites physiques, il est **refusé avec un message** plutôt que de modifier silencieusement X ou la largeur.
- Aperçu des tuiles libres corrigé pour tenir compte du padding LVGL du parent (6 px à gauche, 8 px en haut), et de l'arrondi entier réellement effectué par le firmware.
- Navigation fixe visualisée sous l'aperçu, et signalement des anciennes tuiles coupées par les limites du panneau.
- Sauvegardes/programmes déjà enregistrés non déplacés automatiquement.

**Test conseillé** : avant tout envoi à l'écran, vérifier le bord droit de « Réseau » dans le designer. Modifier sa largeur si besoin, appliquer, puis enregistrer sur l'écran seulement après vérification des positions. Tester la modification clavier d'un nombre à trois chiffres avec la grille active.

Après installation : Ctrl+F5 pour purger l'ancien JavaScript du navigateur. Installer par-dessus 1.2.23 sans désinstaller le plugin (conservation des équipements).

---

# Mira Panel Jeedom 1.2.23-dev — correctif prudent de chargement

**Préversion expérimentale.** Rétablit exactement `desktop/php/mira.php`
provenant de la 1.2.21, notamment `include_file('desktop', 'mira', 'js', 'mira')`,
afin de respecter le chargement JavaScript standard de Jeedom et supprimer
la régression potentiellement bloquante de la 1.2.22.

Le gestionnaire des favoris (issu de la 1.2.22) reste sans popup : il retire
l'ancien écouteur délégué 1.2.20 et ignore les événements synthétiques
lorsque Jeedom hydrate une fiche équipement. Aucun changement des commandes,
de la communication ESP ni des données des équipements.

**Important :** le bon fonctionnement dans Jeedom doit être confirmé sur
l'installation réelle ; les contrôles locaux de syntaxe ne le garantissent
pas. En cas de blocage persistant, réinstaller la 1.2.21.

Après l'installation : recharger Jeedom via Ctrl+F5 ; ne pas vider les données
ou désinstaller le plugin.

---

# Mira Panel Jeedom 1.2.22-dev — favoris et cache JavaScript

**Préversion (à tester avec les firmwares 0.7.2, 0.7.3 ou 0.7.4).**

Correctifs :
- Force le chargement du JavaScript 1.2.22 via une URL versionnée dans la fiche équipement, pour éviter le script 1.2.20 conservé en cache.
- Retire l'ancien gestionnaire d'événements des favoris lorsqu'une fiche Jeedom est réaffichée sans rechargement complet de la page.
- N'accepte le contrôle des doublons que lors d'un changement réel (événement navigateur `isTrusted`). Aucune alerte bloquante ; indication sous les listes.
- En cas de doublon volontaire, restaure si possible la valeur précédente au lieu d'écraser le favori.
- Aucune modification des commandes, widgets, valeurs et méthodes de synchronisation des écrans.

Après installation, fermer/réouvrir la fiche équipement. Si le navigateur avait déjà gardé Mira ouvert, effectuer **Ctrl+F5 une fois** pour repartir sans anciens gestionnaires d'événements.

---

# Mira Panel Jeedom 1.2.21-dev — navigation : correctif des favoris

Le chargement des réglages d’un équipement ne déclenche plus le message intempestif « Cette page est déjà dans les favoris ». Le contrôle de doublons n'agit que lors d'un changement réel du sélecteur par l'utilisateur, avec un message non bloquant à côté des favoris. Les paramètres existants sont préservés ; aucun widget ni commande Jeedom n'est modifié. Compatible avec les firmwares 0.7.2 et 0.7.3.

---

# Mira Panel Jeedom 1.2.20-dev — favoris gérés depuis Jeedom

**À utiliser avec le firmware Mira 0.7.2 de test.** Dans chaque équipement Jeedom Mira Panel, la section **Navigation — favoris** permet de choisir trois pages parmi Accueil, Lumières, Températures, Énergie, Paramètres ou Aucun. Un équipement peut avoir un jeu de favoris différent d'un autre. Sauvegarder l'équipement transmet les raccourcis via le POST authentifié `/navfavorites` **sans reprogrammer les widgets ni redémarrer l'ESP**. Si l'écran était indisponible, cliquer sur **Réenvoyer les favoris** après sa reconnexion. Les choix restent stockés côté ESP jusqu'à la prochaine synchronisation. Les anciennes actions Jeedom et le thermostat sont inchangés.

---

# Mira Panel Jeedom 1.2.19-dev — widget Thermostat (préversion)

**À installer uniquement avec le firmware Mira 0.6.55 preview (non publié en production)**. Le thermostat est un widget unique composé de trois fonctions sous-jacentes : température (info), consigne (curseur au pas de 0,5 °C) et mode (liste).

**Configuration :** Programmer un écran ESP32 → Thermostat → nom, page, consigne mini/maxi, consigne initiale et modes. Le nom technique reste stable ; la modification des options est disponible depuis le tableau. L'écran crée trois commandes internes Jeedom avec suffixes `__temperature`, `__consigne`, `__mode`. Associer dans la fiche équipement Mira la **valeur calculée** de la température et de la consigne aux informations réelles Jeedom, la commande de réglage à l'action curseur correspondante, et les modes aux actions Jeedom voulues. Les modes ne sont pas limités à Confort/Éco ; leur liste est entièrement configurable.

Le widget conserve le transport HTTP principal et les protections **1.2.18** : aucune commande physique déclenchée par l'initialisation et les simples retours d'état. La synchronisation attend les trois entrées du widget pour ne pas valider un ancien index.

**Précaution** : sauvegarder le plugin et la programmation écran précédents. Versions de développement, à tester avant publication.

---

# Mira Panel Jeedom 1.2.18 — sécurité au redémarrage

**Correctif de sécurité prioritaire :** les états locaux du firmware peuvent
retomber sur OFF (ou sur les valeurs par défaut) après un reset. Les anciennes
méthodes `Init_mira_Module()` et `refresh()` pouvaient alors exécuter des
**actions physiques** Jeedom (`On`, `Off`, curseur, liste, couleur) à partir de
ces valeurs obsolètes. Cette version supprime ces exécutions et empêche aussi
que le rafraîchissement périodique écrase les états Jeedom avec ceux de Mira.

L'initialisation conserve le mapping des commandes et des IDs Jeedom. La
fonction `refresh()` devient une vérification de connectivité en lecture seule.
Les paramètres, le designer et l'envoi de programmation 1.2.17 sont conservés.

**Limite volontaire :** ce correctif empêche les commandes involontaires mais
ne rétablit pas encore automatiquement tous les états affichés sur l'écran
après reboot. Cette resynchronisation *Jeedom vers Mira* devra être ajoutée
indépendamment et testée. Ne pas utiliser une valeur d'initialisation provenant
de l'écran pour actionner physiquement un équipement.

---

# Mira Panel Jeedom 1.2.17 — relecture après programmation

Lorsque le WT32 sauvegarde la configuration mais coupe sa connexion HTTP avant d'envoyer son accusé de réception, Jeedom attend 12 s puis tente **une lecture seule** de `/getlvgl` pendant 17 s maximum. Tous les widgets et leurs paramètres envoyés sont comparés à la configuration stockée ; un retour positif requiert une correspondance complète (y compris `displayName`, Mini/Maxi, disposition et options d’icône).

Le navigateur affiche une réussite **uniquement** après un `HTTP 200 OK` de `/programui` ou la confirmation de la configuration stockée sur le matériel. Les refus explicites et les différences de configuration restent des erreurs. Aucune seconde programmation n'est lancée, pour éviter les doublons et les effets de bord.

Les versions du firmware (WT32/Waveshare 0.6.50), l'interface simplifiée et le designer restent inchangés.

---

# Mira Panel Jeedom 1.2.14

Correctif sauvegarde Jeedom avec écran Mira neuf/non programmé : un `/getindex` contenant `actions: []` est désormais considéré comme valide et ne coupe plus la réponse AJAX.

# Mira Panel Jeedom 1.2.13

Cette révision rend la programmation indépendante du modèle d'écran. Le plugin s'appuie sur `/displayinfo` pour adapter automatiquement le designer au profil réel (WT32, Waveshare 800×480, futurs profils Mira).

# Mira 1.1.0 - autosync apres reboot + mapping dynamique des listes

Cette version repart du patch de mapping generique valide : aucune option Confort/Eco/etc. n'est codee en dur.

Correction autosync :
- `/savelistlvgl` repond OK puis redemarre l'ESP32 environ 500 ms plus tard.
- les anciens patchs appelaient `postUpdate()` immediatement et pouvaient donc relire l'ancien `/getindex`.
- Jeedom attend maintenant le redemarrage, poll `/getindex` et verifie que les widgets/options qui viennent d'etre programmes sont presents.
- `postUpdate()` n'est lance qu'apres validation du nouvel index.
- aucun rafraichissement force de l'interface Jeedom et aucun changement d'onglet.
- le mapping dynamique des menus deroulants est conserve.

Des traces `[AutoSync]` sont ajoutees au log `mira` pour diagnostiquer facilement un timeout.

## Style Segmenté

Avec le firmware Mira Panel 0.5.4, le type « Liste vers boutons » propose le style Segmenté pour afficher plusieurs choix dans un contrôle tactile compact.


## 1.2.2 - suppression synchronisée des commandes

Lorsqu'un widget est retiré puis que la programmation est envoyée à l'écran, Mira supprime maintenant automatiquement dans Jeedom les commandes qui appartenaient à ce widget. Les commandes système du plugin sont protégées.

## 1.2.4 - édition directe des bornes

Les widgets Curseur et Curseur arrondi peuvent modifier Mini/Maxi directement dans le tableau de programmation. La colonne Style est élargie pour afficher les variantes plus confortablement.


## Version 1.2.16 (préversion à tester)

L'ajout de widgets privilégie la lisibilité : nom affiché, mode Nom / Icône / Nom + icône et disposition Auto ou Manuel via le designer. Le tableau masque les paramètres techniques conservés dans le JSON ; ils restent éditables via « Modifier ». Les anciennes configurations de styles et de dispositions sont relues sans conversion forcée.

**Compatibilité firmware** : les firmwares 0.6.49 ne prennent pas encore en charge le nouveau champ optionnel `displayName` ; pour deux widgets portant le même libellé, Mira Jeedom envoie un nom technique distinct pour préserver les commandes et la synchronisation, mais l'écran peut afficher ce nom technique jusqu'à une évolution du firmware. Ne pas considérer l'affichage de libellés identiques sur l'écran comme validé avec 0.6.49.

## Diagnostic de programmation 1.2.16

La commande `PostHTTPJson` écrit dans le log `mira` l'IP cible, la taille du JSON et le code HTTP retourné (jamais le mot de passe ni le JSON). Le formulaire distingue dorénavant une réponse HTTP de l'écran d'une réponse AJAX Jeedom inattendue.

La programmation n'est annoncée comme réussie que si l'écran renvoie HTTP 200 et le texte `OK`. Ceci est un correctif de diagnostic, pas encore la correction attestée d'une erreur côté écran.

## Essai de communication /programui : délai 30 secondes

Le délai cURL Jeedom vers l’écran passe de 12 s à 30 s, et le délai AJAX navigateur vers Jeedom de 15 s à 40 s. Cette préversion vise seulement à confirmer si le module répond avec plus de temps : elle ne corrige pas automatiquement un blocage du serveur ESP32 et ne modifie pas le firmware.

**Important** : sur le firmware 0.6.49, `displayName` est ignoré et le simple changement de « Lumi » en « Luminosité » dans « Nom affiché » ne peut pas encore être répercuté sur l’écran. Ne pas renommer les commandes techniques en contournement sans évaluer la synchronisation Jeedom.


## Version 1.2.25 (préversion)

- Supprime le texte parasite « Barre de navigation · fixe » du designer.
- La saisie manuelle et le déplacement réservent 6 px de marge à droite et à gauche.
- Le firmware reste inchangé ; les positions déjà programmées ne sont pas modifiées automatiquement.


## Version 1.2.26 (préversion thermostat / mode)

- La synchronisation de l'état des listes et modes thermostat depuis Jeedom accepte désormais les libellés (`Confort`, `Éco`, `Hors gel`, `Arrêt`) via `/updatenr?listvalue=`, endpoint existant qui résout un mode en identifiant Mira.
- Les valeurs déjà numériques (identifiants Mira) continuent à utiliser `/updatenr?list=`.
- Accepte aussi les variantes de casse, accents et espaces des libellés grâce au mapping des options existantes.
- Les URL sont encodées correctement pour les libellés accentués et les espaces. Aucun ordre chauffage n'est exécuté : mise à jour d'affichage uniquement.
- Les corrections de design 1.2.25 sont conservées. Firmware WT32/Waveshare 0.7.13 inchangé.
