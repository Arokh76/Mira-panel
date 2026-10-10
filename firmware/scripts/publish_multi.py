#!/usr/bin/env python3
"""Prepare one Mira release with verified, hardware-specific OTA and factory files.

This script stages files in a working checkout. GitHub publishing is a separate,
manual-only action, and never happens when a required target is missing.
"""
import argparse
import hashlib
import json
import re
from pathlib import Path

TARGETS = {
    "wt32-sc01-plus": {
        "label": "WT32-SC01 Plus",
        "dir": "wt32",
        "ota": "Mira_Panel_WT32_SC01_Plus_{version}.bin",
        "factory": "Mira_Panel_WT32_SC01_Plus_{version}_FACTORY.bin",
        "docs_factory": "Mira_Panel_WT32_SC01_Plus_FACTORY.bin",
        "manifest": "manifest-wt32.json",
        "flash_mb": 8,
    },
    "waveshare-esp32s3-touch-lcd-4.3c": {
        "label": "Waveshare ESP32-S3 Touch LCD 4.3C",
        "dir": "waveshare",
        "ota": "Mira_Panel_Waveshare_4.3C_{version}.bin",
        "factory": "Mira_Panel_Waveshare_4.3C_{version}_FACTORY.bin",
        "docs_factory": "Mira_Panel_Waveshare_4.3C_FACTORY.bin",
        "manifest": "manifest-waveshare.json",
        "flash_mb": 16,
    },
}
RAW_BASE = "https://raw.githubusercontent.com/Arokh76/Mira-panel/main/"
VERSION_RE = re.compile(r"^\d+\.\d+\.\d+$")


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def prepare(inputs: Path, root: Path, version: str) -> dict:
    if not VERSION_RE.fullmatch(version):
        raise ValueError("Invalid Mira version")
    # First read and validate ALL inputs, before touching the repository.
    prepared = {}
    for profile, spec in TARGETS.items():
        folder = inputs / spec["dir"]
        binary = {}
        for kind in ("ota", "factory"):
            name = spec[kind].format(version=version)
            path = folder / name
            if not path.is_file():
                raise FileNotFoundError(f"Missing {profile} {kind}: {path}")
            raw = path.read_bytes()
            if not 500_000 < len(raw) <= spec["flash_mb"] * 1024 * 1024:
                raise ValueError(f"Unexpected {profile} {kind} size: {len(raw)}")
            if kind == "ota" and version.encode("ascii") not in raw:
                raise ValueError(f"Incorrect version in {profile} OTA")
            binary[kind] = raw
        prepared[profile] = binary

    targets = {}
    for profile, spec in TARGETS.items():
        sub = f"firmware/releases/{version}/{profile}"
        release_dir = root / sub
        release_dir.mkdir(parents=True, exist_ok=True)
        ota = prepared[profile]["ota"]
        factory = prepared[profile]["factory"]
        (release_dir / "ota.bin").write_bytes(ota)
        (release_dir / "factory.bin").write_bytes(factory)

        web_factory = root / "docs" / "firmware" / spec["docs_factory"]
        web_factory.parent.mkdir(parents=True, exist_ok=True)
        web_factory.write_bytes(factory)
        ota_url = RAW_BASE + sub + "/ota.bin"
        factory_url = RAW_BASE + sub + "/factory.bin"
        targets[profile] = {
            "label": spec["label"],
            "flashMB": spec["flash_mb"],
            "ota": ota_url,
            "factory": factory_url,
            "otaSha256": sha256(ota),
            "factorySha256": sha256(factory),
            "otaBytes": len(ota),
        }

        web_manifest = {
            "name": f"Mira Panel - {spec['label']}",
            "version": version,
            "new_install_prompt_erase": True,
            "builds": [{
                "chipFamily": "ESP32-S3",
                "parts": [{"path": "firmware/" + spec["docs_factory"], "offset": 0}],
            }],
        }
        dest = root / "docs" / spec["manifest"]
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_text(json.dumps(web_manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")

    # Legacy WT32 0.6.5 installations still use the generic OTA URL.
    # These two files MUST remain specific to WT32, never to Waveshare.
    wt32_ota = prepared["wt32-sc01-plus"]["ota"]
    (root / "firmware" / "Mira_Panel.bin").write_bytes(wt32_ota)
    wt32_manifest = root / "docs" / TARGETS["wt32-sc01-plus"]["manifest"]
    (root / "docs" / "manifest.json").write_bytes(wt32_manifest.read_bytes())
    legacy_version = {
        "project": "Mira Panel",
        "version": version,
        "bin": RAW_BASE + "firmware/Mira_Panel.bin",
        "factory": targets["wt32-sc01-plus"]["factory"],
        "installer": "https://arokh76.github.io/Mira-panel/",
    }
    (root / "firmware" / "version.json").write_text(
        json.dumps(legacy_version, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )

    release = {"project": "Mira Panel", "version": version, "targets": targets}
    outfile = root / "firmware" / "targets.json"
    outfile.parent.mkdir(parents=True, exist_ok=True)
    outfile.write_text(json.dumps(release, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    return release


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--inputs", required=True, type=Path)
    parser.add_argument("--repo", default=".", type=Path)
    parser.add_argument("--version", required=True)
    args = parser.parse_args()
    release = prepare(args.inputs, args.repo, args.version)
    print(json.dumps({"version": release["version"],
                      "profiles": list(release["targets"])}, ensure_ascii=False))


if __name__ == "__main__":
    main()
