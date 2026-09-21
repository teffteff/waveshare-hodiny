#!/usr/bin/env python3
"""Druzice nad hlavou pro hodiny: drahy z CelesTraku, poloha na obloze z SGP4.

Hodiny se ptaji GET /satellites.json?lat=..&lon=..&groups=..&minel=.. a dostanou
pro kazdou druzici nad obzorem kratkou drahu po obloze: azimut a vysku kazdych
STEP_SECONDS na SPAN_SECONDS dopredu. Druzice se hybou predvidatelne, takze si
hodiny drahu mezi dotazy samy dopocitaji a ptaji se jen jednou za minutu.

Drahy (OMM, tedy nastupce TLE) se stahuji z CelesTraku po skupinach a jen pro
skupiny, o ktere si nejaky hodiny rekly. Kazda skupina se obnovuje nejvys jednou
za REFRESH_SECONDS; CelesTrak data prepocitava po dvou hodinach a IP adresy,
ktere se ptaji casteji, blokuje. Posledni stazena data lezi na disku, takze
restart sluzby CelesTrak nezatizi.

TLE se tu schvalne nepouziva: katalogova cisla nad 99999 se do jeho peti cislic
nevejdou a CelesTrak u novych objektu TLE neposkytuje.

Bezi pod systemd jako satellites-web.service, posloucha jen na 127.0.0.1 a pred
svetem ho chrani Caddy s basic_auth: vypocet stoji procesor a dotaz nese polohu
hodin.
"""
from __future__ import annotations

import json
import math
import os
import tempfile
import threading
import time
import urllib.error
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse

import numpy as np
from sgp4 import omm
from sgp4.api import Satrec, SatrecArray, WGS72

PORT = int(os.environ.get("SATELLITES_PORT", "8095"))
BIND = os.environ.get("SATELLITES_BIND", "127.0.0.1")
CACHE_DIR = os.environ.get("SATELLITES_CACHE_DIR", "/var/cache/satellites")
UPSTREAM = os.environ.get(
    "SATELLITES_UPSTREAM", "https://celestrak.org/NORAD/elements/gp.php"
)
USER_AGENT = os.environ.get(
    "SATELLITES_USER_AGENT",
    "WaveshareHodiny/1.0 (+https://github.com/CooLajz/waveshare-hodiny)",
)

# Skupiny v poradi, ve kterem maji prednost. Index je soucasti odpovedi (klic
# "g") a firmware podle nej barvi, takze se poradi nesmi menit, jen pridavat na
# konec. Jmeno vlevo posilaji hodiny, vpravo je dotaz na CelesTrak: bud cela
# skupina (GROUP=), nebo jedina druzice podle katalogoveho cisla (CATNR=).
GROUPS = (
    ("stations", "GROUP=stations"),
    ("visual", "GROUP=visual"),
    ("weather", "GROUP=weather"),
    ("gnss", "GROUP=gnss"),
    ("amateur", "GROUP=amateur"),
    ("starlink", "GROUP=starlink"),
    # SATGUS (2025-009DJ) neni v zadne skupine CelesTraku, takze se stahuje
    # sama. Je to druzice, ktera fotky nahrane z domova snima nad Zemi, proto
    # ma na obrazovce prednost pred vsemi ostatnimi.
    ("satgus", "CATNR=62713"),
)
GROUP_INDEX = {name: index for index, (name, _) in enumerate(GROUPS)}
# Pri stropu na pocet druzic vyhravaji stanice a jasne druzice; Starlink doplni
# jen zbyle misto, jinak by jeho stovky nad obzorem vytlacily vsechno ostatni.
PRIORITY = ("satgus", "stations", "visual", "weather", "amateur", "gnss",
            "starlink")

STEP_SECONDS = 15
SAMPLE_COUNT = 13  # 0 az 180 s
SPAN_SECONDS = STEP_SECONDS * (SAMPLE_COUNT - 1)
# Firmware drzi stejny pocet; vic by jen stahl a zahodil.
MAX_SATELLITES = 150
MAX_NAME_LENGTH = 24
MAX_MIN_ELEVATION = 60

REFRESH_SECONDS = 6 * 3600
# Po chybe se ceka od deseti minut, s kazdou dalsi dvakrat dele.
RETRY_MIN_SECONDS = 600
# CelesTrak odpovida 403 nebo 429, kdyz se nekdo pta prilis casto. Zkouset to
# znovu za chvili by blokovani jen prodlouzilo.
RETRY_BLOCKED_SECONDS = 12 * 3600
UNCHANGED_RETRY_SECONDS = 2 * 3600 + 300
# Drahy starsi nez tyden uz nizke druzice ukazuji o stovky kilometru vedle.
MAX_DATA_AGE_SECONDS = 7 * 24 * 3600
# Skupina, o kterou se dva dny nikdo nepripomnel, se prestane obnovovat.
WANTED_SECONDS = 2 * 24 * 3600
DOWNLOAD_TIMEOUT_SECONDS = 60
# Starlink ma kolem deseti tisic objektu, tedy nekolik MB. Strop je jen pojistka
# proti nesmyslne odpovedi.
MAX_DOWNLOAD_BYTES = 40 * 1024 * 1024
MAX_OBJECTS_PER_GROUP = 30000

RESPONSE_CACHE_SECONDS = STEP_SECONDS
RESPONSE_CACHE_ENTRIES = 64
# Vypocet Starlinku stoji desitky milisekund. Vic soubeznych vypoctu nez dva by
# jen bralo procesor ostatnim sluzbam.
COMPUTE_SLOTS = threading.BoundedSemaphore(2)

# Prelety, na ktere hodiny upozornuji radkem pod oblohou: klic je skupina,
# ve ktere druzice prijde, a "pref" rika, ze se ma ukazat i tehdy, kdyz druhy
# prelet zacina o neco driv - domaci druzice je zajimavejsi nez ISS. Vyska je
# ta, od ktere se prelet pocita: pod deset stupnu ho skryji stromy a domy,
# stejne jako to pocita Heavens-Above.
PASS_SATELLITES = (
    ("satgus", 62713, "SATGUS", True),
    ("stations", 25544, "ISS", False),
)
# Starsi firmware cte jen "pass" a popisek ma napevno ISS, takze tam patri ISS.
LEGACY_PASS_NORAD_ID = 25544
PASS_MIN_ELEVATION = 10.0
PASS_STEP_SECONDS = 10
PASS_SEARCH_SECONDS = 36 * 3600
PASS_LOOKBACK_SECONDS = 20 * 60
# Pozorovatel je ve tme, kdyz je Slunce aspon sest stupnu pod obzorem.
DARK_SUN_ELEVATION = -6.0

EARTH_EQUATORIAL_KM = 6378.137
EARTH_FLATTENING = 1.0 / 298.257223563
EARTH_E2 = EARTH_FLATTENING * (2.0 - EARTH_FLATTENING)
UNIX_EPOCH_JD = 2440587.5

REQUIRED_OMM_KEYS = (
    "OBJECT_NAME", "NORAD_CAT_ID", "EPOCH", "MEAN_MOTION", "ECCENTRICITY",
    "INCLINATION", "RA_OF_ASC_NODE", "ARG_OF_PERICENTER", "MEAN_ANOMALY",
    "BSTAR", "MEAN_MOTION_DOT", "MEAN_MOTION_DDOT",
)
NAME_CHARACTERS = set("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 ()-./+&#")


# --- Geometrie ---------------------------------------------------------------
def julian_dates(epochs: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    """Unixovy cas na celou a zlomkovou cast julianskeho data."""
    days = np.floor(epochs / 86400.0)
    return UNIX_EPOCH_JD + days, (epochs - days * 86400.0) / 86400.0


def gmst_radians(epochs: np.ndarray) -> np.ndarray:
    """Greenwichsky hvezdny cas podle IAU 1982, stejne jako v SGP4 (Vallado)."""
    jd, fr = julian_dates(np.asarray(epochs, dtype=float))
    centuries = ((jd - 2451545.0) + fr) / 36525.0
    seconds = (
        -6.2e-6 * centuries**3
        + 0.093104 * centuries**2
        + (876600.0 * 3600.0 + 8640184.812866) * centuries
        + 67310.54841
    )
    return np.mod(np.radians(seconds / 240.0), 2.0 * math.pi)


def observer_ecef(latitude: float, longitude: float) -> np.ndarray:
    phi = math.radians(latitude)
    lam = math.radians(longitude)
    normal = EARTH_EQUATORIAL_KM / math.sqrt(1.0 - EARTH_E2 * math.sin(phi) ** 2)
    return np.array([
        normal * math.cos(phi) * math.cos(lam),
        normal * math.cos(phi) * math.sin(lam),
        normal * (1.0 - EARTH_E2) * math.sin(phi),
    ])


def teme_to_ecef(position: np.ndarray, gmst: np.ndarray) -> np.ndarray:
    """Otoceni kolem osy Z o hvezdny cas. Polarni pohyb se zanedbava - na
    obloze je to pod setinu stupne."""
    cosine = np.cos(gmst)
    sine = np.sin(gmst)
    x = position[..., 0]
    y = position[..., 1]
    return np.stack((cosine * x + sine * y, -sine * x + cosine * y,
                     position[..., 2]), axis=-1)


def look_angles(ecef: np.ndarray, latitude: float, longitude: float):
    """Azimut a vyska ve stupnich a vzdalenost v km z pozorovatele."""
    phi = math.radians(latitude)
    lam = math.radians(longitude)
    delta = ecef - observer_ecef(latitude, longitude)
    dx, dy, dz = delta[..., 0], delta[..., 1], delta[..., 2]
    east = -math.sin(lam) * dx + math.cos(lam) * dy
    north = (-math.sin(phi) * math.cos(lam) * dx
             - math.sin(phi) * math.sin(lam) * dy + math.cos(phi) * dz)
    up = (math.cos(phi) * math.cos(lam) * dx
          + math.cos(phi) * math.sin(lam) * dy + math.sin(phi) * dz)
    horizontal = np.hypot(east, north)
    elevation = np.degrees(np.arctan2(up, horizontal))
    azimuth = np.mod(np.degrees(np.arctan2(east, north)), 360.0)
    return azimuth, elevation, np.sqrt(horizontal**2 + up**2)


def sun_direction_eci(epochs: np.ndarray) -> np.ndarray:
    """Jednotkovy smer ke Slunci v rovnikovych souradnicich data (Astronomical
    Almanac, presnost kolem setiny stupne)."""
    jd, fr = julian_dates(np.asarray(epochs, dtype=float))
    days = (jd - 2451545.0) + fr
    mean_longitude = np.radians(280.460 + 0.9856474 * days)
    anomaly = np.radians(357.528 + 0.9856003 * days)
    ecliptic = mean_longitude + np.radians(
        1.915 * np.sin(anomaly) + 0.020 * np.sin(2.0 * anomaly))
    obliquity = np.radians(23.439 - 0.0000004 * days)
    return np.stack((np.cos(ecliptic), np.cos(obliquity) * np.sin(ecliptic),
                     np.sin(obliquity) * np.sin(ecliptic)), axis=-1)


def sunlit(position_teme: np.ndarray, sun: np.ndarray) -> np.ndarray:
    """Valcovy stin Zeme: druzice je ve stinu jen za Zemi a blize ose stinu nez
    polomer Zeme."""
    along = np.sum(position_teme * sun, axis=-1)
    perpendicular = position_teme - along[..., None] * sun
    distance = np.linalg.norm(perpendicular, axis=-1)
    return (along > 0.0) | (distance > EARTH_EQUATORIAL_KM)


def sun_elevation(epoch: float, latitude: float, longitude: float) -> float:
    sun = teme_to_ecef(sun_direction_eci(np.array([epoch]))[0],
                       gmst_radians(np.array([epoch]))[0])
    phi = math.radians(latitude)
    lam = math.radians(longitude)
    up = np.array([math.cos(phi) * math.cos(lam),
                   math.cos(phi) * math.sin(lam), math.sin(phi)])
    return math.degrees(math.asin(max(-1.0, min(1.0, float(np.dot(sun, up))))))


def altitude_km(position_teme: np.ndarray) -> np.ndarray:
    radius = np.linalg.norm(position_teme, axis=-1)
    sine_latitude = position_teme[..., 2] / np.maximum(radius, 1.0)
    surface = EARTH_EQUATORIAL_KM * (1.0 - EARTH_FLATTENING * sine_latitude**2)
    return radius - surface


# --- Katalog -------------------------------------------------------------------
def clean_name(name: object) -> str:
    text = str(name).upper()
    return "".join(c for c in text if c in NAME_CHARACTERS).strip()[:MAX_NAME_LENGTH]


def satrec_from_omm(fields: dict) -> Satrec | None:
    if not isinstance(fields, dict):
        return None
    if any(key not in fields for key in REQUIRED_OMM_KEYS):
        return None
    record = dict(fields)
    epoch = str(record["EPOCH"])
    # strptime chce zlomek sekundy; CelesTrak ho posila, jiny zdroj nemusi.
    if "." not in epoch:
        epoch += ".0"
    record["EPOCH"] = epoch
    record.setdefault("CLASSIFICATION_TYPE", "U")
    record.setdefault("OBJECT_ID", "")
    record.setdefault("EPHEMERIS_TYPE", 0)
    record.setdefault("ELEMENT_SET_NO", 999)
    record.setdefault("REV_AT_EPOCH", 0)
    try:
        satellite = Satrec()
        omm.initialize(satellite, record, WGS72)
    except (KeyError, TypeError, ValueError, OverflowError):
        return None
    if satellite.error != 0:
        return None
    return satellite


class Group:
    """Nactena skupina: SatrecArray pro vektorovy vypocet a jmena k ni."""

    def __init__(self, name: str, records: list, downloaded_at: float):
        satellites = []
        names = []
        ids = []
        for fields in records[:MAX_OBJECTS_PER_GROUP]:
            satellite = satrec_from_omm(fields)
            if satellite is None:
                continue
            try:
                norad_id = int(fields["NORAD_CAT_ID"])
            except (TypeError, ValueError):
                continue
            satellites.append(satellite)
            names.append(clean_name(fields["OBJECT_NAME"]) or str(norad_id))
            ids.append(norad_id)
        if not satellites:
            raise ValueError("no usable element sets")
        self.name = name
        self.array = SatrecArray(satellites)
        self.satellites = satellites
        self.names = names
        self.ids = np.array(ids, dtype=np.int64)
        self.downloaded_at = downloaded_at


def parse_catalog(payload: bytes) -> list:
    data = json.loads(payload)
    if not isinstance(data, list) or not data:
        raise ValueError("expected a non-empty list of element sets")
    if not all(isinstance(item, dict) for item in data):
        raise ValueError("element sets must be objects")
    return data


class Catalog:
    """Skupiny v pameti, stav stahovani a vlakno, ktere obnovuje chtene skupiny."""

    def __init__(self, cache_dir: str = CACHE_DIR, fetcher=None, clock=time.time):
        self.cache_dir = cache_dir
        self.fetcher = fetcher or download_group
        self.clock = clock
        self.lock = threading.Lock()
        self.wake = threading.Event()
        self.groups: dict[str, Group] = {}
        self.wanted: dict[str, float] = {}
        self.next_attempt: dict[str, float] = {}
        self.failures: dict[str, int] = {}
        self.last_error: dict[str, str] = {}
        self.version = 0

    # Nacteni z disku po startu. Poskozeny soubor se ignoruje a skupina se
    # stahne znovu, az o ni nekdo rekne.
    def load_cached(self) -> None:
        for name, _ in GROUPS:
            path = os.path.join(self.cache_dir, f"{name}.json")
            try:
                with open(path, "rb") as handle:
                    payload = handle.read(MAX_DOWNLOAD_BYTES + 1)
                downloaded_at = os.path.getmtime(path)
                group = Group(name, parse_catalog(payload), downloaded_at)
            except (OSError, ValueError):
                continue
            with self.lock:
                self.groups[name] = group
                self.next_attempt[name] = downloaded_at + REFRESH_SECONDS
                self.version += 1

    def request(self, names: list[str]) -> None:
        now = self.clock()
        wake = False
        with self.lock:
            for name in names:
                self.wanted[name] = now
                if self.next_attempt.get(name, 0.0) <= now:
                    wake = True
        if wake:
            self.wake.set()

    def snapshot(self, names: list[str]):
        with self.lock:
            return {name: self.groups.get(name) for name in names}, self.version

    def due_groups(self) -> list[str]:
        now = self.clock()
        with self.lock:
            return [
                name for name, requested in self.wanted.items()
                if now - requested < WANTED_SECONDS
                and self.next_attempt.get(name, 0.0) <= now
            ]

    def refresh(self, name: str) -> None:
        now = self.clock()
        try:
            status, payload = self.fetcher(name)
        except (urllib.error.URLError, OSError, ValueError) as error:
            self._failed(name, now, f"download failed: {error}", blocked=False)
            return
        if b"has not updated" in payload[:300].lower():
            # Stejna data jako minule (CelesTrak prepocitava po dvou hodinach).
            # Pocita se to jako uspech, jinak by se skupina ptala znovu a
            # CelesTrak by ji zablokoval.
            with self.lock:
                known = self.groups.get(name)
                if known is not None:
                    known.downloaded_at = now
                    self.next_attempt[name] = now + REFRESH_SECONDS
                    self.failures[name] = 0
                    self.last_error.pop(name, None)
                else:
                    # Data na disku chybi a nova CelesTrak neda, dokud je
                    # neprepocita. Driv nez za dve hodiny nema smysl zkouset.
                    self.next_attempt[name] = now + UNCHANGED_RETRY_SECONDS
                    self.last_error[name] = "CelesTrak has no newer data yet"
            if known is not None:
                self._touch(name, now)
            return
        if status in (403, 429):
            self._failed(name, now, f"CelesTrak refused with HTTP {status}", blocked=True)
            return
        if status != 200:
            self._failed(name, now, f"CelesTrak answered HTTP {status}", blocked=False)
            return
        try:
            group = Group(name, parse_catalog(payload), now)
        except (ValueError, UnicodeDecodeError) as error:
            self._failed(name, now, f"unexpected catalog: {error}", blocked=False)
            return
        self._store(name, payload)
        with self.lock:
            self.groups[name] = group
            self.next_attempt[name] = now + REFRESH_SECONDS
            self.failures[name] = 0
            self.last_error.pop(name, None)
            self.version += 1

    def _failed(self, name: str, now: float, message: str, blocked: bool) -> None:
        with self.lock:
            failures = self.failures.get(name, 0) + 1
            self.failures[name] = failures
            delay = (RETRY_BLOCKED_SECONDS if blocked else
                     min(REFRESH_SECONDS, RETRY_MIN_SECONDS * 2 ** (failures - 1)))
            self.next_attempt[name] = now + delay
            self.last_error[name] = message[:160]

    def _store(self, name: str, payload: bytes) -> None:
        try:
            os.makedirs(self.cache_dir, exist_ok=True)
            handle, temporary = tempfile.mkstemp(dir=self.cache_dir, suffix=".tmp")
            with os.fdopen(handle, "wb") as output:
                output.write(payload)
            os.replace(temporary, os.path.join(self.cache_dir, f"{name}.json"))
        except OSError:
            # Bez disku se jen po restartu stahuje znovu.
            pass

    def _touch(self, name: str, now: float) -> None:
        try:
            os.utime(os.path.join(self.cache_dir, f"{name}.json"), (now, now))
        except OSError:
            pass

    def status(self) -> dict:
        now = self.clock()
        with self.lock:
            result = {}
            for name, _ in GROUPS:
                group = self.groups.get(name)
                result[name] = {
                    "objects": len(group.names) if group else 0,
                    "ageHours": round((now - group.downloaded_at) / 3600, 1) if group else None,
                    "wanted": name in self.wanted and now - self.wanted[name] < WANTED_SECONDS,
                    "nextAttemptIn": max(0, int(self.next_attempt.get(name, 0) - now)),
                    "error": self.last_error.get(name, ""),
                }
            return result

    def run_forever(self) -> None:
        while True:
            for name in self.due_groups():
                self.refresh(name)
            self.wake.wait(60)
            self.wake.clear()


def download_group(name: str) -> tuple[int, bytes]:
    url = f"{UPSTREAM}?{dict(GROUPS)[name]}&FORMAT=json"
    request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    try:
        with urllib.request.urlopen(request, timeout=DOWNLOAD_TIMEOUT_SECONDS) as response:
            payload = response.read(MAX_DOWNLOAD_BYTES + 1)
            if len(payload) > MAX_DOWNLOAD_BYTES:
                raise ValueError("catalog too large")
            return response.status, payload
    except urllib.error.HTTPError as error:
        # Telo chyby se cte kvuli hlasce "has not updated"; delsi nebyva.
        try:
            return error.code, error.read(4096)
        except OSError:
            return error.code, b""


# --- Odpoved -------------------------------------------------------------------
def sky_tracks(group: Group, epochs: np.ndarray, latitude: float, longitude: float):
    jd, fr = julian_dates(epochs)
    errors, positions, _ = group.array.sgp4(jd, fr)
    ecef = teme_to_ecef(positions, gmst_radians(epochs)[None, :])
    azimuth, elevation, distance = look_angles(ecef, latitude, longitude)
    valid = (errors == 0).all(axis=1) & np.isfinite(elevation).all(axis=1)
    return positions, azimuth, elevation, distance, valid


def next_pass(satellite: Satrec, now: float, latitude: float, longitude: float):
    """Nejblizsi prelet nad PASS_MIN_ELEVATION, ktery jeste neskoncil."""
    epochs = (now - PASS_LOOKBACK_SECONDS
              + PASS_STEP_SECONDS * np.arange(
                  (PASS_LOOKBACK_SECONDS + PASS_SEARCH_SECONDS) // PASS_STEP_SECONDS))
    jd, fr = julian_dates(epochs)
    errors, positions, _ = satellite.sgp4_array(jd, fr)
    ecef = teme_to_ecef(positions, gmst_radians(epochs))
    _, elevation, _ = look_angles(ecef, latitude, longitude)
    above = (errors == 0) & (elevation >= PASS_MIN_ELEVATION)
    lit = sunlit(positions, sun_direction_eci(epochs))
    index = 0
    count = len(epochs)
    while index < count:
        if not above[index]:
            index += 1
            continue
        start = index
        while index < count and above[index]:
            index += 1
        end = index - 1
        # Prelet useknuty zacatkem okna uz se nedozvime cely; pokud jeste
        # probiha, zacatek se odhadne linearne jen kdyz ho okno ma.
        if start == 0 or end == count - 1:
            continue

        def crossing(before: int, after: int) -> float:
            e0 = elevation[before] - PASS_MIN_ELEVATION
            e1 = elevation[after] - PASS_MIN_ELEVATION
            fraction = 0.0 if e1 == e0 else -e0 / (e1 - e0)
            return float(epochs[before] + fraction * (epochs[after] - epochs[before]))

        rise = crossing(start - 1, start)
        set_time = crossing(end, end + 1)
        if set_time <= now:
            continue
        peak = start + int(np.argmax(elevation[start:end + 1]))
        visible = False
        for sample in range(start, end + 1):
            if lit[sample] and sun_elevation(float(epochs[sample]), latitude,
                                             longitude) <= DARK_SUN_ELEVATION:
                visible = True
                break
        return {
            "rise": int(round(rise)),
            "set": int(round(set_time)),
            "maxTime": int(epochs[peak]),
            "max": int(round(float(elevation[peak]))),
            "vis": 1 if visible else 0,
        }
    return None


def build_response(catalog: Catalog, latitude: float, longitude: float,
                   names: list[str], min_elevation: int, now: float) -> tuple[int, dict]:
    groups, _ = catalog.snapshot(names)
    start = math.floor(now / STEP_SECONDS) * STEP_SECONDS
    epochs = start + STEP_SECONDS * np.arange(SAMPLE_COUNT, dtype=float)
    sun = sun_direction_eci(epochs[:1])[0]

    problems = []
    pending = []
    oldest = None
    candidates = []
    usable: dict[str, Group] = {}
    seen: set[int] = set()
    for name in sorted(names, key=PRIORITY.index):
        group = groups.get(name)
        if group is None:
            pending.append(name)
            continue
        age = now - group.downloaded_at
        if age > MAX_DATA_AGE_SECONDS:
            problems.append(f"{name} elements are {int(age // 86400)} days old")
            continue
        oldest = age if oldest is None else max(oldest, age)
        usable[name] = group
        positions, azimuth, elevation, distance, valid = sky_tracks(
            group, epochs, latitude, longitude)
        keep = valid & (elevation.max(axis=1) >= min_elevation)
        lit = sunlit(positions[:, 0, :], sun)
        heights = altitude_km(positions[:, 0, :])
        order = np.argsort(-elevation[:, 0])
        for row in order:
            if not keep[row]:
                continue
            norad_id = int(group.ids[row])
            if norad_id in seen:
                continue
            seen.add(norad_id)
            samples = []
            for column in range(SAMPLE_COUNT):
                samples.append(int(round(float(azimuth[row, column]) * 10)) % 3600)
                samples.append(int(round(float(elevation[row, column]) * 10)))
            candidates.append({
                "id": norad_id,
                "n": group.names[row],
                "g": GROUP_INDEX[name],
                "h": max(0, int(round(float(heights[row])))),
                "r": int(round(float(distance[row, 0]))),
                "l": 1 if bool(lit[row]) else 0,
                "p": samples,
            })

    # Zadna pouzitelna skupina: bud se teprve stahuje, nebo jsou data prilis
    # stara. Prazdny seznam s kodem 200 by hodiny ukazaly jako prazdnou oblohu.
    if oldest is None:
        if pending:
            return 503, {"error": "loading", "pending": pending}
        return 503, {"error": "unavailable", "problem": "; ".join(problems)[:120]}

    body = {
        "v": 1,
        "time": int(start),
        "step": STEP_SECONDS,
        "samples": SAMPLE_COUNT,
        "sun": int(round(sun_elevation(start, latitude, longitude) * 10)),
        "total": len(candidates),
        "age": int((oldest or 0) // 3600),
        "sats": candidates[:MAX_SATELLITES],
    }
    passes = []
    for name, norad_id, label, preferred in PASS_SATELLITES:
        group = usable.get(name)
        if group is None or not len(np.nonzero(group.ids == norad_id)[0]):
            continue
        upcoming = pass_cache.get(group, norad_id, latitude, longitude, now)
        if upcoming is None:
            continue
        entry = {"id": norad_id, "n": label, **upcoming}
        if preferred:
            entry["pref"] = 1
        passes.append(entry)
    if passes:
        body["passes"] = passes
        # Starsi firmware zna jen "pass" a popisek u nej ma napevno ISS,
        # takze tam nesmi skoncit jina druzice.
        legacy = [entry for entry in passes if entry["id"] == LEGACY_PASS_NORAD_ID]
        if legacy:
            body["pass"] = legacy[0]
    if problems:
        body["problem"] = "; ".join(problems)[:120]
    if pending:
        body["pending"] = pending
    return 200, body


class PassCache:
    """Prelet se pocita tisici kroku, ale meni se jen jednou za prelet."""

    def __init__(self):
        self.lock = threading.Lock()
        self.entries: dict[tuple, dict | None] = {}

    def get(self, group: Group, norad_id: int, latitude: float, longitude: float,
            now: float):
        key = (round(latitude, 2), round(longitude, 2), id(group), norad_id)
        with self.lock:
            entry = self.entries.get(key)
        if entry is not None and entry["validUntil"] > now:
            return entry["pass"]
        index = int(np.nonzero(group.ids == norad_id)[0][0])
        upcoming = next_pass(group.satellites[index], now, latitude, longitude)
        valid_until = upcoming["set"] if upcoming else now + 1800
        with self.lock:
            if len(self.entries) > 64:
                self.entries.clear()
            self.entries[key] = {"pass": upcoming, "validUntil": valid_until}
        return upcoming


pass_cache = PassCache()


# --- HTTP --------------------------------------------------------------------------
def parse_query(query: str):
    """Vraci (lat, lon, skupiny, minel), nebo text chyby."""
    if len(query) > 256:
        return "query too long"
    fields = parse_qs(query, keep_blank_values=True)
    try:
        latitude = float(fields.get("lat", [""])[0])
        longitude = float(fields.get("lon", [""])[0])
    except ValueError:
        return "expected lat and lon"
    if not (math.isfinite(latitude) and math.isfinite(longitude)
            and -90.0 <= latitude <= 90.0 and -180.0 <= longitude <= 180.0):
        return "latitude or longitude out of range"
    raw_groups = fields.get("groups", ["stations,visual,weather"])[0]
    names = [name for name in raw_groups.split(",") if name]
    if not names or len(names) > len(GROUPS) or any(name not in GROUP_INDEX for name in names):
        return "unknown satellite group"
    names = sorted(set(names), key=PRIORITY.index)
    raw_min = fields.get("minel", ["10"])[0]
    if not raw_min.isdigit() or int(raw_min) > MAX_MIN_ELEVATION:
        return f"minel must be 0 to {MAX_MIN_ELEVATION}"
    # Poloha se zaokrouhluje na setinu stupne (kilometr): na obloze je to pod
    # stupen a hodiny v jedne domacnosti sdili jednu odpoved.
    return round(latitude, 2), round(longitude, 2), names, int(raw_min)


class ResponseCache:
    def __init__(self):
        self.lock = threading.Lock()
        self.entries: dict[tuple, tuple[float, int, bytes]] = {}

    def get(self, key: tuple, now: float):
        with self.lock:
            entry = self.entries.get(key)
        if entry is not None and now - entry[0] < RESPONSE_CACHE_SECONDS:
            return entry[1], entry[2]
        return None

    def put(self, key: tuple, now: float, status: int, body: bytes) -> None:
        with self.lock:
            self.entries[key] = (now, status, body)
            if len(self.entries) > RESPONSE_CACHE_ENTRIES:
                oldest = sorted(self.entries.items(), key=lambda item: item[1][0])
                for stale_key, _ in oldest[: len(self.entries) - RESPONSE_CACHE_ENTRIES]:
                    self.entries.pop(stale_key, None)


catalog = Catalog()
responses = ResponseCache()
# Nocni obloha (sky.py) se nacita az za behu: bez skyfieldu nebo efemerid maji
# druzice fungovat dal, jen obloha odpovi 503.
night_sky = None


def start_night_sky() -> None:
    global night_sky
    try:
        import sky
    except ImportError:
        return
    night_sky = sky.Sky(CACHE_DIR, USER_AGENT)
    night_sky.start()


def answer_sky(query: str, now: float | None = None) -> tuple[int, bytes, dict]:
    """Nocni obloha pro stranku vedle druzic: /satellites.json?view=sky."""
    if len(query) > 256:
        return 400, b"query too long\n", {}
    fields = parse_qs(query)
    try:
        latitude = float(fields.get("lat", [""])[0])
        longitude = float(fields.get("lon", [""])[0])
    except ValueError:
        return 400, b"expected lat and lon\n", {}
    if not (math.isfinite(latitude) and math.isfinite(longitude)
            and -90.0 <= latitude <= 90.0 and -180.0 <= longitude <= 180.0):
        return 400, b"latitude or longitude out of range\n", {}
    if night_sky is None or not night_sky.ready():
        return 503, b"ephemeris loading\n", {"Retry-After": "60"}
    english = fields.get("lang", ["cs"])[0].lower().startswith("en")
    now = time.time() if now is None else now
    with COMPUTE_SLOTS:
        payload = night_sky.answer(latitude, longitude, english, now)
    return 200, json.dumps(payload, ensure_ascii=False, separators=(",", ":")).encode(), {}


def answer(query: str, now: float | None = None) -> tuple[int, bytes, dict]:
    parsed = parse_query(query)
    if isinstance(parsed, str):
        return 400, (parsed + "\n").encode(), {}
    latitude, longitude, names, min_elevation = parsed
    catalog.request(names)
    now = time.time() if now is None else now
    _, version = catalog.snapshot(names)
    key = (latitude, longitude, tuple(names), min_elevation, version,
           math.floor(now / STEP_SECONDS))
    cached = responses.get(key, now)
    if cached is not None:
        status, body = cached
    else:
        with COMPUTE_SLOTS:
            status, payload = build_response(catalog, latitude, longitude, names,
                                             min_elevation, now)
        body = json.dumps(payload, separators=(",", ":")).encode()
        responses.put(key, now, status, body)
    headers = {"Retry-After": "30"} if status == 503 else {}
    return status, body, headers


class Handler(BaseHTTPRequestHandler):
    server_version = "satellites-web/1.0"
    # ThreadingHTTPServer zaklada vlakno na spojeni; bez timeoutu by je pomala
    # spojeni hromadila.
    timeout = 15

    def _send(self, code: int, body: bytes, ctype: str, headers: dict | None = None):
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        for name, value in (headers or {}).items():
            self.send_header(name, value)
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(body)

    def do_GET(self):
        if len(self.path) > 512:
            self._send(414, b"uri too long\n", "text/plain; charset=utf-8")
            return
        parsed = urlparse(self.path)
        if parsed.path == "/satellites/status":
            # Caddy tuhle cestu ven nepousti; je pro check-stack pres SSH.
            status = catalog.status()
            status["sky"] = night_sky.status() if night_sky is not None else None
            body = json.dumps(status, separators=(",", ":")).encode()
            self._send(200, body, "application/json; charset=utf-8")
            return
        if parsed.path not in ("/satellites.json", "/"):
            self._send(404, b"not found\n", "text/plain; charset=utf-8")
            return
        try:
            if "sky" in parse_qs(parsed.query).get("view", []):
                status, body, headers = answer_sky(parsed.query)
            else:
                status, body, headers = answer(parsed.query)
        except Exception:  # noqa: BLE001 - jedna rozbita skupina nesmi shodit sluzbu
            self._send(500, b"internal error\n", "text/plain; charset=utf-8")
            return
        ctype = "application/json; charset=utf-8" if status != 400 else "text/plain; charset=utf-8"
        self._send(status, body, ctype, headers)

    do_HEAD = do_GET

    def log_message(self, *args):  # tise, at neplni journal
        pass


if __name__ == "__main__":
    catalog.load_cached()
    start_night_sky()
    threading.Thread(target=catalog.run_forever, name="celestrak", daemon=True).start()
    ThreadingHTTPServer((BIND, PORT), Handler).serve_forever()
