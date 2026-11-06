# Maintenance du firmware Mira Panel

## Référence
- Sources : `firmware/source/consolidated/MIRA_PANEL/` (base 0.7.37 et correctifs bêta WT32 validés).
- Derniers binaires de release conservés : `firmware/releases/0.7.37/` (antérieurs au thème Rose).
- Sorties OTA et FACTORY différentes par matériel : WT32-SC01 Plus, Waveshare 4.3C, Open Nextion 2.8.
- Ne jamais croiser les binaires OTA/FACTORY entre modèles.

## Construction
`.github/workflows/build-mira.yml` utilise directement les sources consolidées. Aucun script `firmware/scripts/v0.*`, patch intermédiaire ou surcharge historique n'est requis pour cette construction. La publication de la branche principale n'est pas déclenchée par le simple nettoyage de `firmware-beta`.

Conserver `firmware/scripts/publish_multi.py` et `tests/test_publish_multi.py` pour les manifestes et les tests. Conserver `firmware/fonts/Montserrat-OFL.txt` pour les droits des polices.

## Récupération
Le point de sauvegarde `backup/firmware-beta-pre-cleanup-20261106` conserve l'arborescence avant tri. Le firmware WT32 testé avec JSON, thème Rose et logo est sauvegardé à `backup/wt32-dimitri-rose-logo-valide-20261106`.

Le code actif peut contenir des modifications bêta non publiées : ne pas interpréter les binaires de `0.7.37` comme comprenant automatiquement ces changements.
