# Publication du plugin Mira Panel pour Jeedom

Le dépôt `Arokh76/Mira-panel` reste le dépôt principal du **firmware** sur `main`.

Le plugin Jeedom est publié automatiquement sur la branche indépendante **`jeedom-beta`**. Cette branche doit contenir directement `plugin_info/`, `core/`, `desktop/` et les autres dossiers du plugin, et non un sous-dossier intermédiaire.

## Publier une préversion (testeurs)

1. Vérifier que l'archive du plugin possède directement à sa racine `plugin_info/info.json`, et que le champ `id` vaut `mira`.
2. Renommer le fichier ZIP en `mira.zip` et le placer dans **`jeedom/source/mira.zip`** sur la branche `main` (depuis GitHub : ouvrir le dossier `jeedom/source`, puis « Add file » / « Upload files »).
3. Enregistrer le changement. GitHub Actions lance le workflow « Publish Mira Jeedom plugin beta ».
4. Attendre sa réussite dans l'onglet **Actions**. La branche `jeedom-beta` contient alors les fichiers du plugin à la racine.

Le workflow rejette les archives mal structurées ou dont l'identifiant est différent de `mira`. Il ne modifie ni les sources ni les manifestes du firmware.

## Installation sur Jeedom

Activer la source GitHub dans `Réglages → Système → Configuration → Mises à jour/Market → Github`.

Depuis `Plugins → Gestion des plugins → Ajouter → Sources`, choisir :

| Champ | Valeur |
| --- | --- |
| Type de source | Github |
| ID logique du plugin | `mira` |
| Utilisateur | `Arokh76` |
| Dépôt | `Mira-panel` |
| Branche | `jeedom-beta` |

Les versions ultérieures sont détectées dans le Centre de mise à jour de Jeedom. Ne pas supprimer le plugin installé pour changer de source : prendre une sauvegarde Jeedom, puis réinstaller par-dessus via la source GitHub en conservant le même ID `mira`.

Le contenu publié est une **préversion de test**, distincte des mises à jour OTA du firmware des écrans. Publier seulement les versions validées pour les testeurs.

**Important :** aucune version installable n'existe sur `jeedom-beta` tant que la première archive `jeedom/source/mira.zip` n'a pas été envoyée et que le workflow n'a pas réussi.
