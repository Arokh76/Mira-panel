"""Mira 0.6.52 - WT32 LVGL allocations in PSRAM.

The WT32 uses Arduino-ESP32 2.0.17 + LVGL 8.3.11 and has 8 MB PSRAM.
With LV_MEM_CUSTOM=0, LVGL reserves 96 KiB in internal DRAM as a static
array, leaving about 24 KiB free for Wi-Fi/AsyncTCP. Move only LVGL object
allocation to capability-checked PSRAM. DMA display draw buffers and the
network stack remain in internal RAM.

The Waveshare CI target explicitly replaces lv_conf.h with its own bundled
configuration and therefore is NOT changed by this WT32 memory patch.
"""
from pathlib import Path
import sys

root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("firmware/source/work/MIRA_PANEL")
conf = root / "lv_conf.h"
ino = root / "MIRA_PANEL.ino"
server = root / "MiraPanelServer.cpp"

def once(s, old, new, label):
    count = s.count(old)
    if count != 1:
        raise SystemExit(f"0.6.52 {label}: expected 1 anchor, got {count}")
    return s.replace(old, new, 1)

s = conf.read_text(encoding="utf-8")
s = once(s, "#define LV_MEM_CUSTOM 0\n#define LV_MEM_SIZE (96U * 1024U)", """// WT32: LVGL objects in external PSRAM, preserving scarce DMA/internal RAM
// for ESPAsyncWebServer, Wi-Fi and the LCD DMA buffers.
#define LV_MEM_CUSTOM 1
#define LV_MEM_CUSTOM_INCLUDE "esp_heap_caps.h"
#define LV_MEM_CUSTOM_ALLOC(size) heap_caps_malloc((size), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#define LV_MEM_CUSTOM_FREE(ptr) heap_caps_free((ptr))
#define LV_MEM_CUSTOM_REALLOC(ptr, size) heap_caps_realloc((ptr), (size), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
// Kept for documentation: only used when LV_MEM_CUSTOM is disabled.
#define LV_MEM_SIZE (96U * 1024U)""", "LVGL heap")
conf.write_text(s, encoding="utf-8")

s = ino.read_text(encoding="utf-8")
s = once(s, '"0.6.51"', '"0.6.52"', "firmware version")
s = s.replace("[MIRA] Firmware 0.6.51 initialise", "[MIRA] Firmware 0.6.52 initialise")
ino.write_text(s, encoding="utf-8")

s = server.read_text(encoding="utf-8")
s = once(s, '    char body[288];', '    char body[440];', "health buffer")
s = once(s,
         '      "\\\"nav_count\\\":%u,\\\"nav_last_heap\\\":%u,\\\"nav_lowest_heap\\\":%u}",',
         '      "\\\"nav_count\\\":%u,\\\"nav_last_heap\\\":%u,\\\"nav_lowest_heap\\\":%u,"\n'
         '      "\\\"internal_heap_free\\\":%u,\\\"internal_largest_block\\\":%u}",',
         "health JSON field")
s = once(s, '      (unsigned)miraNavLastHeap, (unsigned)miraNavLowestHeap);',
         '      (unsigned)miraNavLastHeap, (unsigned)miraNavLowestHeap,\n'
         '      (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),\n'
         '      (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));',
         "health internal heap metrics")
server.write_text(s, encoding="utf-8")
print("0.6.52: WT32 LVGL heap moved to PSRAM; internal heap diagnostics added")
