# Widgets Info : nom, icône et taille responsive (Mira 0.6.43 preview)

Cette fonctionnalité est **en développement** et n'est pas encore déployée sur `main`.

## Configuration par widget

Les paramètres facultatifs de présentation sont transportés et mémorisés avec le JSON LVGL existant, sans changer les identifiants de commande Jeedom. Les deux routes de programmation de l'écran acceptent les champs HTTP `labelMode` et `icon` en plus des paramètres habituels.

Exemple d'objet Info mémorisé :

```json
{
  "type": "info",
  "name": "Température",
  "unit": "°C",
  "style": "standard",
  "layout": "auto",
  "labelMode": "both",
  "icon": "temperature"
}
```

- `labelMode=auto` : sur Waveshare, inférer un pictogramme pertinent ; sur WT32, préserver la présentation 0.6.39/0.6.42 d'origine par défaut. Si aucune icône pertinente n'est identifiée, conserver le texte.
- `labelMode=text` : nom + valeur, sans icône (ancien comportement).
- `labelMode=icon` : **icône à gauche de la valeur**, sans titre ; raccourcissement de la carte lorsque la taille automatique le permet.
- `labelMode=both` : **icône à gauche de la valeur**, nom au-dessus ; horodatage de la dernière réception conservé s'il existe.

Icônes reconnues : `auto`, `temperature`, `humidity`, `pressure`, `light`, `energy`, `plug`, `door`. Une icône inconnue laisse le nom visible.

Les formes sont tracées avec les primitives natives LVGL et changent de taille selon **la largeur ET la hauteur finales de la tuile**, y compris avec les positions `layout=free`. Les icônes ne sont pas des emojis ni des bitmaps agrandis. Ces réglages sont indépendants du format d'écran ; le même objet JSON reste utilisable sur WT32 et Waveshare.

## Horloge de veille

Le WT32 conserve son horloge de veille et sa police Montserrat 48. Sur Waveshare 800×480, la veille utilise désormais une **police Montserrat native 120 px**, dont seuls les 12 caractères nécessaires (`0-9`, `:`, `-`) sont intégrés. Les glyphes sont tracés à leur résolution finale, sans canvas zoomé ni flou de redimensionnement. La génération est automatisée et reproductible dans GitHub Actions depuis Montserrat (SIL OFL 1.1, source Google Fonts épinglée). Le déplacement anti-marquage reste actif. Le rendu final doit encore être validé sur matériel.

## Compatibilité et tests

La 0.6.43 doit être compilée pour les deux profils avant tout test. Le rendu et le choix manuel des modes doivent ensuite être vérifiés physiquement sur les deux écrans. Le travail est actuellement contenu dans la PR #3 et ne change pas les binaires publiés.

**Interface Jeedom :** les champs sont disponibles dans l'API de programmation Mira, mais leur exposition comme listes déroulantes dans le plugin Jeedom reste un travail d'intégration séparé.
