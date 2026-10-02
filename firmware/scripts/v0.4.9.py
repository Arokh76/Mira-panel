from pathlib import Path
import sys

root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("firmware/source/work/MIRA_PANEL")
ino = root / "MIRA_PANEL.ino"
server = root / "MiraPanelServer.cpp"
index_h = root / "HTML" / "HTML_Index.h"
html_dir = root / "HTML"

ICON_B64 = "iVBORw0KGgoAAAANSUhEUgAAABgAAAAYCAYAAADgdz34AAAE40lEQVR42m2WW4xeVRXHf2vt891mbKedaSFA4oXEpCU+iCkmGlETYyBaUUCh0eFBGUhqNUqKl3BRazQ0hBis0highhhjJPJCeCeGBgPUWHpJeLQJJNiBUjp05rudvf4+7PN9MyWcZGXvs8/ea63/f132McAALXxqaV+ovkvS1WZe4Qk8GZbAHLOEzDGzckQChBQgYWSBwj2dcfcnl1889CjIDIwtO299XGZ35TwCwMzBHLwZzTFPYKnxx1AzK4YCKU9XveqQpKd+cNOf77SFnbfsHdXjw1EPaswdS26eMHO0UblXU2MIJGEFPCgDUSRyKHKk9myVvLontTZ/9K85D+ZBhuSg4okmQiPCxJQSFFgEihoUOEI5Q9QFWB7LYEcVefRhqfYGMO7OcJBptdqYO5ALNZGQJwwHs8JM44iZGI5GtFqpIDNzKZPHwytdOZsipjD7/TW2b51lPFwjj4agGsW4SC5j5BGKEcQY1SNWVy6wfa5LPeyDMoqMIlBkuRQQJUDDlRU+t2snp174G0/9/j7GayvEeIQRKMZFYTNa1JDHrL59lgP3LnLyn0e44QufpH9hBTegobJSZETGzIm1VT597U7mt87x7W99hVBwx9J99LbMY6lV+MZwd4jg4rlzPHLwHvbvWwRgx9VX8OxgFWNzQeCGM0m1CEAMB0Mk0e8PWLxtN0cOH6B//h2UayAKmnrM6tvLHHrkXvbvW6TfnBkOBqCSsjRSNdFiYsgMzIyqSgyHI763eDPnB2N+8uPf0JtfINeZ4bvvcvjQ/exdup3hcESVSgGaWVEcuVCUJwg2PLZh3um0WR2O+frSbdz6y5/RP7dMpZojjx9g79LtSKLTaa/7R1N0TeGJwDWhSHGJCXfnxImTvGcVry6P+dAd36G680fMpwF7brkRgOW33uL4qydxT1MTU12N+JSiSWE1T0qJ3x78Az/c/yDLm1u8cGZIveduXt+1hy99cTcnTr3GDbsX+fs/niMl36BjXbmU14OsiQesG5nbfgXP/PExnnj4Sc4udNDrA/zmn/LS7Jf57PVf5cSx19h22eWXcqxAKjQVBI1C+wADCLx3Facf/h1rT/8FPt4lzg/wz/+cuOb7eBIR9j4DwjYgcSSkpu0CEUHOQc6lGkOis3A51cGHsKPPwK4u4TWx42tEDiJys7/MS5A1bec+dRUgMt1uh5SclBLdTguiBjNsbhvpF7/CTj8L36zQNQZkZmYm+51up13qaRKDCKqSPSIi8E6Hf718nKMvvkJIvPyf03hvhsgZqyp8Zg7uf5B4KMP/zmLtDs8fPcZ1136CnDMv/fsU3usREUhgJqx72XX9iHEXK7191F+DXJe+35mlM7OpALWG67pG712AVsJ6s4zWLkIeFUpaHdrd3jr/5oNqUh7WMNWZmW1utIS8QrJC0SSGVYXNbwOBKdPdtKXQKCGEYqoNA3Mz+69BIWzSlmSErNksbHLJACawCCxy08JE4KVLTe+ICKQwtzdSb+tH+gq+oajjkkZh1rA2Uaz1NN5YN5PCYpqekmq5V6nt/kAarbxxvL3pqq2Cz0jZimoZqLnd1axhkgyp+R5mNHNFeVc2JHNzT67HLr557NcGOBCbrtz13XEde6X8sSaqYG5ltPITMGklZtPOW9akJm0iJTvj+J8uvvnKEcD/D4kuGOnactXiAAAAAElFTkSuQmCC"


def replace_once(path: Path, old: str, new: str, label: str):
    text = path.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected exactly 1 match in {path}, got {count}")
    path.write_text(text.replace(old, new, 1), encoding="utf-8")


replace_once(ino, '"0.4.8"', '"0.4.9"', "firmware version")
replace_once(ino, '[MIRA] Firmware 0.4.8 initialise', '[MIRA] Firmware 0.4.9 initialise', "startup log")

old_info = 'RETOUR += "<div style=\\"margin-top: 10px; font-size: " + Size + "px;\\">" + Nom + ": <b><span id=\\"" + ID + "\\">" + Value + "</span></b><span id=\\"Unit" + ID + "\\"> " + Unit + "</span></div>";'
new_info = 'RETOUR += "<div class=\\"mira-info\\">" + Nom + ": <b><span id=\\"" + ID + "\\">" + Value + "</span></b><span id=\\"Unit" + ID + "\\"> " + Unit + "</span></div>";'
replace_once(server, old_info, new_info, "normalize info widgets")

old_css = '.hint{font-size:14px;color:var(--muted);line-height:1.45;margin:0 0 18px}.actions{text-align:center;font-size:18px}.quick{display:grid;grid-template-columns:repeat(4,1fr);gap:12px}'
new_css = '.hint{font-size:14px;color:var(--muted);line-height:1.45;margin:0 0 18px}.actions{text-align:center;font-size:18px}.mira-info{font-size:clamp(22px,2.6vw,30px);line-height:1.25;margin:10px 0}.quick{display:grid;grid-template-columns:repeat(4,1fr);gap:12px}'
replace_once(index_h, old_css, new_css, "normalized info typography")

brand_old = '<div class="brand">%APP%</div>'
brand_new = '<div class="brand"><img src="data:image/png;base64,' + ICON_B64 + '" alt="" aria-hidden="true" style="width:24px;height:24px;vertical-align:-5px;margin-right:6px;border-radius:6px">ira Panel</div>'
brand_count = 0
for path in sorted(html_dir.glob("*.h")):
    text = path.read_text(encoding="utf-8")
    count = text.count(brand_old)
    if count:
        path.write_text(text.replace(brand_old, brand_new), encoding="utf-8")
        brand_count += count

if brand_count < 5:
    raise SystemExit(f"brand logo: expected several modern pages, got {brand_count}")

print(f"Mira Panel 0.4.9 post-processing applied ({brand_count} header logos)")
