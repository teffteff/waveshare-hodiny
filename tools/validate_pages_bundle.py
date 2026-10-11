#!/usr/bin/env python3
"""Ověří statický Pages web a případný čtyřdílný instalační balíček."""

from __future__ import annotations

import json
import hashlib
import re
import sys
from pathlib import Path


EXPECTED_PARTS = {
    "waveshare-hodiny.bootloader.bin": 0x0,
    "waveshare-hodiny.partitions.bin": 0x8000,
    "waveshare-hodiny.boot-app0.bin": 0xE000,
    "waveshare-hodiny.app.bin": 0x10000,
}
ROOT = Path(__file__).resolve().parent.parent
ASSET_ENTRY_PATTERN = re.compile(
    r'\{"([^"]+)", (\d+), "([0-9a-f]{64})"\}'
)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def validate_weather_assets(site: Path) -> None:
    source = (ROOT / "WaveshareHodiny" / "WeatherAnimationService.cpp").read_text(
        encoding="utf-8"
    )
    expected = {
        key: {"size": int(size), "sha256": digest}
        for key, size, digest in ASSET_ENTRY_PATTERN.findall(source)
    }
    root = site / "assets" / "weather-icons"
    catalog = json.loads((root / "manifest.json").read_text(encoding="utf-8"))
    actual: dict[str, dict[str, object]] = {}
    referenced_files: set[Path] = set()
    for style in ("monochrome", "flat", "line"):
        version_info = catalog.get("versions", {}).get(style)
        if not isinstance(version_info, dict):
            raise SystemExit(f"V asset katalogu chybí styl {style}.")
        relative_manifest = version_info.get("manifest")
        if not isinstance(relative_manifest, str) or ".." in Path(relative_manifest).parts:
            raise SystemExit(f"Styl {style} má neplatnou cestu manifestu.")
        manifest_path = root / relative_manifest
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        if manifest.get("version") != version_info.get("version"):
            raise SystemExit(f"Verze asset manifestu {style} nesouhlasí s katalogem.")
        icons = manifest.get("icons")
        if not isinstance(icons, dict) or len(icons) != version_info.get("icons"):
            raise SystemExit(f"Asset manifest {style} má neplatný počet ikon.")
        for key, metadata in icons.items():
            if key in actual or not isinstance(metadata, dict):
                raise SystemExit(f"Duplicitní nebo neplatný asset: {key}")
            filename = metadata.get("file")
            if not isinstance(filename, str) or Path(filename).name != filename:
                raise SystemExit(f"Asset {key} má nebezpečnou cestu.")
            path = manifest_path.parent / filename
            if not path.is_file():
                raise SystemExit(f"Asset {key} chybí.")
            actual[key] = {"size": path.stat().st_size, "sha256": sha256(path)}
            referenced_files.add(path.resolve())
    published_files = {path.resolve() for path in root.glob("*/*.gif")}
    if actual != expected or published_files != referenced_files:
        raise SystemExit("Veřejné Meteocons neodpovídají firmwarovému allowlistu.")



BOARD_SUFFIX = {"lcd21": "", "lcd7": "-lcd7"}


def expected_parts(board: str) -> dict[str, int]:
    prefix = "waveshare-hodiny" + BOARD_SUFFIX[board]
    return {
        name.replace("waveshare-hodiny", prefix, 1): offset
        for name, offset in EXPECTED_PARTS.items()
    }


def check_build(manifest: dict, board: str, label: str) -> None:
    builds = manifest.get("builds")
    if not isinstance(builds, list) or len(builds) != 1:
        raise SystemExit(f"{label}: manifest musí obsahovat právě jeden build.")
    build = builds[0]
    if build.get("chipFamily") != "ESP32-S3" or build.get("improv") is not True:
        raise SystemExit(f"{label}: manifest nemá očekávaný ESP32-S3 Improv build.")
    parts = build.get("parts")
    wanted = expected_parts(board)
    if not isinstance(parts, list) or len(parts) != len(wanted):
        raise SystemExit(f"{label}: manifest musí obsahovat právě čtyři instalační části.")
    if any(
        not isinstance(part, dict)
        or set(part) != {"path", "offset"}
        or not isinstance(part["path"], str)
        or not isinstance(part["offset"], int)
        or isinstance(part["offset"], bool)
        for part in parts
    ):
        raise SystemExit(f"{label}: instalační část má neplatnou strukturu nebo typy.")
    paths = [part["path"] for part in parts]
    if len(paths) != len(set(paths)):
        raise SystemExit(f"{label}: manifest obsahuje duplicitní instalační část.")
    actual = {part["path"]: part["offset"] for part in parts}
    if actual != wanted:
        raise SystemExit(f"{label}: instalační části nebo offsety nesouhlasí: {actual}")


# Balíček jedné desky v adresáři firmware: manifest, čtyři části, OTA obraz
# a metadata. Metadata 2,1" ze starších release pole "board" nemají.
def validate_board_bundle(firmware: Path, board: str) -> dict:
    suffix = BOARD_SUFFIX[board]
    label = f"Deska {board}"
    manifest = json.loads((firmware / f"manifest{suffix}.json").read_text(encoding="utf-8"))
    check_build(manifest, board, label)
    for name in expected_parts(board):
        path = firmware / name
        if not path.is_file() or path.stat().st_size == 0:
            raise SystemExit(f"{label}: instalační část chybí nebo je prázdná: {path}")
    ota_name = f"waveshare-hodiny{suffix}.ota.bin"
    ota_path = firmware / ota_name
    ota_metadata_path = firmware / f"ota{suffix}.json"
    if not ota_path.is_file() or not ota_metadata_path.is_file():
        raise SystemExit(f"{label}: ve veřejném release chybí OTA obraz nebo metadata.")
    ota = json.loads(ota_metadata_path.read_text(encoding="utf-8"))
    base_keys = {"version", "chipFamily", "size", "sha256", "url"}
    if set(ota) != base_keys | {"board"} and not (board == "lcd21" and set(ota) == base_keys):
        raise SystemExit(f"{label}: OTA metadata mají neplatnou strukturu.")
    if (
        ota.get("board", "lcd21") != board
        or ota["version"] != manifest.get("version")
        or ota["chipFamily"] != "ESP32-S3"
        or ota["size"] != ota_path.stat().st_size
        or ota["sha256"] != sha256(ota_path)
        or ota["url"] != f"/waveshare-hodiny/firmware/{ota_name}"
    ):
        raise SystemExit(f"{label}: OTA metadata neodpovídají veřejnému OTA obrazu.")
    return manifest


def validate_archived_manifest(path: Path, version: str, board: str) -> dict:
    label = f"Archivní verze {version} ({board})"
    manifest = json.loads(path.read_text(encoding="utf-8"))
    if manifest.get("version") != version:
        raise SystemExit(f"{label}: manifest nesouhlasí s katalogem.")
    check_build(manifest, board, label)
    for name in expected_parts(board):
        part = path.parent / name
        if not part.is_file() or part.stat().st_size == 0:
            raise SystemExit(f"{label}: chybí část {name}.")
    return manifest

def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit("Použití: validate_pages_bundle.py ADRESAR_WEBU")

    site = Path(sys.argv[1]).resolve()
    for relative in ("index.html", "styles.css", "app.js"):
        path = site / relative
        if not path.is_file() or path.stat().st_size == 0:
            raise SystemExit(f"Povinný soubor webu chybí nebo je prázdný: {path}")
    index_html = (site / "index.html").read_text(encoding="utf-8")
    expected_css_url = f"styles.css?v={sha256(site / 'styles.css')[:12]}"
    expected_js_url = f"app.js?v={sha256(site / 'app.js')[:12]}"
    if expected_css_url not in index_html or expected_js_url not in index_html:
        raise SystemExit("HTML nepoužívá obsahové verze aktuálního CSS a JavaScriptu.")
    validate_weather_assets(site)

    firmware = site / "firmware"
    manifest_path = firmware / "manifest.json"
    if not manifest_path.exists():
        unexpected = [path for path in firmware.glob("*") if path.is_file()]
        if unexpected:
            raise SystemExit("Firmware adresář obsahuje soubory bez manifestu.")
        print("Web je platný; firmware zatím není publikovaný.")
        return
    manifest = validate_board_bundle(firmware, "lcd21")
    lcd7_manifest = None
    if (firmware / "manifest-lcd7.json").exists():
        lcd7_manifest = validate_board_bundle(firmware, "lcd7")
        if lcd7_manifest.get("version") != manifest.get("version"):
            raise SystemExit("Balíčky 2,1\" a 7\" mají různé verze.")

    catalog_path = firmware / "releases.json"
    if catalog_path.exists():
        catalog = json.loads(catalog_path.read_text(encoding="utf-8"))
        releases = catalog.get("releases")
        if not isinstance(releases, list) or not 1 <= len(releases) <= 5:
            raise SystemExit("Katalog musí obsahovat jednu až pět stabilních verzí.")
        versions: set[str] = set()
        for index, release in enumerate(releases):
            if not isinstance(release, dict) or set(release) not in (
                {"version", "manifest"},
                {"version", "manifest", "manifestLcd7"},
            ):
                raise SystemExit("Položka katalogu verzí má neplatnou strukturu.")
            version = release["version"]
            if (
                not isinstance(version, str)
                or not re.fullmatch(r"(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)", version)
                or version in versions
                or release["manifest"] != f"firmware/releases/{version}/manifest.json"
                or release.get("manifestLcd7", f"firmware/releases/{version}/manifest-lcd7.json")
                != f"firmware/releases/{version}/manifest-lcd7.json"
            ):
                raise SystemExit("Katalog obsahuje neplatnou nebo duplicitní verzi.")
            versions.add(version)
            archived = validate_archived_manifest(site / release["manifest"], version, "lcd21")
            if index == 0 and archived != manifest:
                raise SystemExit("První verze katalogu neodpovídá aktuálnímu manifestu.")
            if "manifestLcd7" in release:
                archived7 = validate_archived_manifest(site / release["manifestLcd7"], version, "lcd7")
                if index == 0 and archived7 != lcd7_manifest:
                    raise SystemExit("První verze katalogu neodpovídá aktuálnímu manifestu 7\".")
            elif index == 0 and lcd7_manifest is not None:
                raise SystemExit("Katalog nezná 7\" manifest aktuální verze.")
    boards = "2,1\" i 7\"" if lcd7_manifest is not None else "2,1\""
    print(f"Web i instalační balíček verze {manifest.get('version')} ({boards}) jsou platné.")

if __name__ == "__main__":
    main()
