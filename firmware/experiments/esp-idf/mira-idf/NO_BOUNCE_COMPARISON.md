# Mira ESP-IDF 5.5.6 / LVGL 9.6 — RGB no-bounce differential test

## Evidence from 31 October 2026 hardware
The user tested the 800×480 Waveshare 4.3C on PR #101's initial IDF firmware: **7.3 FPS** (vs 5.8 FPS on LVGL9 Arduino), but severe image instability. Video: discontinuous duplicated columns and warped card sections, especially during vertical scrolling. It is not acceptable despite FPS gain.

## Targeted change
- `bounce_buffer_size_px`: **800×10 -> 0**; EDMA streams directly from the dual RGB565 PSRAM framebuffers, avoiding ISR copies from PSRAM into bounce buffers while LVGL is drawing.
- Native ESP-IDF `on_frame_buf_complete` (not `on_vsync`) is still the condition for LVGL to reuse the buffer.
- Additional `on_vsync` counter and `on_frame_buf_complete` counter in `/perf`.
- **Do not release framebuffers on a 125ms timeout.** Keep waiting and log timeout. The former code did mark flushed frames ready even after missing synchronization.
- Identical 35 cards and scroll rate, PCLK 12 MHz and LVGL9 config: compare behavior on physical hardware, not merely compilation.

## Critical caveats
- **Direct PSRAM DMA without bounce is not always better**. On ESP32-S3, EDMA and CPU share PSRAM bandwidth; slow or disrupted DMA could still cause artifacts. Do not infer the cause or any FPS gain from compilation.
- If artifacts remain, next differential probe is *static screen with scrolling disabled*, then **larger bounce buffers / reduced DMA contention** or altered PCLK. Compare without layering multiple changes at once.
- Same 0.7.37 Waveshare OTA partitions and recovery AP `Mira-IDF-Test` are preserved. Recovery HTTP not validated on every failure mode; retain USB access.
- This is a standalone benchmark, not the complete Mira Jeedom firmware. No changes to stable.
