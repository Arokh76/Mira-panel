# Publication du plugin Mira Panel pour Jeedom

Le dépôt `Arokh76/Mira-panel` reste le dépôt principal du **firmware** sur `main`.

Le plugin Jeedom est publié automatiquement sur la branche indépendante **`beta`**. Cette branche doit contenir directement `plugin_info/`, `core/`, `desktop/` et les autres dossiers du plugin, et non un sous-dossier intermédiaire.

## Publier une préversion (testeurs)

1. Vérifier que l'archive du plugin possède directement à sa racine `plugin_info/info.json`, et que le champ `id` vaut `mira`.
2. Renommer le fichier ZIP en `mira.zip` et le placer dans **`jeedom/source/mira.zip`** sur la branche `main` (depuis GitHub : ouvrir le dossier `jeedom/source`, puis « Add file » / « Upload files »).
3. Enregistrer le changement. GitHub Actions lance le workflow « Publish Mira Jeedom plugin beta ».
4. Attendre sa réussite dans l'onglet **Actions**. La branche `beta` contient alors les fichiers du plugin à la racine.

Le workflow rejette les archives mal structurées ou dont l'identifiant est différent de `mira`. Il ne modifie ni les sources ni les manifestes du firmware.

## Canal stable : publication uniquement après validation

- `beta` : canal de test, alimenté automatiquement par le workflow lors des nouveaux ZIP.
- `stable` : canal validé, **non alimenté automatiquement**. Ne pas utiliser comme source Jeedom avant la première publication d'une version validée.
- Après validation par le testeur, promouvoir explicitement sur `stable` l'arborescence du **commit précis** testé sur `beta`. Ne pas modifier le plugin pendant cette promotion.
- Pour une installation stable Jeedom, garder le même `ID logique : mira`, le dépôt `Arokh76/Mira-panel` et choisir `Branche : stable`.
- Les testeurs qui utilisent `beta` ne basculent pas automatiquement sur `stable`.

## Installation sur Jeedom

Activer la source GitHub dans `Réglages → Système → Configuration → Mises à jour/Market → Github`.

Depuis `Plugins → Gestion des plugins → Ajouter → Sources`, choisir :

| Champ | Valeur |
| --- | --- |
| Type de source | Github |
| ID logique du plugin | `mira` |
| Utilisateur | `Arokh76` |
| Dépôt | `Mira-panel` |
| Branche | `beta` |

Les versions ultérieures sont détectées dans le Centre de mise à jour de Jeedom. Ne pas supprimer le plugin installé pour changer de source : prendre une sauvegarde Jeedom, puis réinstaller par-dessus via la source GitHub en conservant le même ID `mira`.

L'ancienne branche `jeedom-beta` est laissée en place temporairement pour les installations existantes, mais **elle ne recevra plus de nouvelles versions**. Pour basculer dans Jeedom, ajouter à nouveau la source GitHub avec le même ID logique `mira` et renseigner `beta` comme branche, sans supprimer le plugin.

Le contenu publié est une **préversion de test**, distincte des mises à jour OTA du firmware des écrans. Publier seulement les versions validées pour les testeurs.

**Important :** aucune version installable n'existe sur `beta` tant que la première archive `jeedom/source/mira.zip` n'a pas été envoyée et que le workflow n'a pas réussi.
