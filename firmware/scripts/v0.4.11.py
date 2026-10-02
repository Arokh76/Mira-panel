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


replace_once(ino, '"0.4.10"', '"0.4.11"', "firmware version")
replace_once(ino, '[MIRA] Firmware 0.4.10 initialise', '[MIRA] Firmware 0.4.11 initialise', "startup log")

replace_once(
    server,
    'bool SetupModuleDone = false;\nString UpdateURL = "";',
    'bool SetupModuleDone = false;\nString MiraRuntimeVersion = "";\nString UpdateURL = "";',
    "runtime version storage",
)

replace_once(
    server,
    'void MiraPanelServer::SetupModule(String Module, String ID_Module, String Version, String MAJ, String Type) {\n  JsonArray TableMod = SPIFFS_Config_Module["module"];\n  JsonObject RowMod = TableMod.createNestedObject();',
    'void MiraPanelServer::SetupModule(String Module, String ID_Module, String Version, String MAJ, String Type) {\n  MiraRuntimeVersion = Version;\n  JsonArray TableMod = SPIFFS_Config_Module["module"];\n  TableMod.clear();\n  JsonObject RowMod = TableMod.createNestedObject();',
    "setup module version source",
)

replace_once(
    server,
    '          String version = SPIFFS_Uncrypt(SPIFFS_Config_Module["module"][0]["version"].as<String>());',
    '          String version = MiraRuntimeVersion;\n          if (!version.length()) {\n            String storedVersion = SPIFFS_Config_Module["module"][0]["version"].as<String>();\n            if (storedVersion.length()) version = SPIFFS_Uncrypt(storedVersion);\n          }',
    "legacy updater runtime version",
)

replace_once(
    server,
    '  if (var == "VERSION") {\n    String v = SPIFFS_Config_Module["module"][0]["version"].as<String>();\n    if (v.length()) v = SPIFFS_Uncrypt(v);\n    return v.length() ? v : "0.4.6";\n  }',
    '  if (var == "VERSION") {\n    if (MiraRuntimeVersion.length()) return MiraRuntimeVersion;\n    String v = SPIFFS_Config_Module["module"][0]["version"].as<String>();\n    if (v.length()) v = SPIFFS_Uncrypt(v);\n    return v.length() ? v : "0.4.11";\n  }',
    "version placeholder runtime source",
)

print("Mira Panel 0.4.11 runtime version fix applied")
