# Mira Panel — widget Thermostat natif (chantier de développement)

## Objectif

Le widget `thermostat` est **un seul composant LVGL** associant les trois
fonctions Jeedom : consigne (écriture), température actuelle (lecture) et
mode (sélection). Il peut être placé sur **Accueil**, **Lumières** ou
**Températures** ; sa présentation n'est jamais déduite de la combinaison
de trois widgets indépendants.

- **Waveshare 800 × 480** : cadran à gauche, température actuelle puis mode
  à droite, inspiré de la maquette utilisateur.
- **WT32 320 × 480** : cadran en haut, température actuelle puis mode
  en dessous, comme la configuration matérielle déjà utilisée.
- Le widget réutilise `lv_arc`, `lv_label`, `lv_dropdown` de LVGL 8 ;
  il ne crée pas un moteur graphique supplémentaire.
- Le mode `auto` affiche le thermostat complet. Les dispositions
  libres très petites nécessiteront un compactage ultérieur.
- Ne jamais restructurer le flex des autres widgets ni modifier l'accueil.

## Contrat JSON provisoire

Une seule entrée dans `/miralvgl.json` ; tous les champs supplémentaires
sont optionnels. Les noms techniques restent stables, le nom affiché peut
être personnalisé.

```json
{
  "lvgl": [
    {
      "type": "thermostat",
      "name": "Thermostat salon",
      "displayName": "Salon",
      "emplacement": "temperatures",
      "layout": "auto",
      "min": "5",
      "max": "30",
      "value": "20.5",
      "unit": "°C",
      "modes": "Confort|Éco|Hors gel|Arrêt"
    }
  ]
}
```

### Trois canaux Jeedom (noms techniques générés)

À partir du nom `Thermostat salon`, le firmware génère dans `/getindex`
les commandes `Thermostat salon__temperature` (info),
`Thermostat salon__consigne` (range) et
`Thermostat salon__mode` (liste à options).

Le plugin Jeedom 1.2.19-dev reconnaît cette composition, peut la programmer
et synchronise les trois entrées via les commandes historiques. Les liaisons
vers les commandes **réelles** Jeedom se renseignent ensuite dans la fiche
équipement Mira par les champs de calcul/mapping existants.

### Trois canaux Jeedom

| Rôle | Type Jeedom | Mise à jour / action |
| --- | --- | --- |
| Température réelle | `info/numeric` | Retour Jeedom/HTTP ; **ne déclenche jamais d'action** |
| Consigne | `info/numeric` + `action/slider` | Arc LVGL au pas de 0,5 °C, valeur flottante émise sur interaction |
| Mode | `info/string` + commandes mode | Liste LVGL de modes déclarés explicitement |

Côté serveur Mira, la table historique `IndexAddInfo / IndexAddRange /
IndexAddList` est préservée pour la compatibilité HTTP Jeedom. La
future configuration du plugin doit associer les **trois fonctions**
distinctement, sans exécuter les commandes physiques pendant
l'initialisation, la restauration d'état ou la resynchronisation.

L'API HTTP Jeedom reste **le canal principal**. MQTT reste facultatif,
comme sur tous les autres widgets.

## Règles de validation

1. L'utilisateur peut choisir n'importe quelle page pour ce seul widget.
2. Tester le rendu horizontal Waveshare et le vertical WT32 sur matériel.
3. Tester le pas de consigne **0,5 °C** et sa remontée Jeedom.
4. Tester la liste Mode et la mise à jour de mode depuis Jeedom.
5. Confirmer qu'un **retour d'état ne provoque jamais d'action physique**.
6. La 0.6.52 reste publiée inchangée jusqu'aux tests réels.
7. La PR #8 (ancien agencement de trois widgets) **ne doit pas être fusionnée**.

## Sources du plugin Jeedom

Le dépôt GitHub connecté contient uniquement le **firmware**. Les sources PHP/JS du plugin Jeedom **1.2.18** ont été fournies séparément.
Une préversion locale **1.2.19-dev** est préparée avec type Thermostat,
configuration des modes, sauvegarde/relecture des données et contrôle de
synchronisation des trois actions. Elle doit être installée conjointement au
firmware 0.6.55 preview, puis testée sur Jeedom réel. Le ZIP du plugin est
livré comme artefact séparé et n'est pas encore publié dans le dépôt GitHub.

Ce document est le point de départ de l'intégration, et **non** une
confirmation de publication de la fonctionnalité.
