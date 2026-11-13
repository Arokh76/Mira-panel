# Mira Panel 4,3″ — essai USB en installation propre

**Cible exclusive** : Waveshare ESP32-S3 Touch LCD **4.3C**, 800×480, LCD RGB 12 MHz, tactile GT911. **Ne pas flasher sur le 7 pouces.**

Version expérimentale : ESP-IDF 5.5.6 / LVGL 9.6 / HTTP Jeedom par défaut. Cette version a compilé, mais son fonctionnement réel reste à vérifier sur l'écran.

## 1 — Préparation

- Télécharger l'artefact GitHub Actions `MIRA_0_7_37_UI_LVGL96_4_3C_FACTORY_OTA` associé au **dernier commit** de la branche.
- Décompresser l'archive et utiliser uniquement `Mira_LVGL96_43C_UI_FACTORY_USB_0x0.bin` pour cet essai.
- Ne **pas** utiliser le fichier `*_OTA.bin` pour une installation USB effacée.
- Si possible, conserver à part l'export de la programmation actuelle `miralvgl.json`, les paramètres Jeedom et le firmware de retour arrière.

## 2 — Effacer entièrement et flasher par USB

Attention : `erase_flash` supprime **toutes** les données locales : NVS, Wi-Fi, clés Jeedom, préférences et SPIFFS. Le firmware ne garde aucun paramètre de la bêta.

Avec `esptool` disponible (remplacer `COM7` par le port de l'ESP32-S3) :

```powershell
python -m esptool --chip esp32s3 --port COM7 erase_flash
python -m esptool --chip esp32s3 --port COM7 --baud 460800 write_flash 0x0 Mira_LVGL96_43C_UI_FACTORY_USB_0x0.bin
```

Si le programme ne peut pas ouvrir le port, placer l'ESP32-S3 en mode USB bootloader/recovery et réessayer, sans changer de firmware.

## 3 — Premier démarrage : aucun fichier utilisateur

- L'écran doit afficher Mira/LVGL, sans boucle de redémarrage. L'absence de widgets est **normale** avant import.
- Le firmware doit initialiser **uniquement une partition SPIFFS totalement vierge**. Il ne formate jamais une partition contenant des données.
- Depuis un téléphone/ordinateur, rejoindre le Wi-Fi `Mira-LVGL96-43` avec le mot de passe de test `MiraIDF2026`.
- Ouvrir **http://192.168.4.1/mira**.

**Ordre conseillé pendant la connexion au point d'accès Mira** : renseigner la clé d'écran déjà utilisée par le plugin Jeedom (si disponible), importer une programmation JSON Mira si souhaité, **puis configurer le Wi-Fi domestique en dernier**. Le point d'accès de secours se désactive automatiquement quelques secondes après la connexion au réseau domestique.

Une fois le Wi-Fi configuré, reprendre la liaison **HTTP du plugin Mira sous Jeedom**. MQTT est facultatif et ne doit pas être nécessaire au fonctionnement.

## 4 — Contrôles à effectuer

1. Démarrage de l'écran, lumière, absence de reboot ; tactile et navigation.
2. Configuration Wi-Fi domestique : adresse IP, heure et date.
3. Liaison HTTP Jeedom : états passifs puis commandes (un bouton, un switch, une consigne thermostat).
4. Import/reprogrammation `miralvgl.json` : retour de tous les widgets et navigation entre les pages.
5. Gestes multiaction, RGB complet, thermostat 0,5 °C, sélections de liste, défilement, veille et réveil.
6. JPEG/caméra si utilisée : ouverture/fermeture répétées et stabilité.
7. Vérifier `http://ADRESSE_IP_ECRAN/perf` en cas de lenteurs.

Si écran noir ou redémarrages : **ne pas reflasher en boucle**. Récupérer d'abord le journal série USB à 115200 bauds, ou une capture du point de diagnostic si accessible, puis conserver le binaire exact testé.

## État de qualification

Le build GitHub Actions valide la compilation et le packaging, **pas** la compatibilité réelle du GT911, du rétroéclairage, des profils Wi-Fi et des widgets. Ne pas remplacer le firmware stable tant que ces tests ne sont pas concluants.
