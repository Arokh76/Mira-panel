## V0.3.6 – corrections visuelles directes

- Pas d'ajout artificiel du champ Mode : il vient du menu deroulant programme dans Mira.
- `info` n'utilise plus le flex horizontal automatique : largeur fixe pour le libelle et la valeur afin de supprimer les coupures autour des accents et de stabiliser l'alignement.
- Titre Parametres force sur une ligne.
- Pages principales legerement resserrees verticalement.
- MQTT runtime toujours desactive, code conserve.

# Notes V0.3.4 – comparaison directe captures officielles / écran reconstruit

Les captures officielles fournies le 30/09/2026 servent de reference visuelle directe pour cette passe. Les points retenus sont : navigation a trois destinations dont l'ordre depend de la page, infos d'accueil plus compactes, cadran rond sans grosse pastille blanche, affichage central de la consigne sous forme `19°`, bloc `Mode` centre sous la temperature quand il existe, et accents francais correctement rendus.

La veille suit la consigne utilisateur issue des echanges perdus : date retiree, heure nettement agrandie, leger decalage vers le haut et anti-burn par petits deplacements.

MQTT reste present dans la source mais son runtime est force a OFF pour cette phase. La page Parametres et sa geometrie restent celles deja corrigees/validees.

---

# Notes V0.3.3 – veille officielle / anti-burn

Les captures officielles fournies le 30/09/2026 confirment une veille tres epuree : heure seule et nettement plus grande. La V0.3.3 supprime donc la date de la veille, passe l'heure en Montserrat 48 et ajoute un leger deplacement periodique anti-burn. La page Parametres de la V0.3.2 n'est pas modifiee.

---

# Notes V0.3.2 – rendu vidéo

La séquence vidéo autour de 4:20 confirme plusieurs choix visuels : interface très plate, absence de bandeau supérieur, barre noire de navigation, trois destinations visibles et page courante masquée, gros cadran de consigne, horloge d’accueil dominante.

Le MQTT natif est conservé temporairement dans cette branche pour éviter une double modification fonctionnelle + graphique. L’information utilisateur la plus récente indique que l’installation originale utilisait jMQTT côté Jeedom.

---

# Notes de reconstruction Mira V0.3

## Elements confirmes par les sources fournies

- Le dernier firmware embarque une pile MQTT : logs de connexion/reconnexion, reception et interpretation de messages.
- Le prefixe `mira/` et le client ID `mira-client-` sont visibles dans le binaire.
- Les captures de l'interface Web originale donnent explicitement :

```text
Actions MQTT: mira/<nom_ecran>/Actions/#topic#
Etats MQTT:   mira/<nom_ecran>/States/#topic#
```

- La page originale liste les widgets par nom et type.

## Choix V0.3 encore inferes

Le payload exact du firmware original n'a pas encore ete demontre. La V0.3 garde donc une convention volontairement simple : valeur brute dans le payload et nom du widget comme `#topic#`.

La reception est plus permissive que l'emission pour faciliter les essais : synonymes booleens, index/libelle de liste, couleur hex/RGB, JSON avec cle `value`.

## Regle anti-boucle

Un message `States/...` :

1. met a jour le JSON actions de MiraPanelServer via `IndexSetInfo/Toggle/Range/List/Color`,
2. met a jour le widget LVGL,
3. **ne fait jamais** `IndexSetEvent()`.

Ainsi un etat externe ne devient pas une commande et un reboot ne publie aucun OFF/ON par defaut.

## HTTP + MQTT

La V0.3 conserve l'HTTP Mira complet pour la compatibilite actuelle avec Jeedom. MQTT est ajoute en parallele pour retrouver progressivement l'architecture du firmware original et tester les retours temps reel.
