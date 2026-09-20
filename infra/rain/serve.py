#!/usr/bin/env python3
"""Prevypravec srazkove predpovedi CHMU pro hodiny.

Hodiny umi ukazat meteoradar, ale samy nevedi, jestli se dest blizi. Odpoved
na to uz ale nekdo pocita: CHMU vedle aktualni kompozice publikuje i vlastni
extrapolaci

    composite/fct_maxz/png/pacz2gmaps3.fct_z_max.YYYYMMDD.HHMM.ft60s10.tar

tedy tar se sesti snimky na +10 az +60 minut, novy kazdych pet minut. Snimky
maji presne tutez geometrii i paletu jako kompozice maxz, kterou firmware uz
dneska dekoduje - 680x460, osmibitova paleta, stejna projekce.

Hodiny by to zvladly taky, jenze draho: 235 kB taru kazdych pet minut, sest
dekodovanych PNG navic v PSRAM a korelace, ktera by musela uprostred uvolnovat
watchdog. Tenhle server proto stahne tar jednou za vsechny hodiny v domacnosti,
precte z kazdeho snimku okoli jejich polohy a posle dal dve stovky bajtu.

Rozhodnuti "prepnout obrazovku" tady nepadne. Server vraci jen namerenou
odrazivost v dBZ a firmware si prah, dohled i prodlevu bere z vlastniho
nastaveni - stejne jako u blesku, kde server vozi udery a poplach vyhlasuji
hodiny.

GET /rain.json?lat=49.9&lon=14.8&r=5
    lat, lon  poloha hodin ve stupnich
    r         nepovinny polomer okoli v km (1 az 30, vychozi 5). Bere se
              maximum pres ctverec, aby jediny sumivy pixel nespustil poplach.

Odpoved:
    {"time":1789320000,"slot":1789319400,"covered":true,"step":10,
     "now":24,"steps":[16,16,16,12,12,20]}

    now, steps  odrazivost v dBZ; 0 znamena "zadny odraz", -1 "snimek chybi".
    slot        cas analyzy, ze ktere predpoved vysla (UTC, unixove sekundy).
                Hodiny z nej poznaji, ze server vozi stara data.
    covered     poloha lezi v dosahu ceskych radaru. Mimo nej je snimek prazdny
                proto, ze tam radar nevidi, ne proto, ze neprsi.

Nic se nestahuje dopredu: kdyz se zadne hodiny neptaji, server mlci. Bezi pod
systemd jako rain-web.service a posloucha jen na 127.0.0.1, protoze jedinym
klientem je Caddy na stejnem stroji.
"""
from __future__ import annotations

import io
import json
import math
import os
import struct
import tarfile
import time
import urllib.error
import urllib.request
import zlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from threading import Lock
from urllib.parse import parse_qs, urlparse

PORT = int(os.environ.get("RAIN_PORT", "8096"))
BIND = os.environ.get("RAIN_BIND", "127.0.0.1")
BASE_URL = os.environ.get(
    "RAIN_UPSTREAM", "https://opendata.chmi.cz/meteorology/weather/radar/composite"
)
# CHMU prosi, at se volajici predstavi, stejne jako adsb.fi u letadel.
USER_AGENT = os.environ.get(
    "RAIN_USER_AGENT",
    "WaveshareHodiny/1.0 (+https://github.com/CooLajz/waveshare-hodiny)",
)
UPSTREAM_TIMEOUT_SECONDS = 20
# Pet minut je krok publikace; ctyri znamenaji, ze si hodiny na novy snimek
# nikdy nepockaji zbytecne dlouho a zaroven se tar nestahuje dvakrat za slot.
CACHE_SECONDS = 240
# Po chybe se posledni snimky pujcuji jeste chvili dal. Prazdna obloha po
# jednom nepovedenem stazeni vypada jako pravda, ale neni.
STALE_SECONDS = 1200
SLOT_SECONDS = 300
# Kolik slotu zpet se hleda ten nejnovejsi publikovany. Stejne jako ve firmwaru
# (NEWEST_SLOT_PROBES): tar byva hotovy tri az ctyri minuty po case analyzy.
NEWEST_SLOT_PROBES = 4
# Stropy stahovani. Tar mel pri mereni 20. 9. 2026 230 kB, kompozice 40 kB;
# nic vetsiho z tehle adresy neprijde a server na to nema cekat.
MAX_TAR_BYTES = 4 * 1024 * 1024
MAX_PNG_BYTES = 1024 * 1024

# Projekce kompozice CHMU. Cisla jsou tataz jako v ChmiRadarService.cpp - obraz
# je linearni v zemepisne delce a Mercator v sirce.
LON_LEFT = 11.267
LON_RIGHT = 20.770
LAT_TOP = 52.167
LAT_BOTTOM = 48.047
# Obraz neni jen mapa: podel horniho a praveho okraje lezi svisle rezy
# ("CZRAD - Z: MAX"). Data konci presne tady a firmware si je maskuje stejne.
LON_DATA_RIGHT = 19.624
LAT_DATA_TOP = 51.458

# Paleta je sama stupnice intenzity. Index 182 je pasmo 56-60 dBZ a kazdy dalsi
# index je o ctyri dBZ niz, az po 195 = 4-8 dBZ; index 0 znamena zadny odraz.
# Overeno proti scl/scl-dbz-mmh.png, kterou CHMU publikuje vedle dat: pasma
# v ni maji po patnacti pixelech a popisky jdou po ctyrech dBZ.
DBZ_FIRST_INDEX = 182
DBZ_LAST_INDEX = 195
DBZ_FIRST_VALUE = 56
DBZ_PER_INDEX = 4

# Ceske radary a jejich dosah. Mimo jejich dosah je snimek prazdny proto, ze
# tam nikdo nemeri; hodinam se to rekne, at prazdno nehlasi jako sucho.
RADAR_SITES = ((49.6584, 13.8178), (49.5011, 16.7885))
RADAR_RANGE_KM = 250.0

DEFAULT_RADIUS_KM = 5
MAX_RADIUS_KM = 30
# Kolik minut dopredu tar nese a s jakym krokem. Z nazvu ft60s10.
FORECAST_STEP_MINUTES = 10
FORECAST_LEADS = (10, 20, 30, 40, 50, 60)


def mercator_y(latitude: float) -> float:
    return math.log(math.tan(math.pi / 4 + math.radians(latitude) / 2))


def project(latitude: float, longitude: float, width: int, height: int) -> tuple[int, int]:
    x = round((longitude - LON_LEFT) * (width - 1) / (LON_RIGHT - LON_LEFT))
    top = mercator_y(LAT_TOP)
    bottom = mercator_y(LAT_BOTTOM)
    y = round((top - mercator_y(latitude)) * (height - 1) / (top - bottom))
    return x, y


def data_bounds(width: int, height: int) -> tuple[int, int]:
    """Prava a horni hranice mapovych dat v pixelech; za nimi lezi svisle rezy."""
    return project(LAT_DATA_TOP, LON_DATA_RIGHT, width, height)


def dbz_from_index(index: int) -> int:
    if index < DBZ_FIRST_INDEX or index > DBZ_LAST_INDEX:
        return 0
    return DBZ_FIRST_VALUE - (index - DBZ_FIRST_INDEX) * DBZ_PER_INDEX


def haversine_km(lat1: float, lon1: float, lat2: float, lon2: float) -> float:
    radius = 6371.0
    dlat = math.radians(lat2 - lat1)
    dlon = math.radians(lon2 - lon1)
    a = (math.sin(dlat / 2) ** 2
         + math.cos(math.radians(lat1)) * math.cos(math.radians(lat2)) * math.sin(dlon / 2) ** 2)
    return 2 * radius * math.asin(math.sqrt(a))


def in_radar_range(latitude: float, longitude: float) -> bool:
    return any(haversine_km(latitude, longitude, lat, lon) <= RADAR_RANGE_KM
               for lat, lon in RADAR_SITES)


class Frame:
    """Jeden dekodovany snimek: indexy palety, bez filtru a bez prokladu."""

    __slots__ = ("width", "height", "pixels")

    def __init__(self, width: int, height: int, pixels: bytes):
        self.width = width
        self.height = height
        self.pixels = pixels

    def index_at(self, x: int, y: int) -> int:
        return self.pixels[y * self.width + x]

    def peak_dbz(self, x: int, y: int, radius_px: int) -> int:
        """Nejsilnejsi odraz ve ctverci kolem bodu, orezany na mapovou cast."""
        right, top = data_bounds(self.width, self.height)
        x0 = max(0, x - radius_px)
        x1 = min(right, x + radius_px)
        y0 = max(top, y - radius_px)
        y1 = min(self.height - 1, y + radius_px)
        best = 0
        for row in range(y0, y1 + 1):
            base = row * self.width
            chunk = self.pixels[base + x0:base + x1 + 1]
            # Nizsi index je silnejsi odraz, takze staci minimum z platnych.
            for index in chunk:
                if DBZ_FIRST_INDEX <= index <= DBZ_LAST_INDEX:
                    value = dbz_from_index(index)
                    if value > best:
                        best = value
        return best


def decode_palette_png(blob: bytes) -> Frame:
    """Osmibitove PNG s paletou na indexy palety. Jen stdlib, zadne Pillow.

    Snimky CHMU jsou neprokladane a vsechny radky maji filtr 0, takze je
    dekodovani jen rozbaleni a odstraneni vodicich bajtu. Filtry 1 az 4 jsou tu
    presto: kdyby CHMU prehodilo enkoder, ma se sluzba ohnout, ne spadnout.
    """
    if blob[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("neni PNG")
    offset = 8
    width = height = 0
    idat = bytearray()
    while offset + 8 <= len(blob):
        length = struct.unpack(">I", blob[offset:offset + 4])[0]
        kind = blob[offset + 4:offset + 8]
        data = blob[offset + 8:offset + 8 + length]
        if kind == b"IHDR":
            width, height, depth, color, _, _, interlace = struct.unpack(">IIBBBBB", data[:13])
            if depth != 8 or color != 3:
                raise ValueError("ocekavana osmibitova paleta")
            if interlace:
                raise ValueError("prokladane PNG")
        elif kind == b"IDAT":
            idat += data
        elif kind == b"IEND":
            break
        offset += 12 + length
    if width <= 0 or height <= 0:
        raise ValueError("chybi IHDR")
    raw = zlib.decompress(bytes(idat))
    if len(raw) < height * (width + 1):
        raise ValueError("neuplna data")
    out = bytearray(width * height)
    previous = bytearray(width)
    pos = 0
    for row in range(height):
        method = raw[pos]
        pos += 1
        line = bytearray(raw[pos:pos + width])
        pos += width
        if method == 1:
            for x in range(1, width):
                line[x] = (line[x] + line[x - 1]) & 0xFF
        elif method == 2:
            for x in range(width):
                line[x] = (line[x] + previous[x]) & 0xFF
        elif method == 3:
            for x in range(width):
                left = line[x - 1] if x else 0
                line[x] = (line[x] + ((left + previous[x]) >> 1)) & 0xFF
        elif method == 4:
            for x in range(width):
                left = line[x - 1] if x else 0
                up = previous[x]
                corner = previous[x - 1] if x else 0
                estimate = left + up - corner
                da, db, dc = abs(estimate - left), abs(estimate - up), abs(estimate - corner)
                if da <= db and da <= dc:
                    nearest = left
                elif db <= dc:
                    nearest = up
                else:
                    nearest = corner
                line[x] = (line[x] + nearest) & 0xFF
        elif method != 0:
            raise ValueError(f"neznamy filtr {method}")
        out[row * width:(row + 1) * width] = line
        previous = line
    return Frame(width, height, bytes(out))


def slot_at(moment: float) -> int:
    return int(moment) - int(moment) % SLOT_SECONDS


def stamp(slot: int) -> str:
    return time.strftime("%Y%m%d.%H%M", time.gmtime(slot))


def _download(url: str, limit: int) -> bytes:
    request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(request, timeout=UPSTREAM_TIMEOUT_SECONDS) as response:
        blob = response.read(limit + 1)
    if len(blob) > limit:
        raise ValueError("odpoved je vetsi, nez ma byt")
    return blob


def fetch_forecast(slot: int) -> dict[int, Frame]:
    """Snimky +10 az +60 z taru jedne analyzy, klicovane predstihem v minutach."""
    url = f"{BASE_URL}/fct_maxz/png/pacz2gmaps3.fct_z_max.{stamp(slot)}.ft60s10.tar"
    blob = _download(url, MAX_TAR_BYTES)
    frames: dict[int, Frame] = {}
    with tarfile.open(fileobj=io.BytesIO(blob), mode="r:") as archive:
        for member in archive.getmembers():
            if not member.isfile() or not member.name.endswith(".png"):
                continue
            # pacz2gmaps3.fct_z_max.20260920.1450.10.png -> predstih je "10"
            parts = member.name.split(".")
            if len(parts) < 3:
                continue
            try:
                lead = int(parts[-2])
            except ValueError:
                continue
            handle = archive.extractfile(member)
            if handle is None:
                continue
            frames[lead] = decode_palette_png(handle.read())
    return frames


def fetch_composite(slot: int) -> Frame:
    url = f"{BASE_URL}/maxz/png/pacz2gmaps3.z_max3d.{stamp(slot)}.0.png"
    return decode_palette_png(_download(url, MAX_PNG_BYTES))


class Radar:
    """Nejnovejsi analyza a jeji extrapolace, sdilena vsemi hodinami."""

    def __init__(self) -> None:
        self._lock = Lock()
        self._slot = 0
        self._fetched = 0.0
        self._now: Frame | None = None
        self._steps: dict[int, Frame] = {}
        self._failures = 0

    def snapshot(self, now: float) -> tuple[int, Frame | None, dict[int, Frame], str]:
        # Zamek se drzi i pres stahovani. Je to zamerne: hodiny v domacnosti se
        # budi po petiminutovem kroku skoro naraz a bez nej by kazde spustily
        # vlastni stazeni tehoz taru. Takhle stahuje prvni a ostatni si pockaji.
        with self._lock:
            age = now - self._fetched
            if self._now is not None and age < CACHE_SECONDS:
                return self._slot, self._now, self._steps, "cache"
            try:
                slot, current, steps = self._load(now)
            except (urllib.error.URLError, OSError, ValueError, tarfile.TarError, zlib.error):
                self._failures += 1
                if self._now is not None and age < STALE_SECONDS:
                    return self._slot, self._now, self._steps, "stale"
                return 0, None, {}, "error"
            self._slot = slot
            self._now = current
            self._steps = steps
            self._fetched = now
            self._failures = 0
            return slot, current, steps, "fresh"

    def _load(self, now: float) -> tuple[int, Frame, dict[int, Frame]]:
        newest = slot_at(now)
        last_error: Exception = ValueError("zadny slot")
        for probe in range(NEWEST_SLOT_PROBES):
            slot = newest - probe * SLOT_SECONDS
            try:
                steps = fetch_forecast(slot)
            except (urllib.error.URLError, OSError, ValueError, tarfile.TarError, zlib.error) as error:
                last_error = error
                continue
            if not steps:
                continue
            # Kompozice tehoz slotu je "prave ted". Kdyby chybela, bere se
            # o slot starsi: pet minut stare "ted" je porad lepsi nez zadne.
            current = None
            for back in range(2):
                try:
                    current = fetch_composite(slot - back * SLOT_SECONDS)
                    break
                except (urllib.error.URLError, OSError, ValueError, zlib.error) as error:
                    last_error = error
            if current is None:
                continue
            # Vsechny snimky se vzorkuji tymiz souradnicemi, takze se jejich
            # geometrie nesmi rozejit. Kdyby CHMU zmenilo rozmer jen u jednoho
            # produktu, vzorkovalo by se vedle - a to je horsi nez chyba.
            steps = {lead: frame for lead, frame in steps.items()
                     if (frame.width, frame.height) == (current.width, current.height)}
            if not steps:
                continue
            return slot, current, steps
        raise last_error

    def status(self, now: float) -> dict:
        with self._lock:
            return {
                "slot": self._slot,
                "age": round(now - self._fetched, 1) if self._fetched else None,
                "leads": sorted(self._steps),
                "failures": self._failures,
            }


radar = Radar()


def build_answer(latitude: float, longitude: float, radius_km: int, now: float) -> tuple[dict | None, str]:
    slot, current, steps, source = radar.snapshot(now)
    if current is None:
        return None, source
    x, y = project(latitude, longitude, current.width, current.height)
    right, top = data_bounds(current.width, current.height)
    inside = 0 <= x <= right and top <= y < current.height
    covered = inside and in_radar_range(latitude, longitude)
    if not inside:
        return {"time": round(now), "slot": slot, "covered": False, "step": FORECAST_STEP_MINUTES,
                "now": -1, "steps": [-1] * len(FORECAST_LEADS)}, source
    # Jeden pixel je zhruba kilometr v obou osach, takze se polomer v km da
    # vzit rovnou jako polomer v pixelech.
    radius_px = max(0, radius_km)
    answer = {
        "time": round(now),
        "slot": slot,
        "covered": covered,
        "step": FORECAST_STEP_MINUTES,
        "now": current.peak_dbz(x, y, radius_px),
        "steps": [steps[lead].peak_dbz(x, y, radius_px) if lead in steps else -1
                  for lead in FORECAST_LEADS],
    }
    return answer, source


class Handler(BaseHTTPRequestHandler):
    server_version = "rain-web/1.0"
    # ThreadingHTTPServer zaklada vlakno na spojeni. Bez timeoutu staci pomale
    # otevrene spojeni, aby se vlakna nahromadila.
    timeout = 30

    def _send(self, code: int, body: bytes = b"", ctype: str = "text/plain; charset=utf-8",
              source: str = "") -> None:
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-cache")
        if source:
            # Jen pro cloveka s curlem; hodiny hlavicku nectou.
            self.send_header("X-Rain-Source", source)
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(body)

    def _json(self, code: int, payload: dict, source: str = "") -> None:
        body = json.dumps(payload, separators=(",", ":")).encode("utf-8")
        self._send(code, body, "application/json; charset=utf-8", source)

    def do_GET(self) -> None:  # noqa: N802
        parsed = urlparse(self.path)
        now = time.time()
        if parsed.path == "/rain/status":
            self._json(200, radar.status(now))
            return
        if parsed.path not in ("/rain.json", "/"):
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
        try:
            radius_km = int(float(query.get("r", [DEFAULT_RADIUS_KM])[0]))
        except (TypeError, ValueError):
            self._send(400, b"r must be a number of kilometres\n")
            return
        radius_km = max(1, min(MAX_RADIUS_KM, radius_km))
        answer, source = build_answer(latitude, longitude, radius_km, now)
        if answer is None:
            self._send(502, b"upstream unavailable\n", source=source)
            return
        self._json(200, answer, source)

    do_HEAD = do_GET

    def log_message(self, *args):  # tise, at neplni journal
        pass


if __name__ == "__main__":
    ThreadingHTTPServer((BIND, PORT), Handler).serve_forever()
