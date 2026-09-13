"""Registr poloh, pro ktere se vybiraji mistni zpravy.

Sdileji ho serve.py (zapisuje polohy, na ktere se hodiny ptaji) a generate.py
(pro kazdou zivou polohu udela vlastni vyber). Kazda poloha je jeden maly soubor
JSON, jehoz mtime znamena "naposledy se na ni nekdo ptal". Oba procesy tak
nepotrebuji zamek: server jen pridava a osahava, generator jen maze prosle.

Server visi na verejne IP, takze registr ma strop (NEWS_MAX_LOCATIONS). Kdo ho
zaplni vymyslenymi polohami, zpusobi jen to, ze nove hodiny dostanou spolecny
vyber, dokud cizi polohy neprosehnou - a zaplati nejvys tolik volani modelu na
beh, kolik je mist. Jmeno mista se modelu predava jako data; vyber pro podvrzene
jmeno ale dostane jen ten, kdo se na nej ptal, protoze je soucasti klice.
"""

from __future__ import annotations

import hashlib
import json
import math
import os
import time
import unicodedata
from dataclasses import dataclass
from pathlib import Path

STATE = Path(os.environ.get("NEWS_STATE", "/opt/news/state/locations"))
MAX_LOCATIONS = int(os.environ.get("NEWS_MAX_LOCATIONS", "4"))
# Hodiny se ptaji kazdych nekolik minut, takze tyden bez dotazu znamena, ze
# hodiny zmenily polohu nebo adresu, ne ze byly chvili bez site.
TTL_SECONDS = float(os.environ.get("NEWS_LOCATION_TTL_DAYS", "7")) * 86400
# Osahavat soubor pri kazdem dotazu by zbytecne psalo na disk.
TOUCH_INTERVAL_SECONDS = 3600
MAX_CITY_CHARS = 64


@dataclass(frozen=True)
class Location:
    city: str
    # Zaokrouhleno na desetinu stupne (kolem 10 km): na vyber zprav to staci,
    # dvoje hodiny v jednom meste sdili jeden vyber a server si presnou polohu
    # domacnosti neuklada.
    lat: float
    lon: float

    @property
    def key(self) -> str:
        raw = f"{self.lat:.1f},{self.lon:.1f},{self.city.casefold()}"
        return hashlib.sha256(raw.encode("utf-8")).hexdigest()[:16]

    def describe(self) -> str:
        coordinates = f"{self.lat:.1f}, {self.lon:.1f}"
        return f"{self.city} ({coordinates})" if self.city else coordinates


def clean_city(value: str) -> str:
    # Jen pismena, cislice a par oddelovacu, ktere se ve jmenech mist opravdu
    # vyskytuji. Zbytek (ridici znaky, uvozovky, zavorky promptu) se zahodi.
    # Hledání místa v nastavení hodin ukládá celý řetězec
    # "Ondřejov · Praha-východ · Středočeský kraj · Česko"; tečka uprostřed
    # je oddělovač, ne ozdoba, a bez ní by z kraje a obce slil jeden název.
    kept = []
    for char in unicodedata.normalize("NFC", value.replace("·", ",")):
        category = unicodedata.category(char)
        if category[0] in "LN" or char in " -.,'/":
            kept.append(char)
        elif category[0] == "Z":
            kept.append(" ")
    collapsed = " ".join("".join(kept).split()).replace(" ,", ",")
    return collapsed[:MAX_CITY_CHARS]


def location_from_query(query: dict[str, list[str]]) -> Location | None:
    try:
        lat = float(query["lat"][0])
        lon = float(query["lon"][0])
    except (KeyError, IndexError, ValueError):
        return None
    if not (math.isfinite(lat) and math.isfinite(lon)):
        return None
    if not (-90 <= lat <= 90 and -180 <= lon <= 180):
        return None
    city = clean_city(query.get("city", [""])[0])
    return Location(city=city, lat=round(lat, 1), lon=round(lon, 1))


def location_output(www: Path, location: Location) -> Path:
    return www / f"top-{location.key}.xml"


def _mtime(path: Path) -> float:
    # Generator muze soubor smazat mezi vypisem adresare a stat(); smazana
    # poloha se pocita jako davno prosla.
    try:
        return path.stat().st_mtime
    except FileNotFoundError:
        return 0.0


def _read(path: Path) -> Location | None:
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
        return Location(city=str(data["city"]), lat=float(data["lat"]), lon=float(data["lon"]))
    except (OSError, ValueError, KeyError, TypeError):
        return None


def register(location: Location) -> None:
    path = STATE / f"{location.key}.json"
    now = time.time()
    try:
        if now - path.stat().st_mtime > TOUCH_INTERVAL_SECONDS:
            os.utime(path)
        return
    except FileNotFoundError:
        pass
    STATE.mkdir(parents=True, exist_ok=True)
    live = sum(1 for other in STATE.glob("*.json") if now - _mtime(other) <= TTL_SECONDS)
    if live >= MAX_LOCATIONS:
        return
    temporary = path.with_suffix(".tmp")
    temporary.write_text(
        json.dumps({"city": location.city, "lat": location.lat, "lon": location.lon},
                   ensure_ascii=False),
        encoding="utf-8",
    )
    os.replace(temporary, path)


def active_locations(www: Path) -> list[Location]:
    """Vrati zive polohy; prosle smaze i s jejich vyberem."""
    if not STATE.is_dir():
        return []
    now = time.time()
    result = []
    for path in sorted(STATE.glob("*.json")):
        location = _read(path)
        if location is None or now - _mtime(path) > TTL_SECONDS:
            path.unlink(missing_ok=True)
            if location is not None:
                location_output(www, location).unlink(missing_ok=True)
            continue
        result.append(location)
    return result[:MAX_LOCATIONS]
