# Nettoyage final des branches — 13 novembre 2026

Les huit références ci-dessous ont été relevées **avant suppression**. Le workflow `.github/workflows/archive-final-mira-branches.yml` vérifie leurs SHA exacts, crée pour chacune un tag Git `archive/mira-final-20261113/<ancien-nom>`, vérifie les tags distants et seulement ensuite supprime les branches.

Le code reste accessible via son tag Git même si la branche a été effacée. La branche `backup/mira7-bootresync-http24k-before-404-fix-20261111` qui est à l'origine de la build Mira7 HTTP24K est donc préservée explicitement.

```text
6c24a84edd9265dd89a05789e2b03a3b1de633b1	archive/mira-preclean-20261101
dffe875d7d11a3f1b532492215d670c601049d59	backup/firmware-beta-before-wave-arduino-removal-20261106
9790a1f110fed1c96b1d9dcfbb9a4155fa868649	backup/firmware-beta-pre-cleanup-20261106
c91e1daba901040f0e637038555b4d9a67df3582	backup/mira-43c-lvgl96-before-original-icons-relief-20261101
fe6867f5d6ef09d9fe96d0de04181942020c790d	backup/mira7-bootresync-http24k-before-404-fix-20261111
db0abe65a6cc7d32e20cf5cfb662ec33f9348417	backup/waveshare7-jeedom-actions-20261110
5e1b9d88e4afcad66c23057ab1ea01a73fae68e1	backup/wt32-dimitri-rose-logo-valide-20261106
91d5e4fc3880707434877f8400fbd787bb1ab704	cleanup-mira-0638-stable
```

À ne jamais supprimer dans cette opération : `main`, `firmware-beta`, `beta`, `jeedom-beta`, `stable`, `release/*`.

Historique du premier ménage : 40 tags `archive/mira-precleanup-20261113/*` préservent les essais `experiment/`, `test/` et `cleanup/`.

État final attendu : **10 branches**, **48 nouveaux tags d'archives** entre les deux passages de nettoyage.
