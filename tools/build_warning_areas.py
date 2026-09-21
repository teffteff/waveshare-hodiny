#!/usr/bin/env python3
"""Sestaví infra/warnings/orp.json: hranice ORP s kódy, podle kterých ČHMÚ
posílá výstrahy.

Výstrahy ČHMÚ (CAP) nesou oblast jen jako seznam kódů CISORP, polygony v nich
nejsou. Hodiny ale znají jen zeměpisnou polohu, takže server potřebuje vědět,
do které ORP poloha padá. Ten soubor se dělá jednou (hranice ORP se mění
zřídka) a jede na server spolu se službou; za běhu se nikam neptá.

Dva zdroje, oba veřejné:

  * hranice ORP z ČÚZK (RÚIAN, prohlížecí služba ArcGIS, vrstva 14), už
    zjednodušené na zhruba 200 m a převedené do WGS84. RÚIAN má vlastní kódy
    ORP (Praha je 19), ne CISORP (Praha je 1100), takže se páruje podle jména;
    jména ORP jsou v republice jedinečná a skript to ověří.
  * kódy CISORP k jménům z archivu CAP souborů ČHMÚ. Když výstraha nepokrývá
    celý kraj, stojí v areaDesc "Kraj (ORP, ORP, ...)" a jména jdou ve stejném
    pořadí jako kódy CISORP pod ní. Skript prochází archiv od největších
    souborů (nejvíc výstrah), dokud nemá všech 206.

Použití: python3 tools/build_warning_areas.py [--output infra/warnings/orp.json]
"""
from __future__ import annotations

import argparse
import html
import json
import re
import sys
import urllib.parse
import urllib.request
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_OUTPUT = ROOT / "infra" / "warnings" / "orp.json"
RUIAN_QUERY = (
    "https://ags.cuzk.gov.cz/arcgis/rest/services/RUIAN/"
    "Prohlizeci_sluzba_nad_daty_RUIAN/MapServer/14/query"
)
CAP_DIRECTORY = "https://opendata.chmi.cz/meteorology/weather/alerts/cap/"
USER_AGENT = "WaveshareHodiny/1.0 (+https://github.com/CooLajz/waveshare-hodiny)"
CAP_NS = "{urn:oasis:names:tc:emergency:cap:1.2}"
ORP_COUNT = 206
# Zjednodušení hranic ve stupních (0,002° je 150 až 220 m) a zaokrouhlení
# souřadnic na desetitisíciny stupně, tedy kolem deseti metrů.
SIMPLIFY_DEGREES = 0.002
SCALE = 10000
# Kolik souborů archivu nejvýš projít, než to skript vzdá.
MAX_CAP_FILES = 60


def fetch(url: str, timeout: int = 120) -> bytes:
    request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(request, timeout=timeout) as response:
        return response.read()


def fetch_boundaries() -> list[dict]:
    query = urllib.parse.urlencode({
        "where": "1=1",
        "outFields": "kod,nazev",
        "returnGeometry": "true",
        "outSR": "4326",
        "maxAllowableOffset": str(SIMPLIFY_DEGREES),
        "geometryPrecision": "4",
        "f": "json",
    })
    payload = json.loads(fetch(f"{RUIAN_QUERY}?{query}"))
    if payload.get("exceededTransferLimit"):
        raise SystemExit("ČÚZK nevrátil všechny ORP najednou")
    return payload["features"]


def cap_files_by_size() -> list[str]:
    """Jména CAP souborů SIVS (XOCZ50) od největšího: víc výstrah, víc dvojic."""
    listing = fetch(CAP_DIRECTORY).decode("utf-8", "replace")
    rows = re.findall(r'href="(alert_cap_50_\d+\.xml)".*?\s(\d+)\s*$', listing, re.M)
    return [name for name, _ in sorted(rows, key=lambda row: -int(row[1]))]


def pairs_from_cap(blob: bytes) -> dict[str, str]:
    """Kód CISORP -> jméno ORP z oblastí, které nepokrývají celý kraj."""
    pairs: dict[str, str] = {}
    root = ET.fromstring(blob)
    for area in root.iter(f"{CAP_NS}area"):
        description = (area.findtext(f"{CAP_NS}areaDesc") or "").strip()
        codes = [
            geocode.findtext(f"{CAP_NS}value")
            for geocode in area.findall(f"{CAP_NS}geocode")
            if geocode.findtext(f"{CAP_NS}valueName") == "CISORP"
        ]
        if description == "Hlavní město Praha" and len(codes) == 1:
            pairs[codes[0]] = "Hlavní město Praha"
            continue
        match = re.fullmatch(r".*? \((.*)\)", description)
        if match is None:
            continue
        names = [name.strip() for name in match.group(1).split(",")]
        if len(names) != len(codes):
            continue
        for name, code in zip(names, codes):
            pairs[code] = html.unescape(name)
    return pairs


def collect_codes(wanted_names: set[str]) -> dict[str, str]:
    """Jméno ORP -> CISORP, dokud nejsou všechna jména z hranic."""
    by_name: dict[str, str] = {}
    for index, name in enumerate(cap_files_by_size()[:MAX_CAP_FILES]):
        for code, orp in pairs_from_cap(fetch(CAP_DIRECTORY + name)).items():
            previous = by_name.get(orp)
            if previous is not None and previous != code:
                raise SystemExit(f"{orp}: CAP uvádí dva kódy, {previous} a {code}")
            by_name[orp] = code
        missing = wanted_names - set(by_name)
        print(f"{name}: {len(by_name)} jmen, chybí {len(missing)}", file=sys.stderr)
        if not missing:
            return by_name
    raise SystemExit(f"Kódy chybí pro: {', '.join(sorted(missing))}")


def compact_ring(ring: list[list[float]]) -> list[int]:
    """Kruh jako ploché pole celých čísel lon,lat,lon,lat v desetitisícinách."""
    flat: list[int] = []
    for lon, lat in ring:
        flat.append(round(lon * SCALE))
        flat.append(round(lat * SCALE))
    return flat


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    args = parser.parse_args()

    features = fetch_boundaries()
    names = [feature["attributes"]["nazev"] for feature in features]
    if len(features) != ORP_COUNT or len(set(names)) != ORP_COUNT:
        raise SystemExit(f"Čekal jsem {ORP_COUNT} ORP s jedinečnými jmény")
    codes = collect_codes(set(names))

    areas = []
    for feature in features:
        name = feature["attributes"]["nazev"]
        rings = [compact_ring(ring) for ring in feature["geometry"]["rings"]]
        lons = [value for ring in rings for value in ring[0::2]]
        lats = [value for ring in rings for value in ring[1::2]]
        areas.append({
            "code": codes[name],
            "name": name,
            "bbox": [min(lons), min(lats), max(lons), max(lats)],
            "rings": rings,
        })
    areas.sort(key=lambda area: area["code"])
    if len({area["code"] for area in areas}) != ORP_COUNT:
        raise SystemExit("Dvě ORP dostaly stejný kód CISORP")

    document = {
        "about": "Hranice ORP (ČÚZK RÚIAN, zjednodušené) s kódy CISORP z CAP ČHMÚ. "
                 "Souřadnice jsou lon,lat v desetitisícinách stupně. "
                 "Generuje tools/build_warning_areas.py.",
        "scale": SCALE,
        "areas": areas,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        json.dumps(document, ensure_ascii=False, separators=(",", ":")) + "\n",
        encoding="utf-8",
    )
    print(f"{args.output}: {len(areas)} ORP, {args.output.stat().st_size} B", file=sys.stderr)


if __name__ == "__main__":
    main()
