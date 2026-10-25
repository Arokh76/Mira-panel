# Mira OTA — three hardware profiles (0.7.15 candidate)

The firmware codebase and published version number are shared across:
- WT32-SC01 Plus — `wt32-sc01-plus`, 8 MB flash
- Waveshare ESP32-S3 Touch LCD 4.3C — `waveshare-esp32s3-touch-lcd-4.3c`, 16 MB
- Open Nextion Genius 2.8 (ONX2432G028) — `onx2432g028`, 16 MB (**experimental until hardware-validated**)

**Never cross-flash devices.** A shared software version does not mean the three ESP32 binaries are interchangeable.

## How an update works

1. A change to the consolidated firmware is merged into `main` with a new shared version.
2. `build-mira.yml` compiles and packages all three profiles with their own board flags, drivers and flash layout. It also runs release tests.
3. Only if all three builds succeed, `publish-mira-targets.yml` downloads the artifacts from that precise successful commit and publishes one atomic version to `firmware/targets.json`, individual OTA/FACTORY paths, and USB Web Installer manifests.
4. `pages.yml` automatically deploys the updated Web Installer.
5. On the device's System page, « Vérifier les mises à jour » selects the OTA URL from `targets.json.targets[MIRA_DISPLAY_PROFILE_ID]`. The user must confirm installation. The device's existing configuration is preserved by OTA.

No background auto-flashing is performed: discovery and downloading are automatic, installation is user-initiated.

## Legacy updater / transition

`firmware/version.json` and `firmware/Mira_Panel.bin` refer **only to WT32**. They are intentionally pinned at the previous version because old Waveshare builds also queried that untyped URL. Publishing a new WT32 binary there would be dangerous. Firmware with the profile-aware updater reads `firmware/targets.json`. Older firmware that still uses the untyped updater must be migrated safely through the System web UI or a matching manual OTA binary; the first transition on some older builds cannot be fully automatic.

## Nextion caution

Open Nextion Genius 2.8 development build dev8 was a **diagnostic build for a boot loop**, not a hardware-approved stable release. A successful GitHub compilation and correct OTA partition layout do not prove that the board boots, lights the backlight, reads touch, stays stable, or completes an OTA. A USB recovery/flash backup must be available before the first hardware test. Do not treat this target as stable until those tests pass.
