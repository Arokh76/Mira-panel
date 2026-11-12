# Firmware complet expérimental Mira Panel 7 STD

**Branche :** test/waveshare7-full-provision.
**Repli matériel confirmé :** test/waveshare7-ui-wifi-lowmem.
**Statut :** compilation CI uniquement. Reste à valider sur le Waveshare réel.

Au premier démarrage, se connecter au Wi-Fi **Mira Panel**, puis visiter
http://192.168.4.1 dans un navigateur. La procédure originale demande
d'abord une clé locale puis le réseau Wi-Fi et redémarre l'écran après
enregistrement. Aucune configuration Jeedom n'est encore présente sur un
écran dont la mémoire a été effacée.

Après le redémarrage, le serveur complet, le réseau de la box et les
communications Mira/Jeedom/MQTT sont démarrés, selon les réglages enregistrés.
Les caméras et pochettes n'ouvrent leur tâche que lorsqu'on les utilise.
L'import des tuiles depuis Jeedom nécessite SPIFFS monté.

Le splash réutilise le visuel WT32 avec simplement « Chargement XX % »,
adapté à 800x480 paysage. Les pourcentages correspondent à des étapes de
démarrage, pas à des fichiers téléchargés.

Le stockage SPIFFS est formaté **seulement si intégralement vierge** (0xFF).
Jamais d'effacement NVS ni de formatage d'une partition non vide.
Les réglages matériel RGB/GT911, PCLK 16 MHz et DMA bounce 10 lignes
sont inchangés.

Fichiers dans le ZIP GitHub Actions :
- MIRA7_FULL_PROVISION_APP_0x10000.bin -> 0x10000.
- BOOTLOADER_0x0.bin et PARTITIONS_0x8000.bin : secours, ne pas reflasher
  si la carte exécute déjà nos tests.
- MIRA7_OTADATA_VIERGE_0xE000.bin : seulement si le sélecteur OTA a changé.
- ADRESSES_FLASH.txt avec le tableau des adresses.

**Ne pas cocher Full Chip Erase**. Ne pas utiliser les images fusionnées 0x0.
Une compilation réussie ne garantit pas encore les allocations mémoire en
conditions réelles. Les logs FULL_BOOT_MEM, FULL_NET_READY / FULL_NET_FAILED,
FULL_SETUP_MODE et FULL_UI_READY permettront le diagnostic.

Notes : le GPIO du modèle 4.3C et le rétroéclairage variable du Waveshare
7 Standard ne sont pas validés. Ne pas raccorder de sorties 4.3C à cette carte.
