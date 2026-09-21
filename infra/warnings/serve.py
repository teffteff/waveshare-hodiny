#!/usr/bin/env python3
"""Vystrahy CHMU pro hodiny, vybrane pro jejich ORP.

CHMU publikuje vystrahy (SIVS, smogove situace) jako CAP XML na

    opendata.chmi.cz/meteorology/weather/alerts/cap/alert_cap_50_DDHHMM.xml

Kazdy soubor je uplny stav, ne zmena: nese vsechny jevy vcetne "zadna
vystraha" a k nim seznam ORP (kody CISORP), pro ktere plati. Novy vychazi pri
kazde aktualizaci, za klidneho pocasi jednou denne. Ma pritom 1,5 az 2,5 MB,
coz je pro hodiny moc: TLS by si vzalo pamet, kterou maji na jine veci, a XML
by se muselo cist po kouskach.

Server proto soubor stahne jednou za vsechny hodiny v domacnosti, polohu hodin
prevede na ORP podle hranic v orp.json (vedle skriptu, generuje ho
tools/build_warning_areas.py) a posle dal par set bajtu: jen vystrahy, ktere se
tyhle ORP tykaji, serazene od nejvaznejsi.

Rozhodnuti "prepnout na radar" tady nepada. Server vraci stupen a cas, firmware
si prah a drzeni bere z vlastniho nastaveni - stejne jako u srazek a blesku.

GET /warnings.json?lat=49.9&lon=14.8&lang=cs
    lat, lon  poloha hodin ve stupnich
    lang      cs (vychozi) nebo en; CAP nese kazdy jev v obou jazycich

Odpoved:
    {"v":1,"time":1789320000,"sent":1789290955,"covered":true,"orp":"2105",
     "area":"Černošice","stale":false,
     "warnings":[{"id":"SIVS X.2","lvl":3,"type":3,"ev":"Velmi silné bouřky",
                  "on":1789297200,"ex":1789336800}]}

    lvl     stupen z parametru awareness_level: 2 zluta, 3 oranzova, 4 cervena
    type    awareness_type (1 vitr, 2 snih a led, 3 bourky, 5 horko, 6 mraz,
            8 pozary, 10 dest, 12 a 13 povodne ...), 0 kdyz chybi
    id      kod jevu ve vystraznem systemu i se stupnem; stejny pri kazde
            aktualizaci tehoz jevu, takze podle nej hodiny poznaji novou
            vystrahu od prodlouzene
    on, ex  platnost v UTC (unixove sekundy); ex = 0 znamena "do odvolani"
    covered poloha lezi v nekterem ORP. Mimo republiku CHMU vystrahy nevydava
            a prazdny seznam tam neznamena klid.
    stale   posledni stazeni se nepovedlo a posila se starsi stav

Nic se nestahuje dopredu: kdyz se zadne hodiny neptaji, server mlci. Bezi pod
systemd jako warnings-web.service a posloucha jen na 127.0.0.1, protoze jedinym
klientem je Caddy na stejnem stroji.
"""
from __future__ import annotations

import calendar
import json
import math
import os
import re
import time
import urllib.error
import urllib.request
import xml.etree.ElementTree as ET
from datetime import datetime
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from threading import Lock
from urllib.parse import parse_qs, urlparse

PORT = int(os.environ.get("WARNINGS_PORT", "8097"))
BIND = os.environ.get("WARNINGS_BIND", "127.0.0.1")
DIRECTORY_URL = os.environ.get(
    "WARNINGS_UPSTREAM", "https://opendata.chmi.cz/meteorology/weather/alerts/cap/"
)
AREAS_PATH = Path(os.environ.get(
    "WARNINGS_AREAS", str(Path(__file__).resolve().with_name("orp.json"))
))
USER_AGENT = os.environ.get(
    "WARNINGS_USER_AGENT",
    "WaveshareHodiny/1.0 (+https://github.com/CooLajz/waveshare-hodiny)",
)
UPSTREAM_TIMEOUT_SECONDS = 30
# Seznam adresare se kontroluje nejvys jednou za pet minut. Novy CAP soubor
# vychazi pri kazde aktualizaci vystrah, ale nikdy casteji nez po par minutach.
CHECK_SECONDS = 300
# Po chybe se posledni stav pujcuje dal. Vystrahy plati hodiny az dny, takze
# pul dne stary stav je porad lepsi nez zadny; stale:true to hodinam rekne.
STALE_SECONDS = 12 * 3600
# Stropy stahovani. Seznam mel pri mereni 21. 9. 2026 70 kB, nejvetsi CAP za
# posledni rok 2,4 MB.
MAX_LISTING_BYTES = 2 * 1024 * 1024
MAX_CAP_BYTES = 16 * 1024 * 1024
MAX_WARNINGS = 6
MAX_EVENT_LENGTH = 60
# Bod kousek za hranici ORP (zjednodusene hranice, poloha zadana s malou
# presnosti) se jeste priradi nejblizsi ORP. Dal uz jde o cizinu.
NEAREST_TOLERANCE_KM = 3.0

CAP_NS = "{urn:oasis:names:tc:emergency:cap:1.2}"
# Soubory SIVS a smogovych situaci (bulletin XOCZ50). Soubory _70_ jsou
# tydenni prehled a vystrahy nenesou.
CAP_NAME = re.compile(r"alert_cap_50_\d{6}\.xml")
LISTING_ROW = re.compile(
    r'href="(alert_cap_50_\d{6}\.xml)">[^<]*</a>\s+(\d{2}-[A-Za-z]{3}-\d{4} \d{2}:\d{2})'
)

# Znaky, ktere hodiny umi napsat (pismo clock_czech_*): ASCII, stupen, horni
# trojka, mikro, ceska diakritika a dolni dvojka. Ostatni se nahradi nebo zahodi.
FONT_CHARACTERS = set(chr(code) for code in range(0x20, 0x7F)) | set(
    "°³µÁÉÍÓÚÝáéíóúýČčĎďĚěŇňŘřŠšŤťŮůŽž₂"
)
REPLACEMENTS = {
    "\u2013": "-", "\u2014": "-", "\u2212": "-", "\u00a0": " ", "\u2009": " ",
    "\u201e": '"', "\u201c": '"', "\u201d": '"', "\u2019": "'", "\u00b7": "|",
    "\u2026": "...",
}


def font_safe(text: str, limit: int = MAX_EVENT_LENGTH) -> str:
    out = []
    for character in " ".join(text.split()):
        character = REPLACEMENTS.get(character, character)
        out.extend(c for c in character if c in FONT_CHARACTERS)
    return "".join(out)[:limit].rstrip()


def parse_time(value: str | None) -> int:
    """ISO 8601 s posunem (2026-06-28T22:51:35+02:00) na unixove sekundy."""
    if not value:
        return 0
    try:
        return int(datetime.fromisoformat(value.strip()).timestamp())
    except ValueError:
        return 0


# --- Oblasti ------------------------------------------------------------------
class Areas:
    """ORP jako polygony; bod se hleda nejdriv podle obdelniku, pak paprskem."""

    def __init__(self, path: Path) -> None:
        document = json.loads(path.read_text(encoding="utf-8"))
        self.scale = float(document["scale"])
        self.areas = document["areas"]

    @staticmethod
    def _inside_ring(ring: list[int], x: int, y: int) -> bool:
        inside = False
        count = len(ring) // 2
        j = count - 1
        for i in range(count):
            xi, yi = ring[2 * i], ring[2 * i + 1]
            xj, yj = ring[2 * j], ring[2 * j + 1]
            if (yi > y) != (yj > y) and x < (xj - xi) * (y - yi) / (yj - yi) + xi:
                inside = not inside
            j = i
        return inside

    def locate(self, latitude: float, longitude: float) -> dict | None:
        x = round(longitude * self.scale)
        y = round(latitude * self.scale)
        for area in self.areas:
            west, south, east, north = area["bbox"]
            if not (west <= x <= east and south <= y <= north):
                continue
            # Sude-liche pravidlo pres vsechny kruhy: dira v ORP (enklava) je
            # druhy kruh uvnitr prvniho.
            inside = False
            for ring in area["rings"]:
                if self._inside_ring(ring, x, y):
                    inside = not inside
            if inside:
                return area
        return self._nearest(latitude, longitude)

    def _nearest(self, latitude: float, longitude: float) -> dict | None:
        """Nejblizsi ORP do NEAREST_TOLERANCE_KM podle vrcholu hranice."""
        cos_lat = math.cos(math.radians(latitude))
        limit = NEAREST_TOLERANCE_KM / 111.0 * self.scale
        x = longitude * self.scale
        y = latitude * self.scale
        best = None
        best_distance = limit
        for area in self.areas:
            west, south, east, north = area["bbox"]
            if (x < west - limit / cos_lat or x > east + limit / cos_lat
                    or y < south - limit or y > north + limit):
                continue
            for ring in area["rings"]:
                for index in range(0, len(ring), 2):
                    distance = math.hypot((ring[index] - x) * cos_lat, ring[index + 1] - y)
                    if distance < best_distance:
                        best_distance = distance
                        best = area
        return best


# --- CAP ----------------------------------------------------------------------
def _text(element: ET.Element, tag: str) -> str:
    return (element.findtext(f"{CAP_NS}{tag}") or "").strip()


def _parameters(info: ET.Element) -> dict[str, str]:
    return {
        _text(parameter, "valueName"): _text(parameter, "value")
        for parameter in info.findall(f"{CAP_NS}parameter")
    }


def _leading_int(value: str) -> int:
    """"3; orange; Severe" -> 3."""
    match = re.match(r"\s*(\d+)", value or "")
    return int(match.group(1)) if match else 0


def parse_cap(blob: bytes) -> tuple[int, list[dict]]:
    """Cas vydani a seznam vystrah (bez "zadna vystraha" a bez vyhledu).

    Bloky info jdou v parech: cesky a hned za nim anglicky pro Meteoalarm.
    Anglicky text se vezme z nasledujiciho bloku en-GB, pokud existuje.
    """
    root = ET.fromstring(blob)
    sent = parse_time(_text(root, "sent"))
    infos = root.findall(f"{CAP_NS}info")
    warnings: list[dict] = []
    for index, info in enumerate(infos):
        if not _text(info, "language").startswith("cs"):
            continue
        severity = _text(info, "severity")
        if severity in ("Minor", "Unknown", ""):
            continue
        parameters = _parameters(info)
        level = _leading_int(parameters.get("awareness_level", ""))
        if level < 2:
            continue
        codes = [
            (_text(code, "valueName"), _text(code, "value"))
            for code in info.findall(f"{CAP_NS}eventCode")
        ]
        # Vyhled nebezpecnych jevu neni vystraha, jen upozorneni na dalsi dny.
        if any(value == "OUTLOOK" for _, value in codes):
            continue
        system, code = codes[0] if codes else ("", _text(info, "event"))
        english = ""
        if index + 1 < len(infos) and _text(infos[index + 1], "language").startswith("en"):
            english = _text(infos[index + 1], "event")
        orps = {
            _text(geocode, "value")
            for area in info.findall(f"{CAP_NS}area")
            for geocode in area.findall(f"{CAP_NS}geocode")
            if _text(geocode, "valueName") == "CISORP"
        }
        if not orps:
            continue
        warnings.append({
            "id": f"{system} {code}".strip(),
            "lvl": level,
            "type": _leading_int(parameters.get("awareness_type", "")),
            "cs": _text(info, "event"),
            "en": english,
            "on": parse_time(_text(info, "onset")) or parse_time(_text(info, "effective")) or sent,
            "ex": parse_time(_text(info, "expires")),
            "orps": orps,
        })
    return sent, warnings


def newest_cap_name(listing: str) -> str | None:
    """Nejnovejsi soubor podle casu v seznamu adresare.

    Jmeno nese jen den v mesici a cas (DDHHMM), takze se mesic co mesic
    opakuje; o poradi rozhoduje az datum zmeny, ktere nginx vypisuje.
    """
    best: tuple[int, str] | None = None
    for name, stamp in LISTING_ROW.findall(listing):
        try:
            moment = calendar.timegm(time.strptime(stamp, "%d-%b-%Y %H:%M"))
        except ValueError:
            continue
        if best is None or (moment, name) > best:
            best = (moment, name)
    return best[1] if best else None


def _download(url: str, limit: int) -> bytes:
    request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(request, timeout=UPSTREAM_TIMEOUT_SECONDS) as response:
        blob = response.read(limit + 1)
    if len(blob) > limit:
        raise ValueError("odpoved je vetsi, nez ma byt")
    return blob


class Feed:
    """Posledni stav vystrah, sdileny vsemi hodinami."""

    def __init__(self) -> None:
        self._lock = Lock()
        self._name: str | None = None
        self._sent = 0
        self._warnings: list[dict] = []
        self._checked = 0.0
        self._loaded = 0.0
        self._failures = 0

    def snapshot(self, now: float) -> tuple[int, list[dict], bool, bool]:
        """(cas vydani, vystrahy, mame data, jsou stara)."""
        # Zamek i pres stahovani: hodiny v domacnosti se ptaji skoro naraz
        # a stahovat dvakrat tentyz dvoumegovy soubor nema smysl.
        with self._lock:
            if now - self._checked >= CHECK_SECONDS or self._name is None:
                self._checked = now
                try:
                    self._refresh()
                    self._loaded = now
                    self._failures = 0
                except (urllib.error.URLError, OSError, ValueError, ET.ParseError):
                    self._failures += 1
            have = self._name is not None and now - self._loaded < STALE_SECONDS
            stale = self._failures > 0
            return self._sent, self._warnings if have else [], have, stale

    def _refresh(self) -> None:
        listing = _download(DIRECTORY_URL, MAX_LISTING_BYTES).decode("utf-8", "replace")
        name = newest_cap_name(listing)
        if name is None or not CAP_NAME.fullmatch(name):
            raise ValueError("v adresari neni zadny CAP soubor")
        if name == self._name:
            return
        sent, warnings = parse_cap(_download(DIRECTORY_URL + name, MAX_CAP_BYTES))
        self._name = name
        self._sent = sent
        self._warnings = warnings

    def status(self, now: float) -> dict:
        with self._lock:
            return {
                "file": self._name,
                "sent": self._sent,
                "warnings": len(self._warnings),
                "checkedAge": round(now - self._checked, 1) if self._checked else None,
                "loadedAge": round(now - self._loaded, 1) if self._loaded else None,
                "failures": self._failures,
            }


areas: Areas | None = None
feed = Feed()


def build_answer(latitude: float, longitude: float, english: bool,
                 now: float) -> dict | None:
    sent, warnings, have, stale = feed.snapshot(now)
    if not have:
        return None
    area = areas.locate(latitude, longitude) if areas is not None else None
    answer = {
        "v": 1,
        "time": round(now),
        "sent": sent,
        "covered": area is not None,
        "orp": area["code"] if area else "",
        "area": font_safe(area["name"]) if area else "",
        "stale": stale,
        "warnings": [],
    }
    if area is None:
        return answer
    relevant = [
        warning for warning in warnings
        if area["code"] in warning["orps"] and (warning["ex"] == 0 or warning["ex"] > now)
    ]
    # Nejvaznejsi prvni, pri stejnem stupni ta, ktera plati driv.
    relevant.sort(key=lambda warning: (-warning["lvl"], warning["on"], warning["id"]))
    for warning in relevant[:MAX_WARNINGS]:
        text = warning["en"] if english and warning["en"] else warning["cs"]
        answer["warnings"].append({
            "id": font_safe(warning["id"], 24),
            "lvl": warning["lvl"],
            "type": warning["type"],
            "ev": font_safe(text),
            "on": warning["on"],
            "ex": warning["ex"],
        })
    return answer


class Handler(BaseHTTPRequestHandler):
    server_version = "warnings-web/1.0"
    # ThreadingHTTPServer zaklada vlakno na spojeni. Bez timeoutu staci pomale
    # otevrene spojeni, aby se vlakna nahromadila.
    timeout = 30

    def _send(self, code: int, body: bytes = b"",
              ctype: str = "text/plain; charset=utf-8") -> None:
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-cache")
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(body)

    def _json(self, code: int, payload: dict) -> None:
        body = json.dumps(payload, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
        self._send(code, body, "application/json; charset=utf-8")

    def do_GET(self) -> None:  # noqa: N802
        if len(self.path) > 512:
            self._send(414, b"uri too long\n")
            return
        parsed = urlparse(self.path)
        now = time.time()
        if parsed.path == "/warnings/status":
            # Caddy tuhle cestu ven nepousti; je pro check-stack pres SSH.
            self._json(200, feed.status(now))
            return
        if parsed.path not in ("/warnings.json", "/"):
            self._send(404, b"not found\n")
            return
        query = parse_qs(parsed.query)
        try:
            latitude = float(query.get("lat", [""])[0])
            longitude = float(query.get("lon", [""])[0])
        except (TypeError, ValueError):
            self._send(400, b"expected lat and lon\n")
            return
        if not -90.0 <= latitude <= 90.0 or not -180.0 <= longitude <= 180.0:
            self._send(400, b"latitude or longitude out of range\n")
            return
        english = query.get("lang", ["cs"])[0].lower().startswith("en")
        answer = build_answer(latitude, longitude, english, now)
        if answer is None:
            self._send(502, b"upstream unavailable\n")
            return
        self._json(200, answer)

    do_HEAD = do_GET

    def log_message(self, *args):  # tise, at neplni journal
        pass


if __name__ == "__main__":
    areas = Areas(AREAS_PATH)
    ThreadingHTTPServer((BIND, PORT), Handler).serve_forever()
