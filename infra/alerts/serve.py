#!/usr/bin/env python3
"""Upozorneni na telefon: blizici se dest a neobvykla letadla pres ntfy.

Hodiny umi prepnout na radar, kdyz se blizi dest, a zvyraznit letadlo
v nouzi - jenze jen kdyz jsou zapnute a nekdo se na ne diva. Tenhle server
hlida totez sam a posila push na telefon, i kdyz jsou vsechny hodiny vypnute.

Nastaveni se zadava v hodinach (zalozka Obrazovky, Upozorneni na telefon)
a hodiny ho po ulozeni poslou sem:

    PUT /alerts/config/<id>     id = MAC adresa hodin, telo viz parse_config()
                                odpoved {"elevation": 478.0} (nebo null)

Server si ho ulozi na disk a hlida dal bez nich. Vypnute upozorneni hodiny
poslou taky, jen s "enabled":false; soubor pak zustane, ale nehlida se nic.

Co se hlida:

- Dest. Predpoved je z infra/rain (extrapolace radaru CHMU, kroky po deseti
  minutach). Push odejde, kdyz ted neprsi a nektery krok do nastavene doby
  prekroci prah. Dalsi az po pul hodine sucha, aby jedna fronta nepipala
  s kazdym novym snimkem.
- Vojenska letadla: bit 1 v dbFlags z databaze tar1090, kterou adsb.fi
  pripojuje ke kazdemu letadlu. Vojaci ADS-B casto vypinaji, takze tohle
  najde jen ty, kteri vysilaji.
- Vzacne typy: typ, ktery se tu za poslednich 30 dni ukazal mene nez trikrat,
  nebo letadlo, ktere databaze znaci jako zajimave (bit 2 v dbFlags). Server
  si pocty typu vede sam; prvnich 14 dni nehlasi nic, protoze by bylo vzacne
  uplne vsechno.
- Nizke prelety: poloha, kurz, rychlost a stoupani se promitnou po primce
  a spocita se nejblizsi bod ke hodinam v pristich trech minutach. Push
  odejde, kdyz letadlo projde blize nez nastaveny polomer a nize nez nastavena
  vyska nad zemi. Vrtulniky a letadla v okruhu nad letistem primo neleti,
  takze u nich je odhad hrubsi.

Odpoved na PUT nese nadmorskou vysku polohy. Hodiny podle ni na obrazovce
letadel oznaci nizky prelet stejne jako tenhle server; samy ji odnikud nemaji.

Letadla i predpoved se berou z ostatnich sluzeb na tomto stroji po loopbacku
(infra/planes, infra/rain), takze adsb.fi i CHMU dal vidi jediny zdroj dotazu
s jejich cache. Letadla se ctou kazdych 10 sekund, tedy desetina limitu
adsb.fi (1 dotaz za sekundu).

Push jde na ntfy, stejne jako u hlidani obchodu (/opt/watch): JSON POST na
NTFY_URL, tema je jedine tajemstvi. Nerusit od-do plati pro vsechno.

GET /alerts/status vraci prehled pro check-stack; ven pres Caddy nevede.
"""
from __future__ import annotations

import argparse
import json
import math
import os
import re
import sys
import tempfile
import threading
import time
import urllib.error
import urllib.request
from datetime import datetime, timedelta
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlencode, urlparse
from zoneinfo import ZoneInfo

PORT = int(os.environ.get("ALERTS_PORT", "8098"))
BIND = os.environ.get("ALERTS_BIND", "127.0.0.1")
STATE = Path(os.environ.get("ALERTS_STATE", "/opt/alerts/state"))
RAIN_URL = os.environ.get("ALERTS_RAIN_URL", "http://127.0.0.1:8096/rain.json")
PLANES_URL = os.environ.get("ALERTS_PLANES_URL", "http://127.0.0.1:8090/planes.json")
ELEVATION_URL = os.environ.get(
    "ALERTS_ELEVATION_URL", "https://api.open-meteo.com/v1/elevation")
NTFY_URL = os.environ.get("NTFY_URL", "https://ntfy.sh").rstrip("/")
NTFY_TOPIC = os.environ.get("NTFY_TOPIC", "").strip()
NTFY_TOKEN = os.environ.get("NTFY_TOKEN", "").strip()
USER_AGENT = os.environ.get(
    "ALERTS_USER_AGENT",
    "WaveshareHodiny/1.0 (+https://github.com/CooLajz/waveshare-hodiny)",
)
TIMEZONE = ZoneInfo(os.environ.get("ALERTS_TIMEZONE", "Europe/Prague"))

# Jak casto se kontroluje. Predpoved CHMU je nova po peti minutach a infra/rain
# ji drzi ctyri; dva a pul minuty tedy nove snimky nemine a nestahuje navic.
RAIN_PERIOD_SECONDS = 150
# Letadlo v 300 km/h uleti za deset sekund 830 m; pri trech minutach
# vyhledu je to porad osmnact pokusu, nez prelet nastane.
PLANES_PERIOD_SECONDS = 10
HTTP_TIMEOUT_SECONDS = 12
# Predpoved starsi nez pul hodiny uz o blizicim se desti nic nerika.
RAIN_MAX_AGE_SECONDS = 1800
# Po pushi se dalsi posle az po tolika minutach bez deste v predpovedi.
RAIN_REARM_MINUTES = 30
# Nejdal, kam se prelet predpovida. Dal uz primka neplati: letadlo zataci,
# klesa na pristani nebo stoupa jinak, nez prave ted.
LOW_LOOKAHEAD_SECONDS = 180
# Pod touhle rychlosti letadlo (vrtulnik) skoro stoji a smer nic neznamena;
# bere se jeho soucasna vzdalenost.
LOW_HOVER_KNOTS = 30
# ADS-B hlasi geometrickou vysku nad elipsoidem WGS84; geoid je v Cesku
# 44 az 48 m nad nim. Chyba par metru je proti presnosti vysky zanedbatelna.
GEOID_OFFSET_M = 45.0
# Opakovani stejneho letadla. Vojak krouzici nad krajem by jinak pipal kazdych
# deset sekund.
PLANE_REPEAT_SECONDS = {"low": 3600, "military": 6 * 3600, "rare": 6 * 3600}
# Vzacnost typu: mene nez RARE_MAX_DAYS dni s vyskytem za RARE_WINDOW_DAYS.
RARE_WINDOW_DAYS = 30
RARE_MAX_DAYS = 3
RARE_WARMUP_DAYS = 14
TYPE_HISTORY_DAYS = 60

MAX_CONFIGS = 16
MAX_BODY_BYTES = 4096
CONFIG_ID = re.compile(r"^[a-z0-9][a-z0-9-]{0,31}$")

FT_TO_M = 0.3048
KNOT_TO_MS = 0.514444
NM_TO_KM = 1.852


# ---------------------------------------------------------------------------
# Nastaveni od hodin
# ---------------------------------------------------------------------------

def _number(value, low, high, kind=float):
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ValueError("expected a number")
    if not low <= value <= high:
        raise ValueError(f"{value} outside {low}..{high}")
    return kind(value)


def _flag(value) -> bool:
    if not isinstance(value, bool):
        raise ValueError("expected true or false")
    return value


def parse_config(payload: object) -> dict:
    """Overi telo od hodin. Meze jsou stejne jako v ClockConfig.h."""
    if not isinstance(payload, dict):
        raise ValueError("expected an object")
    rain = payload.get("rain")
    planes = payload.get("planes")
    quiet = payload.get("quiet")
    if not isinstance(rain, dict) or not isinstance(planes, dict) or not isinstance(quiet, dict):
        raise ValueError("expected rain, planes and quiet objects")
    name = payload.get("name", "")
    if not isinstance(name, str) or len(name) > 32:
        raise ValueError("name must be a short string")
    return {
        "name": name,
        "enabled": _flag(payload.get("enabled")),
        "lat": _number(payload.get("lat"), -90, 90),
        "lon": _number(payload.get("lon"), -180, 180),
        "rain": {
            "enabled": _flag(rain.get("enabled")),
            "lead": _number(rain.get("lead"), 10, 60, int),
            "dbz": _number(rain.get("dbz"), 4, 60, int),
            "radius": _number(rain.get("radius"), 1, 30, int),
        },
        "planes": {
            "military": _flag(planes.get("military")),
            "rare": _flag(planes.get("rare")),
            "low": _flag(planes.get("low")),
            "radius": _number(planes.get("radius"), 5, 100, int),
            "lowRadius": _number(planes.get("lowRadius"), 300, 10000, int),
            "lowHeight": _number(planes.get("lowHeight"), 100, 3000, int),
        },
        "quiet": {
            "from": _number(quiet.get("from"), 0, 23, int),
            "to": _number(quiet.get("to"), 0, 23, int),
        },
    }


def is_quiet(config: dict, now: datetime) -> bool:
    start, end = config["quiet"]["from"], config["quiet"]["to"]
    if start == end:
        return False
    hour = now.hour
    return start <= hour < end if start < end else hour >= start or hour < end


def location_key(config: dict) -> tuple[float, float]:
    # Zhruba kilometr. Hodiny v jednom dome maji tutez polohu, takze sdili
    # stazeni i to, ze push odejde jen jednou.
    return round(config["lat"], 2), round(config["lon"], 2)


# ---------------------------------------------------------------------------
# Dest
# ---------------------------------------------------------------------------

def rain_state(forecast: dict, lead: int, dbz: int) -> tuple[str, int, int]:
    """("coming", za kolik minut, dBZ) / ("raining"|"dry"|"unknown", 0, 0)."""
    if forecast.get("covered") is not True:
        return "unknown", 0, 0
    now = forecast.get("now")
    steps = forecast.get("steps")
    step = forecast.get("step", 10)
    if not isinstance(now, int) or not isinstance(steps, list) or not isinstance(step, int):
        return "unknown", 0, 0
    if now >= dbz:
        return "raining", 0, now
    missing = now < 0
    for index, value in enumerate(steps):
        minutes = (index + 1) * step
        if minutes > lead:
            break
        if not isinstance(value, int) or value < 0:
            missing = True
            continue
        if value >= dbz:
            return "coming", minutes, value
    # Chybejici snimek neni sucho: znovu se nabije az z uplne predpovedi.
    return ("unknown" if missing else "dry"), 0, 0


RAIN_WORDS = ((44, "průtrž"), (36, "silný déšť"), (28, "déšť"), (20, "slabý déšť"), (0, "mrholení"))


def rain_word(dbz: int) -> str:
    return next(word for limit, word in RAIN_WORDS if dbz >= limit)


class RainWatch:
    """Stav jedne polohy a jednoho nastaveni: nabito, nebo ceka na sucho."""

    def __init__(self):
        self.armed = True
        self.last_wet = 0.0
        self.last_state = "unknown"

    def update(self, state: str, now: float) -> bool:
        """Vrati True, kdyz ma odejit push."""
        self.last_state = state
        if state in ("coming", "raining"):
            fire = state == "coming" and self.armed
            self.armed = False
            self.last_wet = now
            return fire
        if state == "dry" and not self.armed and now - self.last_wet >= RAIN_REARM_MINUTES * 60:
            self.armed = True
        return False


# ---------------------------------------------------------------------------
# Letadla
# ---------------------------------------------------------------------------

def local_xy_km(home_lat: float, home_lon: float, lat: float, lon: float) -> tuple[float, float]:
    """Rovinne souradnice v km od domu (x na vychod, y na sever)."""
    x = (lon - home_lon) * 111.320 * math.cos(math.radians(home_lat))
    y = (lat - home_lat) * 110.574
    return x, y


def height_above_ground_m(aircraft: dict, elevation_m: float) -> float | None:
    geometric = aircraft.get("alt_geom")
    if isinstance(geometric, (int, float)):
        return geometric * FT_TO_M - GEOID_OFFSET_M - elevation_m
    # Barometricka vyska je vztazena k 1013 hPa, ne ke skutecnemu tlaku; pri
    # vyse i nize se splete o stovku metru. Lepsi nez nic, kdyz geometricka chybi.
    barometric = aircraft.get("alt_baro")
    if isinstance(barometric, (int, float)):
        return barometric * FT_TO_M - elevation_m
    return None


def predict_pass(aircraft: dict, home_lat: float, home_lon: float,
                 elevation_m: float) -> dict | None:
    """Nejblizsi bod k domu v pristich LOW_LOOKAHEAD_SECONDS po primce."""
    lat, lon = aircraft.get("lat"), aircraft.get("lon")
    if not isinstance(lat, (int, float)) or not isinstance(lon, (int, float)):
        return None
    height = height_above_ground_m(aircraft, elevation_m)
    if height is None:
        return None
    x, y = local_xy_km(home_lat, home_lon, lat, lon)
    x, y = x * 1000.0, y * 1000.0
    speed = aircraft.get("gs")
    track = aircraft.get("track", aircraft.get("true_heading"))
    seconds = 0.0
    vx = vy = 0.0
    if (isinstance(speed, (int, float)) and speed >= LOW_HOVER_KNOTS
            and isinstance(track, (int, float))):
        velocity = speed * KNOT_TO_MS
        vx = velocity * math.sin(math.radians(track))
        vy = velocity * math.cos(math.radians(track))
        # Minimum |p + v t| pro t >= 0; za horizontem uz primka neplati.
        seconds = -(x * vx + y * vy) / (vx * vx + vy * vy)
        seconds = min(max(seconds, 0.0), LOW_LOOKAHEAD_SECONDS)
    distance = math.hypot(x + vx * seconds, y + vy * seconds)
    rate = aircraft.get("baro_rate")
    climb = rate * FT_TO_M / 60.0 if isinstance(rate, (int, float)) else 0.0
    return {
        "seconds": seconds,
        "distance_m": distance,
        "height_m": max(height + climb * seconds, 0.0),
    }


COMPASS = ("S", "SV", "V", "JV", "J", "JZ", "Z", "SZ")


def compass(bearing: float) -> str:
    return COMPASS[int((bearing % 360) / 45.0 + 0.5) % 8]


def bearing_from_home(home_lat: float, home_lon: float, lat: float, lon: float) -> float:
    x, y = local_xy_km(home_lat, home_lon, lat, lon)
    return math.degrees(math.atan2(x, y)) % 360


class TypeHistory:
    """Ve kolika dnech se ktery typ ukazal. Na disku jako {typ: [dny]}."""

    def __init__(self, path: Path):
        self.path = path
        self.started = datetime.now(TIMEZONE).date().isoformat()
        self.days: dict[str, set[str]] = {}
        self.dirty = False
        try:
            stored = json.loads(path.read_text())
            self.started = stored.get("started", self.started)
            self.days = {t: set(d) for t, d in stored.get("types", {}).items()}
        except (OSError, ValueError, AttributeError):
            pass

    def is_rare(self, aircraft_type: str, today: str) -> bool:
        started = datetime.fromisoformat(self.started).date()
        day = datetime.fromisoformat(today).date()
        if (day - started).days < RARE_WARMUP_DAYS:
            return False
        window = (day - timedelta(days=RARE_WINDOW_DAYS)).isoformat()
        # Dnesek se nepocita: druhe letadlo stejneho typu za jedno odpoledne
        # typ vzacnym neudela o nic min.
        seen = [d for d in self.days.get(aircraft_type, ()) if window <= d < today]
        return len(seen) < RARE_MAX_DAYS

    def record(self, aircraft_type: str, today: str) -> None:
        days = self.days.setdefault(aircraft_type, set())
        if today not in days:
            days.add(today)
            self.dirty = True

    def save(self, today: str) -> None:
        if not self.dirty:
            return
        cutoff = (datetime.fromisoformat(today).date()
                  - timedelta(days=TYPE_HISTORY_DAYS)).isoformat()
        self.days = {t: {d for d in ds if d >= cutoff} for t, ds in self.days.items()}
        self.days = {t: ds for t, ds in self.days.items() if ds}
        write_json(self.path, {"started": self.started,
                               "types": {t: sorted(ds) for t, ds in sorted(self.days.items())}})
        self.dirty = False


def describe_aircraft(aircraft: dict) -> str:
    parts = []
    callsign = str(aircraft.get("flight", "")).strip()
    registration = str(aircraft.get("r", "")).strip()
    if callsign:
        parts.append(callsign)
    if registration and registration != callsign:
        parts.append(registration)
    description = str(aircraft.get("desc", "")).strip() or str(aircraft.get("t", "")).strip()
    if description:
        parts.append(description)
    return " · ".join(parts) or aircraft.get("hex", "?").upper()


def plane_reasons(aircraft: dict, config: dict, elevation_m: float | None,
                  history: TypeHistory, today: str) -> dict[str, str]:
    """Duvody, proc o letadle dat vedet, s radkem textu ke kazdemu."""
    reasons: dict[str, str] = {}
    lat, lon = aircraft.get("lat"), aircraft.get("lon")
    if not isinstance(lat, (int, float)) or not isinstance(lon, (int, float)):
        return reasons
    settings = config["planes"]
    home_lat, home_lon = config["lat"], config["lon"]
    x, y = local_xy_km(home_lat, home_lon, lat, lon)
    distance_km = math.hypot(x, y)
    flags = aircraft.get("dbFlags", 0)
    flags = flags if isinstance(flags, int) else 0
    where = f"{distance_km:.0f} km {compass(bearing_from_home(home_lat, home_lon, lat, lon))}"

    if distance_km <= settings["radius"]:
        if settings["military"] and flags & 1:
            reasons["military"] = f"Vojenské letadlo {where}"
        aircraft_type = str(aircraft.get("t", "")).strip()
        if settings["rare"] and (flags & 2 or (aircraft_type and history.is_rare(aircraft_type, today))):
            reasons["rare"] = f"Vzácný typ {where}"

    if settings["low"] and elevation_m is not None:
        predicted = predict_pass(aircraft, home_lat, home_lon, elevation_m)
        if (predicted is not None and predicted["distance_m"] <= settings["lowRadius"]
                and predicted["height_m"] <= settings["lowHeight"]):
            when = ("teď" if predicted["seconds"] < 20
                    else f"za {max(1, round(predicted['seconds'] / 60))} min")
            reasons["low"] = (
                f"Nízký přelet {when}: ~{predicted['height_m']:.0f} m nad zemí, "
                f"{predicted['distance_m'] / 1000:.1f} km od domu".replace(".", ",")
            )
    return reasons


# ---------------------------------------------------------------------------
# Sit a disk
# ---------------------------------------------------------------------------

def write_json(path: Path, payload: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile("w", dir=path.parent, delete=False) as handle:
        json.dump(payload, handle, ensure_ascii=False, separators=(",", ":"))
        temporary = Path(handle.name)
    os.chmod(temporary, 0o600)
    temporary.replace(path)


def get_json(url: str) -> object:
    request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT,
                                                   "Accept": "application/json"})
    with urllib.request.urlopen(request, timeout=HTTP_TIMEOUT_SECONDS) as response:
        return json.loads(response.read(1024 * 1024))


def send_push(title: str, message: str, tags: list[str], priority: int,
              click: str = "") -> bool:
    if not NTFY_TOPIC:
        log(f"push bez tematu: {title} / {message}")
        return False
    # JSON na koren serveru misto hlavicek: titulek s diakritikou by v HTTP
    # hlavicce nesel poslat bez kodovani.
    body = {"topic": NTFY_TOPIC, "title": title, "message": message,
            "tags": tags, "priority": priority}
    if click:
        body["click"] = click
    headers = {"Content-Type": "application/json", "User-Agent": USER_AGENT}
    if NTFY_TOKEN:
        headers["Authorization"] = f"Bearer {NTFY_TOKEN}"
    request = urllib.request.Request(NTFY_URL, data=json.dumps(body).encode("utf-8"),
                                     headers=headers, method="POST")
    try:
        with urllib.request.urlopen(request, timeout=HTTP_TIMEOUT_SECONDS) as response:
            response.read(4096)
        return True
    except (urllib.error.URLError, OSError) as error:
        log(f"push selhal: {error}")
        return False


def log(message: str) -> None:
    print(message, file=sys.stderr, flush=True)


# ---------------------------------------------------------------------------
# Hlidac
# ---------------------------------------------------------------------------

class Watcher:
    def __init__(self, state_dir: Path):
        self.state_dir = state_dir
        self.lock = threading.Lock()
        self.configs: dict[str, dict] = {}
        self.rain_watches: dict[tuple, RainWatch] = {}
        self.plane_sent: dict[str, float] = {}
        self.elevations: dict[tuple[float, float], float] = {}
        self.elevation_retry: dict[tuple[float, float], float] = {}
        self.history = TypeHistory(state_dir / "types.json")
        self.pushes = 0
        self.last_rain = 0.0
        self.last_planes = 0.0
        self.last_error = ""
        self._load()

    def _load(self) -> None:
        for path in sorted((self.state_dir / "configs").glob("*.json")):
            try:
                self.configs[path.stem] = parse_config(json.loads(path.read_text()))
            except (OSError, ValueError) as error:
                log(f"nastaveni {path.name} nejde nacist: {error}")
        try:
            stored = json.loads((self.state_dir / "elevations.json").read_text())
            self.elevations = {tuple(map(float, key.split(","))): value
                               for key, value in stored.items()}
        except (OSError, ValueError):
            pass

    def store_config(self, config_id: str, config: dict) -> bool:
        with self.lock:
            if config_id not in self.configs and len(self.configs) >= MAX_CONFIGS:
                return False
            self.configs[config_id] = config
        write_json(self.state_dir / "configs" / f"{config_id}.json", config)
        return True

    def snapshot(self) -> list[dict]:
        with self.lock:
            return [c for c in self.configs.values() if c["enabled"]]

    # -- dest ---------------------------------------------------------------

    def check_rain(self, now: float | None = None) -> None:
        now = time.time() if now is None else now
        local = datetime.fromtimestamp(now, TIMEZONE)
        fetched: dict[tuple, dict | None] = {}
        for config in self.snapshot():
            rain = config["rain"]
            if not rain["enabled"]:
                continue
            place = location_key(config)
            fetch_key = place + (rain["radius"],)
            if fetch_key not in fetched:
                fetched[fetch_key] = self._rain_forecast(config["lat"], config["lon"],
                                                         rain["radius"], now)
            forecast = fetched[fetch_key]
            if forecast is None:
                continue
            state, minutes, dbz = rain_state(forecast, rain["lead"], rain["dbz"])
            watch = self.rain_watches.setdefault(fetch_key + (rain["lead"], rain["dbz"]),
                                                 RainWatch())
            # V nocnim klidu se push neposle, ale ani nevybije: kdyz bude dest
            # porad na ceste i po jeho konci, prijde tehdy.
            if state == "coming" and watch.armed and is_quiet(config, local):
                watch.last_state = state
                continue
            if watch.update(state, now):
                self.pushes += send_push(
                    f"Déšť za {minutes} min",
                    f"Radar ČHMÚ čeká {rain_word(dbz)} ({dbz} dBZ) do {rain['radius']} km "
                    f"od {config['name'] or 'hodin'}. Teď ještě neprší.",
                    ["umbrella"], 4)
        self.last_rain = now

    def _rain_forecast(self, lat: float, lon: float, radius: int, now: float) -> dict | None:
        query = urlencode({"lat": f"{lat:.4f}", "lon": f"{lon:.4f}", "r": radius})
        try:
            forecast = get_json(f"{RAIN_URL}?{query}")
        except (urllib.error.URLError, OSError, ValueError) as error:
            self.last_error = f"rain: {error}"
            return None
        slot = forecast.get("slot") if isinstance(forecast, dict) else None
        if not isinstance(slot, (int, float)) or now - slot > RAIN_MAX_AGE_SECONDS:
            self.last_error = "rain: predpoved je stara"
            return None
        return forecast

    # -- letadla ------------------------------------------------------------

    def check_planes(self, now: float | None = None) -> None:
        now = time.time() if now is None else now
        local = datetime.fromtimestamp(now, TIMEZONE)
        today = local.date().isoformat()
        configs = [c for c in self.snapshot()
                   if c["planes"]["military"] or c["planes"]["rare"] or c["planes"]["low"]]
        by_place: dict[tuple[float, float], list[dict]] = {}
        for config in configs:
            by_place.setdefault(location_key(config), []).append(config)

        for place, group in by_place.items():
            reach_km = max(max(c["planes"]["radius"], c["planes"]["lowRadius"] / 1000 + 20)
                           for c in group)
            aircraft = self._aircraft(group[0]["lat"], group[0]["lon"], reach_km)
            if aircraft is None:
                continue
            elevation = self._elevation(place, group[0]["lat"], group[0]["lon"], now)
            for plane in aircraft:
                self._consider(plane, place, group, elevation, today, local, now)
            # Az po vyhodnoceni: letadlo, ktere typ zapise, ho pak samo nesmi
            # prestat delat vzacnym.
            for plane in aircraft:
                aircraft_type = str(plane.get("t", "")).strip()
                if aircraft_type:
                    self.history.record(aircraft_type, today)
        self.history.save(today)
        self.plane_sent = {k: t for k, t in self.plane_sent.items()
                           if now - t < max(PLANE_REPEAT_SECONDS.values())}
        self.last_planes = now

    def _consider(self, plane: dict, place: tuple, group: list[dict],
                  elevation: float | None, today: str, local: datetime, now: float) -> None:
        hex_code = str(plane.get("hex", "")).lower()
        if not hex_code:
            return
        reasons: dict[str, str] = {}
        for config in group:
            if is_quiet(config, local):
                continue
            for kind, line in plane_reasons(plane, config, elevation, self.history, today).items():
                reasons.setdefault(kind, line)
        fresh = {kind: line for kind, line in reasons.items()
                 if now - self.plane_sent.get(f"{kind}:{hex_code}:{place}", 0)
                 >= PLANE_REPEAT_SECONDS[kind]}
        if not fresh:
            return
        order = [kind for kind in ("low", "military", "rare") if kind in fresh]
        titles = {"low": "Nízký přelet", "military": "Vojenské letadlo", "rare": "Vzácné letadlo"}
        tags = {"low": "small_airplane", "military": "military_helmet", "rare": "sparkles"}
        lines = [describe_aircraft(plane)] + [fresh[kind] for kind in order]
        lines.append("Data: adsb.fi")
        sent = send_push(titles[order[0]], "\n".join(lines),
                         [tags[kind] for kind in order], 4 if "low" in fresh else 3,
                         f"https://globe.adsb.fi/?icao={hex_code}")
        self.pushes += sent
        # I neodeslany push se pamatuje: ntfy, ktere zrovna nejde, by jinak
        # dostalo tentyz prelet kazdych deset sekund, az se probere.
        for kind in fresh:
            self.plane_sent[f"{kind}:{hex_code}:{place}"] = now

    def _aircraft(self, lat: float, lon: float, reach_km: float) -> list[dict] | None:
        query = urlencode({"lat": f"{lat:.4f}", "lon": f"{lon:.4f}",
                           "dist": f"{max(reach_km / NM_TO_KM, 1.0):.1f}"})
        try:
            payload = get_json(f"{PLANES_URL}?{query}")
        except (urllib.error.URLError, OSError, ValueError) as error:
            self.last_error = f"planes: {error}"
            return None
        aircraft = payload.get("ac") if isinstance(payload, dict) else None
        return [a for a in aircraft if isinstance(a, dict)] if isinstance(aircraft, list) else None

    def _elevation(self, place: tuple, lat: float, lon: float, now: float) -> float | None:
        if place in self.elevations:
            return self.elevations[place]
        if now < self.elevation_retry.get(place, 0):
            return None
        query = urlencode({"latitude": f"{lat:.4f}", "longitude": f"{lon:.4f}"})
        try:
            value = get_json(f"{ELEVATION_URL}?{query}")["elevation"][0]
            if not isinstance(value, (int, float)):
                raise ValueError("elevation is not a number")
        except (urllib.error.URLError, OSError, ValueError, KeyError, IndexError, TypeError) as error:
            # Bez nadmorske vysky by "nad zemi" bylo o stovky metru vedle; nizke
            # prelety se do te doby nehlasi a zkusi se to za hodinu znovu.
            self.last_error = f"elevation: {error}"
            self.elevation_retry[place] = now + 3600
            return None
        self.elevations[place] = float(value)
        write_json(self.state_dir / "elevations.json",
                   {f"{k[0]},{k[1]}": v for k, v in self.elevations.items()})
        return float(value)

    def elevation_for(self, config: dict) -> float | None:
        return self._elevation(location_key(config), config["lat"], config["lon"], time.time())

    def status(self) -> dict:
        with self.lock:
            configs = {key: {"name": c["name"], "enabled": c["enabled"],
                             "rain": c["rain"]["enabled"],
                             "planes": [k for k in ("military", "rare", "low") if c["planes"][k]]}
                       for key, c in self.configs.items()}
        return {
            "configs": configs,
            "topic": bool(NTFY_TOPIC),
            "pushes": self.pushes,
            "lastRain": int(self.last_rain),
            "lastPlanes": int(self.last_planes),
            "rain": {",".join(map(str, k)): w.last_state for k, w in self.rain_watches.items()},
            "types": len(self.history.days),
            "historySince": self.history.started,
            "elevations": {f"{k[0]},{k[1]}": v for k, v in self.elevations.items()},
            "lastError": self.last_error,
        }


def run_loop(period: float, step) -> None:
    while True:
        started = time.monotonic()
        try:
            step()
        except Exception as error:  # noqa: BLE001 - hlidac nesmi umrit na jednom letadle
            log(f"chyba: {error!r}")
        time.sleep(max(1.0, period - (time.monotonic() - started)))


WATCHER: Watcher | None = None


class Handler(BaseHTTPRequestHandler):
    server_version = "alerts-web/1.0"
    timeout = 15

    def _send(self, code: int, body: bytes = b"", ctype: str = "text/plain; charset=utf-8"):
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        if urlparse(self.path).path != "/alerts/status":
            self._send(404, b"not found\n")
            return
        body = json.dumps(WATCHER.status(), ensure_ascii=False, indent=1).encode("utf-8")
        self._send(200, body, "application/json; charset=utf-8")

    def do_PUT(self):
        path = urlparse(self.path).path
        prefix = "/alerts/config/"
        config_id = path[len(prefix):] if path.startswith(prefix) else ""
        if CONFIG_ID.match(config_id) is None:
            self._send(404, b"not found\n")
            return
        try:
            length = int(self.headers.get("Content-Length", "0"))
        except ValueError:
            length = -1
        if not 0 < length <= MAX_BODY_BYTES:
            self._send(413 if length > MAX_BODY_BYTES else 400, b"bad body length\n")
            return
        try:
            config = parse_config(json.loads(self.rfile.read(length)))
        except (ValueError, UnicodeDecodeError) as error:
            self._send(400, f"{error}\n".encode("utf-8"))
            return
        if not WATCHER.store_config(config_id, config):
            self._send(507, b"too many clocks\n")
            return
        elevation = WATCHER.elevation_for(config)
        self._send(200, json.dumps({"elevation": elevation}).encode("utf-8"),
                   "application/json; charset=utf-8")

    def log_message(self, *args):
        pass


def main() -> None:
    global WATCHER
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--test-push", action="store_true", help="poslat zkusebni push a skoncit")
    arguments = parser.parse_args()
    if arguments.test_push:
        ok = send_push("Hodiny: zkušební upozornění",
                       "Tohle téma dostává upozornění na déšť a neobvyklá letadla.",
                       ["white_check_mark"], 3)
        sys.exit(0 if ok else 1)
    WATCHER = Watcher(STATE)
    threading.Thread(target=run_loop, args=(RAIN_PERIOD_SECONDS, WATCHER.check_rain),
                     daemon=True).start()
    threading.Thread(target=run_loop, args=(PLANES_PERIOD_SECONDS, WATCHER.check_planes),
                     daemon=True).start()
    ThreadingHTTPServer((BIND, PORT), Handler).serve_forever()


if __name__ == "__main__":
    main()
