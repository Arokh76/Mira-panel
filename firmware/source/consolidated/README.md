# Mira Panel — consolidated source 0.6.52

This snapshot is generated from the **already released and tested
Mira 0.6.52 code**, not a new firmware feature.

- Source: MIRA_PANEL project in this directory
- Original reconstruction: firmware/source/Mira_Panel_Source_V0.4.3.zip
  + firmware/patches + overrides + scripts through 0.6.52
- Bundled: generated Montserrat home/sleep fonts (fixed upstream commit)
- WT32: sketch lv_conf.h, LVGL 8.3.11, LVGL allocations in PSRAM
- Waveshare: external official pin and LVGL 8.4, 12 MHz RGB +
  triple-buffer, configured in the GitHub build workflow

Do not build the Waveshare with the WT32 sketch-local lv_conf.h.
The build deletes that file on the Waveshare target as before.

Historical reconstruction files remain available for audit/rollback.
Only merge the new snapshot pipeline after WT32 and Waveshare
compilation succeeds independently with matching firmware targets.
