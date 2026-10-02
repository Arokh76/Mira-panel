from pathlib import Path
import sys

root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("firmware/source/work/MIRA_PANEL")
ino = root / "MIRA_PANEL.ino"
server = root / "MiraPanelServer.cpp"


def replace_once(path: Path, old: str, new: str, label: str):
    text = path.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected exactly 1 match in {path}, got {count}")
    path.write_text(text.replace(old, new, 1), encoding="utf-8")


replace_once(ino, '"0.4.8"', '"0.4.9"', "firmware version")
replace_once(ino, '[MIRA] Firmware 0.4.8 initialise', '[MIRA] Firmware 0.4.9 initialise', "startup log")

old_info = 'RETOUR += "<div style=\\"margin-top: 10px; font-size: " + Size + "px;\\">" + Nom + ": <b><span id=\\"" + ID + "\\">" + Value + "</span></b><span id=\\"Unit" + ID + "\\"> " + Unit + "</span></div>";'
new_info = 'RETOUR += "<div class=\\"mira-info\\" style=\\"margin-top: 10px; font-size: " + Size + "px;\\">" + Nom + ": <b><span id=\\"" + ID + "\\">" + Value + "</span></b><span id=\\"Unit" + ID + "\\"> " + Unit + "</span></div>";'
replace_once(server, old_info, new_info, "normalize info widgets")

print("Mira Panel 0.4.9 post-processing applied")
