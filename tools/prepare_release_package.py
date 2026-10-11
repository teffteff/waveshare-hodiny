#!/usr/bin/env python3
"""Připraví minimální instalační balíček a samostatný OTA obraz.

Deska se volí proměnnou RELEASE_BOARD (lcd21 nebo lcd7, výchozí lcd21). Kulatá
2,1" má historické názvy (manifest.json, ota.json, waveshare-hodiny.*.bin),
7" vlastní s příponou -lcd7, aby oba balíčky mohly ležet v jednom adresáři
webu a žádná deska nesáhla po obrazu té druhé.
"""

from __future__ import annotations

import hashlib
import json
import os
import re
import shutil
import sys
from pathlib import Path


VERSION_PATTERN = re.compile(
    r"^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)"
    r"(?:-(?:0|[1-9][0-9]*|[0-9]*[A-Za-z-][0-9A-Za-z-]*)"
    r"(?:\.(?:0|[1-9][0-9]*|[0-9]*[A-Za-z-][0-9A-Za-z-]*))*)?"
    r"(?:\+[0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*)?$"
)
OTA_SLOT_SIZE = 6 * 1024 * 1024
BOARDS = {
    "lcd21": {
        "suffix": "",
        "title": "Waveshare Hodiny",
        "description": "Konfigurovatelné hodiny a Home Assistant dashboard pro Waveshare ESP32-S3-Touch-LCD-2.1.",
    },
    "lcd7": {
        "suffix": "-lcd7",
        "title": "Waveshare Hodiny 7\"",
        "description": "Konfigurovatelné hodiny a Home Assistant dashboard pro Waveshare ESP32-S3-Touch-LCD-7.",
    },
}


def write_json(path: Path, value: object) -> None:
    path.write_text(
        json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )


def main() -> None:
    if len(sys.argv) not in (8, 9) or not VERSION_PATTERN.fullmatch(sys.argv[1]):
        raise SystemExit(
            "Použití: prepare_release_package.py VERZE BOOTLOADER PARTITIONS "
            "BOOT_APP0 APP_BIN OTA_BIN ADRESAR [VEREJNA_ZAKLADNI_CESTA]"
        )
    version = sys.argv[1]
    board = os.environ.get("RELEASE_BOARD", "lcd21")
    if board not in BOARDS:
        raise SystemExit(f"Neznámá deska RELEASE_BOARD={board} (lcd21 nebo lcd7).")
    info = BOARDS[board]
    prefix = "waveshare-hodiny" + info["suffix"]
    flash_parts = [
        (Path(sys.argv[2]).resolve(), f"{prefix}.bootloader.bin", 0x0),
        (Path(sys.argv[3]).resolve(), f"{prefix}.partitions.bin", 0x8000),
        (Path(sys.argv[4]).resolve(), f"{prefix}.boot-app0.bin", 0xE000),
        (Path(sys.argv[5]).resolve(), f"{prefix}.app.bin", 0x10000),
    ]
    ota_source = Path(sys.argv[6]).resolve()
    package_dir = Path(sys.argv[7]).resolve()
    public_base_path = sys.argv[8].rstrip("/") if len(sys.argv) == 9 else ""
    if public_base_path and not public_base_path.startswith("/"):
        raise SystemExit("Veřejná základní cesta musí začínat lomítkem.")
    for source, _, _ in flash_parts:
        if not source.is_file() or source.stat().st_size == 0:
            raise SystemExit(f"Flash část neexistuje nebo je prázdná: {source}")
    if not ota_source.is_file() or not 0 < ota_source.stat().st_size < OTA_SLOT_SIZE:
        raise SystemExit("OTA obraz je prázdný nebo se nevejde do 6MiB slotu.")

    package_dir.mkdir(parents=True, exist_ok=True)
    for child in package_dir.iterdir():
        if child.is_file():
            child.unlink()
        else:
            shutil.rmtree(child)

    ota_name = f"{prefix}.ota.bin"
    for source, name, _ in flash_parts:
        shutil.copy2(source, package_dir / name)
    shutil.copy2(ota_source, package_dir / ota_name)
    write_json(
        package_dir / f"project{info['suffix']}.json",
        {
            "name": info["title"],
            "description": info["description"],
            "icon": "clock",
        },
    )
    write_json(
        package_dir / f"manifest{info['suffix']}.json",
        {
            "name": info["title"],
            "version": version,
            "new_install_prompt_erase": True,
            "new_install_improv_wait_time": 20,
            "builds": [
                {
                    "chipFamily": "ESP32-S3",
                    "improv": True,
                    "parts": [
                        {"path": name, "offset": offset}
                        for _, name, offset in flash_parts
                    ],
                }
            ],
        },
    )
    ota_sha256 = hashlib.sha256((package_dir / ota_name).read_bytes()).hexdigest()
    if public_base_path:
        write_json(
            package_dir / f"ota{info['suffix']}.json",
            {
                "version": version,
                "chipFamily": "ESP32-S3",
                "board": board,
                "size": ota_source.stat().st_size,
                "sha256": ota_sha256,
                "url": f"{public_base_path}/{ota_name}",
            },
        )
    print(f"OTA_SIZE={ota_source.stat().st_size}")
    print(f"OTA_SHA256={ota_sha256}")


if __name__ == "__main__":
    main()
