#!/usr/bin/env python3
"""Prevypravec realtime blesku z LightningMaps.org pro hodiny.

LightningMaps (data site Blitzortung) posila udery pres WebSocket skoro
v realnem case, ale pro hodiny je to nevhodny tvar:

- spojeni je trvale a sifrovane, takze by kazde hodiny drzely vlastni TLS
  relaci se dvema 16kB buffery,
- vyrez "p", o ktery si klient rekne, server bere jen jako voditko: pri mereni
  13. 9. 2026 dorazilo z pozadovaneho obdelniku jen 23 % uderu, zbytek byl
  z pul Evropy, pres kilobajt za sekundu,
- protokol neni zdokumentovany (vyzva "k", uvodni davka pres 50 kB) a kdyz se
  zmeni, oprava na serveru nevyzaduje novy firmware ve vsech hodinach.

Tenhle server proto drzi JEDNO spojeni pro vsechny hodiny v domacnosti, udery
dofiltruje, drzi posledni pulhodinu v pameti a hodinam vraci jen to, co lezi
v kruhu kolem jejich polohy. Tvar odpovedi schvalne kopiruje zpravu
LightningMaps ({"time":..., "strokes":[{"time":ms,"lat":..,"lon":..,"id":..}]}),
takze firmware ma jediny parser.

Nic se nedrzi dopredu: spojeni se otevre az na prvni dotaz a zavre se, kdyz se
hodiny ctvrt hodiny neozvou. Po pripojeni server posle udery z poslednich minut,
takze ani prvni odpoved po probuzeni neni prazdna.

Data patri prispevatelum Blitzortung.org a nesmi se dal zverejnovat. Server
proto posloucha jen na 127.0.0.1 a Caddy ho pousti jen s heslem - je to
soukromy zdroj pro vlastni hodiny, ne verejne API.

GET /lightning.json?lat=49.9&lon=14.8&r=120&since=1789332254
    lat, lon  stred kruhu ve stupnich
    r         polomer v km (1 az MAX_RADIUS_KM)
    since     nepovinne; "time" z predchozi odpovedi. Vrati jen udery, ktere
              server prijal az po nem. Pocita se podle casu prijeti, ne
              uderu: LightningMaps nektere udery dorucuje i minuty pozde
              (namereno az 223 s) a podle casu uderu by se ztratily.
"""
from __future__ import annotations

import base64
import hashlib
import json
import math
import os
import random
import socket
import ssl
import struct
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlsplit

PORT = int(os.environ.get("LIGHTNING_PORT", "8093"))
BIND = os.environ.get("LIGHTNING_BIND", "127.0.0.1")
UPSTREAM_HOSTS = [
    host.strip()
    for host in os.environ.get(
        "LIGHTNING_UPSTREAM_HOSTS", "live2.lightningmaps.org,live.lightningmaps.org"
    ).split(",")
    if host.strip()
]
USER_AGENT = os.environ.get(
    "LIGHTNING_USER_AGENT",
    "WaveshareHodiny/1.0 (+https://github.com/CooLajz/waveshare-hodiny)",
)
# Server bez hlavicky Origin z webove mapy spojeni neprijme.
ORIGIN = "https://www.lightningmaps.org"
WEBSOCKET_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

# Hodiny kresli dvacet minut a vystrahu umi hlidat nejvys tricet.
RETENTION_SECONDS = 35 * 60
MAX_RADIUS_KM = 400.0
# Strop odpovedi. Silna bourka nad radarem hodin da stovky uderu za dvacet
# minut; tisicovka je rezerva a pri ~60 bajtech na uder porad pod 64 kB.
MAX_STROKES = 1000
# Bez dotazu hodin se spojeni po teto dobe zavre.
IDLE_DISCONNECT_SECONDS = 15 * 60
# Kruhy, ktere se pocitaji do vyrezu. Hodiny se ptaji nejvys po minute, takze
# kruh starsi nez tohle uz nikdo nepotrebuje.
AREA_MEMORY_SECONDS = 10 * 60
# Rezerva kolem kruhu hodin ve vyrezu. Hodiny se po posunu polohy o par set metru
# nemusi prihlasovat znovu a okrajovy uder nevypadne kvuli zaokrouhleni.
AREA_MARGIN_KM = 10.0
# Server posila tep kazdych deset sekund. Tricet sekund ticha je mrtve spojeni.
STALE_SECONDS = 30
RECONNECT_MIN_SECONDS = 5
RECONNECT_MAX_SECONDS = 300
SOCKET_TIMEOUT_SECONDS = 15
# Uvodni davka ma i pres 50 kB; vic nez tohle neni zprava, ale chyba.
MAX_MESSAGE_BYTES = 1024 * 1024


def log(message: str) -> None:
    print(message, file=sys.stderr, flush=True)


def distance_km(lat_a: float, lon_a: float, lat_b: float, lon_b: float) -> float:
    lat1 = math.radians(lat_a)
    lat2 = math.radians(lat_b)
    half_lat = (lat2 - lat1) / 2
    half_lon = math.radians(lon_b - lon_a) / 2
    a = math.sin(half_lat) ** 2 + math.cos(lat1) * math.cos(lat2) * math.sin(half_lon) ** 2
    return 2 * 6371.0088 * math.atan2(math.sqrt(min(a, 1.0)), math.sqrt(max(0.0, 1 - a)))


def bounding_box(lat: float, lon: float, radius_km: float) -> tuple[float, float, float, float]:
    """Obdelnik (sever, vychod, jih, zapad), ktery pokryje kruh."""
    lat_span = radius_km / 111.32
    lon_span = radius_km / (111.32 * max(0.05, math.cos(math.radians(lat))))
    return (
        min(85.0, lat + lat_span),
        min(180.0, lon + lon_span),
        max(-85.0, lat - lat_span),
        max(-180.0, lon - lon_span),
    )


def union_box(boxes: list[tuple[float, float, float, float]]) -> tuple[float, float, float, float] | None:
    if not boxes:
        return None
    return (
        max(box[0] for box in boxes),
        max(box[1] for box in boxes),
        min(box[2] for box in boxes),
        min(box[3] for box in boxes),
    )


def box_contains(outer: tuple[float, float, float, float] | None,
                 inner: tuple[float, float, float, float]) -> bool:
    return (outer is not None and outer[0] >= inner[0] and outer[1] >= inner[1]
            and outer[2] <= inner[2] and outer[3] <= inner[3])


def subscription_message(box: tuple[float, float, float, float]) -> str:
    """Prihlaseni vyrezu ve tvaru, jaky posila webova mapa."""
    north, east, south, west = box
    # Zaokrouhluje se ven, aby okraj neurizl udery, ktere do vyrezu patri.
    view = [
        math.ceil(north * 10) / 10,
        math.ceil(east * 10) / 10,
        math.floor(south * 10) / 10,
        math.floor(west * 10) / 10,
    ]
    return json.dumps(
        {
            "v": 24, "i": {}, "s": False, "x": 0, "w": 0, "tx": 0, "tw": 1,
            "a": 4, "z": 7, "b": True, "h": "", "l": 1, "t": 1,
            "from_lightningmaps_org": True, "p": view,
        },
        separators=(",", ":"),
    )


def challenge_reply(key: float, now_ms: int) -> str:
    """Odpoved na vyzvu "k" stejnym vypoctem jako webovy klient."""
    return '{"k": %.3f }' % (math.fmod(key * 3604, 7081) * now_ms / 100)


class StrokeStore:
    """Udery podle id. Po znovupripojeni server posle cast poslednich uderu
    znovu, id je pozna."""

    def __init__(self) -> None:
        self._lock = threading.Lock()
        # id -> (cas uderu v ms, lat, lon, cas prijeti v s)
        self._strokes: dict[int, tuple[int, float, float, float]] = {}

    def add(self, strokes: list, box: tuple[float, float, float, float] | None,
            now: float) -> tuple[int, int]:
        added = outside = 0
        cutoff_ms = int((now - RETENTION_SECONDS) * 1000)
        with self._lock:
            for stroke in strokes:
                try:
                    stroke_id = int(stroke["id"])
                    time_ms = int(stroke["time"])
                    lat = float(stroke["lat"])
                    lon = float(stroke["lon"])
                except (KeyError, TypeError, ValueError):
                    continue
                if not (-90 <= lat <= 90 and -180 <= lon <= 180) or time_ms < cutoff_ms:
                    continue
                if box is None or not (box[2] <= lat <= box[0] and box[3] <= lon <= box[1]):
                    outside += 1
                    continue
                if stroke_id in self._strokes:
                    continue
                self._strokes[stroke_id] = (time_ms, lat, lon, now)
                added += 1
        return added, outside

    def prune(self, now: float) -> None:
        cutoff_ms = int((now - RETENTION_SECONDS) * 1000)
        with self._lock:
            for stroke_id in [key for key, value in self._strokes.items() if value[0] < cutoff_ms]:
                del self._strokes[stroke_id]

    def query(self, lat: float, lon: float, radius_km: float, since: float,
              now: float) -> list[dict]:
        self.prune(now)
        north, east, south, west = bounding_box(lat, lon, radius_km)
        with self._lock:
            candidates = [
                (stroke_id, value) for stroke_id, value in self._strokes.items()
                if value[3] > since and south <= value[1] <= north and west <= value[2] <= east
            ]
        result = [
            {"time": value[0], "lat": round(value[1], 4), "lon": round(value[2], 4), "id": stroke_id}
            for stroke_id, value in candidates
            if distance_km(lat, lon, value[1], value[2]) <= radius_km
        ]
        result.sort(key=lambda item: item["time"])
        # Pri preteceni vyhravaji nejnovejsi: stary uder uz na displeji skoro
        # zmizel, novy je ten, na ktery se ceka.
        return result[-MAX_STROKES:]

    def __len__(self) -> int:
        with self._lock:
            return len(self._strokes)


class WebSocketClosed(Exception):
    pass


class WebSocket:
    """Minimalni klient RFC 6455 nad standardni knihovnou - sluzba nema venv."""

    def __init__(self, host: str) -> None:
        raw = socket.create_connection((host, 443), timeout=SOCKET_TIMEOUT_SECONDS)
        context = ssl.create_default_context()
        self.sock = context.wrap_socket(raw, server_hostname=host)
        self.sock.settimeout(SOCKET_TIMEOUT_SECONDS)
        self._send_lock = threading.Lock()
        key = base64.b64encode(os.urandom(16)).decode()
        request = (
            f"GET / HTTP/1.1\r\nHost: {host}\r\nUpgrade: websocket\r\n"
            f"Connection: Upgrade\r\nSec-WebSocket-Key: {key}\r\n"
            f"Sec-WebSocket-Version: 13\r\nOrigin: {ORIGIN}\r\n"
            f"User-Agent: {USER_AGENT}\r\n\r\n"
        )
        self.sock.sendall(request.encode())
        response = b""
        while b"\r\n\r\n" not in response:
            chunk = self.sock.recv(1024)
            if not chunk or len(response) > 16384:
                raise WebSocketClosed("handshake nedokoncen")
            response += chunk
        head, _, self._buffer = response.partition(b"\r\n\r\n")
        lines = head.decode("latin-1").split("\r\n")
        if not lines[0].startswith("HTTP/1.1 101"):
            raise WebSocketClosed(f"server odmitl: {lines[0]}")
        expected = base64.b64encode(hashlib.sha1((key + WEBSOCKET_GUID).encode()).digest()).decode()
        headers = {line.split(":", 1)[0].strip().lower(): line.split(":", 1)[1].strip()
                   for line in lines[1:] if ":" in line}
        if headers.get("sec-websocket-accept") != expected:
            raise WebSocketClosed("neplatny Sec-WebSocket-Accept")

    def _read_exact(self, count: int) -> bytes:
        while len(self._buffer) < count:
            chunk = self.sock.recv(max(4096, count - len(self._buffer)))
            if not chunk:
                raise WebSocketClosed("server zavrel spojeni")
            self._buffer += chunk
        data, self._buffer = self._buffer[:count], self._buffer[count:]
        return data

    def send_frame(self, opcode: int, payload: bytes) -> None:
        mask = os.urandom(4)
        header = bytearray([0x80 | opcode])
        if len(payload) < 126:
            header.append(0x80 | len(payload))
        elif len(payload) < 65536:
            header.append(0x80 | 126)
            header += struct.pack("!H", len(payload))
        else:
            header.append(0x80 | 127)
            header += struct.pack("!Q", len(payload))
        masked = bytes(byte ^ mask[index % 4] for index, byte in enumerate(payload))
        with self._send_lock:
            self.sock.sendall(bytes(header) + mask + masked)

    def send_text(self, text: str) -> None:
        self.send_frame(0x1, text.encode())

    def receive_text(self) -> str:
        """Dalsi textova zprava; ping odpovi sam, close vyhodi vyjimku."""
        message = bytearray()
        while True:
            first, second = self._read_exact(2)
            fin = first & 0x80
            opcode = first & 0x0F
            length = second & 0x7F
            if length == 126:
                length = struct.unpack("!H", self._read_exact(2))[0]
            elif length == 127:
                length = struct.unpack("!Q", self._read_exact(8))[0]
            if second & 0x80:
                mask = self._read_exact(4)
            else:
                mask = None
            if length > MAX_MESSAGE_BYTES or len(message) + length > MAX_MESSAGE_BYTES:
                raise WebSocketClosed(f"zprava {length} B je prilis velka")
            payload = self._read_exact(length)
            if mask:
                payload = bytes(byte ^ mask[index % 4] for index, byte in enumerate(payload))
            if opcode == 0x8:
                raise WebSocketClosed("server poslal close")
            if opcode == 0x9:
                self.send_frame(0xA, payload)
                continue
            if opcode == 0xA:
                continue
            message += payload
            if fin:
                return message.decode("utf-8", errors="replace")

    def close(self) -> None:
        try:
            self.sock.close()
        except OSError:
            pass


class Upstream:
    """Jedno spojeni na LightningMaps sdilene vsemi hodinami."""

    def __init__(self, store: StrokeStore) -> None:
        self.store = store
        self._lock = threading.Lock()
        self._wake = threading.Event()
        # Klic je zaokrouhleny kruh, aby se tytez hodiny nepocitaly za kazdy
        # dotaz znovu; hodnota nese presny kruh a cas posledniho dotazu.
        # Vyrez se musi skladat z presneho kruhu: z zaokrouhleneho stredu by
        # kruh hodin o kus presahoval a spojeni by ho nikdy "nepokryvalo".
        self._areas: dict[tuple[float, float, float], tuple[float, float, float, float]] = {}
        self._subscribed_box: tuple[float, float, float, float] | None = None
        self._socket: WebSocket | None = None
        self.last_request = 0.0
        self.last_message = 0.0
        self.connected_since = 0.0
        self.connects = 0
        self.messages = 0
        self.last_error = ""

    def request_area(self, lat: float, lon: float, radius_km: float, now: float) -> bool:
        """Zapamatuje si kruh hodin a vrati, jestli ho spojeni uz pokryva."""
        key = (round(lat, 1), round(lon, 1), math.ceil(radius_km / 10) * 10)
        with self._lock:
            self._areas[key] = (lat, lon, radius_km, now)
            self.last_request = now
            covered = box_contains(self._subscribed_box, bounding_box(lat, lon, radius_km))
            live = self._socket is not None and now - self.last_message < STALE_SECONDS
        self._wake.set()
        return covered and live

    def _wanted_box(self, now: float) -> tuple[float, float, float, float] | None:
        with self._lock:
            for key in [key for key, area in self._areas.items() if now - area[3] > AREA_MEMORY_SECONDS]:
                del self._areas[key]
            boxes = [bounding_box(lat, lon, radius + AREA_MARGIN_KM)
                     for lat, lon, radius, _ in self._areas.values()]
        return union_box(boxes)

    def status(self, now: float) -> dict:
        with self._lock:
            return {
                "connected": self._socket is not None,
                "lastMessageAge": round(now - self.last_message, 1) if self.last_message else None,
                "connectedFor": round(now - self.connected_since) if self._socket else 0,
                "connects": self.connects,
                "messages": self.messages,
                "strokes": len(self.store),
                "areas": len(self._areas),
                "box": self._subscribed_box,
                "lastError": self.last_error,
            }

    def run(self) -> None:
        backoff = RECONNECT_MIN_SECONDS
        host_index = 0
        while True:
            now = time.time()
            if now - self.last_request > IDLE_DISCONNECT_SECONDS:
                self._wake.clear()
                self._wake.wait(timeout=60)
                continue
            box = self._wanted_box(now)
            if box is None:
                time.sleep(1)
                continue
            host = UPSTREAM_HOSTS[host_index % len(UPSTREAM_HOSTS)]
            try:
                self._session(host, box)
                backoff = RECONNECT_MIN_SECONDS
            except (OSError, WebSocketClosed, ValueError) as error:
                with self._lock:
                    self.last_error = f"{host}: {error}"
                log(f"lightning: {host}: {error}")
                host_index += 1
                time.sleep(backoff + random.uniform(0, 2))
                backoff = min(RECONNECT_MAX_SECONDS, backoff * 2)
            finally:
                with self._lock:
                    if self._socket is not None:
                        self._socket.close()
                    self._socket = None
                    self._subscribed_box = None

    def _session(self, host: str, box: tuple[float, float, float, float]) -> None:
        ws = WebSocket(host)
        now = time.time()
        with self._lock:
            self._socket = ws
            self.connects += 1
            self.connected_since = now
            self.last_message = now
        ws.send_text(subscription_message(box))
        with self._lock:
            self._subscribed_box = box
        log(f"lightning: pripojeno k {host}, vyrez {box}")
        while True:
            now = time.time()
            if now - self.last_request > IDLE_DISCONNECT_SECONDS:
                log("lightning: hodiny se neozvaly, odpojuji")
                return
            if now - self.last_message > STALE_SECONDS:
                raise WebSocketClosed("server mlci")
            wanted = self._wanted_box(now)
            # Novy kruh mimo vyrez: prohlizec v takove situaci posle nove
            # prihlaseni tymz spojenim, bez odpojeni.
            if wanted is not None and not box_contains(self._subscribed_box, wanted):
                box = union_box([wanted, self._subscribed_box] if self._subscribed_box else [wanted])
                ws.send_text(subscription_message(box))
                with self._lock:
                    self._subscribed_box = box
                log(f"lightning: rozsiren vyrez {box}")
            # Tep chodi po deseti sekundach, takze vyprseny timeout (OSError)
            # znamena mrtve spojeni. Pokracovat by neslo: cast ramce uz mohla
            # byt prectena a proud by se rozjel.
            text = ws.receive_text()
            message = json.loads(text)
            now = time.time()
            with self._lock:
                self.last_message = now
                self.messages += 1
                subscribed = self._subscribed_box
            if isinstance(message, dict):
                if "k" in message:
                    ws.send_text(challenge_reply(float(message["k"]), int(now * 1000)))
                strokes = message.get("strokes")
                if isinstance(strokes, list):
                    self.store.add(strokes, subscribed, now)


store = StrokeStore()
upstream = Upstream(store)


def parse_query(query: str) -> tuple[float, float, float, float]:
    params = parse_qs(query)

    def number(name: str) -> float:
        value = float(params[name][0])
        if not math.isfinite(value):
            raise ValueError(name)
        return value

    lat = number("lat")
    lon = number("lon")
    radius = number("r")
    since = float(params.get("since", ["0"])[0])
    if not (-90 <= lat <= 90 and -180 <= lon <= 180 and 1 <= radius <= MAX_RADIUS_KM
            and math.isfinite(since) and since >= 0):
        raise ValueError("mimo rozsah")
    return lat, lon, radius, since


class Handler(BaseHTTPRequestHandler):
    server_version = "lightning-web/1.0"
    timeout = 20

    def log_message(self, format: str, *args) -> None:  # noqa: A002
        pass

    def _send(self, status: int, body: dict) -> None:
        payload = json.dumps(body, separators=(",", ":")).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(payload)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(payload)

    def do_GET(self) -> None:  # noqa: N802
        url = urlsplit(self.path)
        now = time.time()
        if url.path == "/lightning/status":
            self._send(200, upstream.status(now))
            return
        if url.path != "/lightning.json":
            self._send(404, {"error": "not found"})
            return
        try:
            lat, lon, radius, since = parse_query(url.query)
        except (KeyError, ValueError):
            self._send(400, {"error": "lat, lon a r jsou povinne"})
            return
        live = upstream.request_area(lat, lon, radius, now)
        self._send(200, {
            "time": round(now, 3),
            # false: spojeni se teprve otevira nebo vyrez jeste nepokryva tenhle
            # kruh. Prazdny seznam pak neznamena "neblyska se".
            "live": live,
            "strokes": store.query(lat, lon, radius, since, now),
        })


def main() -> None:
    threading.Thread(target=upstream.run, name="upstream", daemon=True).start()
    server = ThreadingHTTPServer((BIND, PORT), Handler)
    log(f"lightning: posloucham na {BIND}:{PORT}")
    server.serve_forever()


if __name__ == "__main__":
    main()
