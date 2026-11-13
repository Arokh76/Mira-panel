# Règle impérative de développement — Mira Panel

> **« Ne cherche pas à patcher un truc si la réponse est dans le dossier à côté. »**
>
> Directive du mainteneur, formulée le 13 novembre 2026 après deux semaines consacrées à rétablir les comportements perdus lors de la migration Arduino → ESP-IDF.

## Principe non négociable : conserver les fonctions existantes

Le firmware **Arduino / WT32** présent dans `Arduino/MIRA_PANEL/` est la **référence fonctionnelle** du projet. Une migration vers ESP-IDF doit reproduire ses comportements avant toute optimisation ou refonte. Il ne faut jamais inventer un mécanisme, un écran, un workflow, une API ou une logique de récupération sans vérifier si la solution existe déjà dans les sources Arduino.

**Avant de corriger un comportement ESP-IDF :**

1. **Rechercher d'abord la mise en œuvre Arduino d'origine**, notamment `Arduino/MIRA_PANEL/MiraPanelServer.cpp`, `SPIFFS_Process.h`, les pages HTML et les composants associés.
2. **Tracer le parcours complet** : chargement initial, stockage, état en RAM, authentification, Wi-Fi/AP, configuration, sauvegarde, réinitialisation et interactions Jeedom.
3. **Comparer explicitement Arduino ↔ ESP-IDF**, identifier la divergence exacte et, en priorité, **porter le comportement éprouvé**, au lieu de masquer ses symptômes.
4. **Préserver le contrat de Mira** : mêmes usages, mêmes pages et parcours, même gestion attendue de la clé, du Wi-Fi, des widgets et de Jeedom ; n'adapter que ce que les différences de plateforme rendent nécessaire.
5. **Ne pas complexifier par principe** : les retries, caches, garde-fous et nouveaux modes de secours doivent répondre à un besoin démontré, pas compenser une régression du portage.
6. **Ne jamais effacer ni reformater** NVS, SPIFFS, profils réseau, clés, widgets ou partitions OTA lors d'un simple correctif de stabilité.
7. **Valider avec des preuves** : compilation CI, diagnostics et tests fonctionnels des parcours déjà présents sur WT32, notamment démarrage, reconnexion, authentification et récupération AP.

## Exemple concret : authentification locale

- **WT32** : la clé est lue depuis `/reseau.json` dans SPIFFS au démarrage, déchiffrée, puis conservée dans `Uncrypt_Pass` en RAM ; le serveur HTTP utilise cette valeur pour l'authentification.
- **ESP-IDF 7 pouces (état du portage le 13/11/2026)** : la clé native NVS est relue à chaque requête HTTP, et la présence d'une clé est aussi recontrôlée pendant l'exécution. Cela peut transformer une erreur de lecture passagère en faux retour à l'installation initiale.
- **Direction de correction** : repartir du comportement WT32 **avant** d'ajouter des mécanismes de réparation. Garder un chemin de récupération sécurisé pour une clé réellement absente, sans exposer d'écriture ou d'OTA non authentifiée sur le réseau domestique.

## Discipline GitHub

- Développer le firmware dans **`firmware-beta`**, sans multiplier les branches de test.
- Ne pas toucher aux branches du plugin Jeedom (`beta`, `stable`) ni à `main` sans demande explicite.
- Ne pas écraser les binaires de référence ni déployer un build expérimental sur un écran qui fonctionne sans validation.
- Les correctifs doivent rester ciblés, lisibles et réversibles. **Un firmware qui compile n'est pas automatiquement un firmware fonctionnel.**

**Rappel : l'objectif est de continuer à faire évoluer Mira Panel, pas de passer de nouvelles semaines à récupérer des fonctionnalités déjà acquises.**
