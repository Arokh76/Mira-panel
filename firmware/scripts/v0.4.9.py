from pathlib import Path
import sys

root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("firmware/source/work/MIRA_PANEL")
ino = root / "MIRA_PANEL.ino"
server = root / "MiraPanelServer.cpp"
index_h = root / "HTML" / "HTML_Index.h"


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

old_css = '.hint{font-size:14px;color:var(--muted);line-height:1.45;margin:0 0 18px}.actions{text-align:center;font-size:18px}.quick{display:grid;grid-template-columns:repeat(4,1fr);gap:12px}'
new_css = '.hint{font-size:14px;color:var(--muted);line-height:1.45;margin:0 0 18px}.actions{text-align:center;font-size:18px}.mira-info{font-size:clamp(22px,2.6vw,30px)!important;line-height:1.25;margin:10px 0!important}.quick{display:grid;grid-template-columns:repeat(4,1fr);gap:12px}'
replace_once(index_h, old_css, new_css, "normalized info typography")

print("Mira Panel 0.4.9 post-processing applied")
