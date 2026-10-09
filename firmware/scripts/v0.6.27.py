from pathlib import Path
import sys

root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path('firmware/source/work/MIRA_PANEL')
ino = root / 'MIRA_PANEL.ino'
partitions = root / 'partitions.csv'

def replace_once(path: Path, old: str, new: str, label: str):
    text = path.read_text(encoding='utf-8')
    count = text.count(old)
    if count != 1:
        raise SystemExit(f'{label}: expected exactly 1 match in {path}, got {count}')
    path.write_text(text.replace(old, new, 1), encoding='utf-8')

replace_once(ino, '"0.6.26"', '"0.6.27"', 'firmware version')
replace_once(ino, '[MIRA] Firmware 0.6.26 initialise', '[MIRA] Firmware 0.6.27 initialise', 'startup log')

# 16 MB layout for Mira on Waveshare:
# keep two 3 MB OTA slots, but use SPIFFS (not FFat) for Mira configuration.
# The previous app3M_fat9M_16MB board preset exposed a FAT partition, therefore
# SPIFFS.begin()/config writes could not work even though Wi-Fi/web did.
partitions.write_text("""# Name,   Type, SubType, Offset,   Size,     Flags
nvs,      data, nvs,     0x9000,   0x5000,
otadata,  data, ota,     0xE000,   0x2000,
app0,     app,  ota_0,   0x10000,  0x300000,
app1,     app,  ota_1,   0x310000, 0x300000,
spiffs,   data, spiffs,  0x610000, 0x9E0000,
coredump, data, coredump,0xFF0000, 0x10000,
""", encoding='utf-8')

print('Mira Panel 0.6.27 applied: 16MB OTA + SPIFFS partition for Waveshare config persistence')
