#!/usr/bin/env python3
"""Vymyslene blesky pro vyzkouseni displeje, kdyz se zrovna neblyska.

Posloucha na stejnem portu a odpovida stejnym tvarem jako serve.py, takze
se na serveru jen vymeni za lightning-web.service a Caddy ani hodiny se
nemeni. Kolem polohy, na kterou se hodiny ptaji, vymysli:

- hlavni bunku, ktera za hodinu prejde od zapadu na vychod par km jizne od
  hodin (projde kruhem vystrahy, takze se ukaze i upozorneni),
- slabsi bunku asi 28 km severne, ktera jede stejnym smerem,
- ridke rozhazene udery po celem okoli.

Scenar se opakuje po hodine. Pri prvnim dotazu z nove polohy se doplni
poslednich dvacet minut, aby byla na radaru hned videt stopa podle stari.

Spusteni na serveru (vymeni se za skutecnou sluzbu, Conflicts plati obema
smery, takze start lightning-web mock zase zastavi):

    sudo systemd-run --unit=lightning-mock -p User=lightning \
        -p Conflicts=lightning-web.service -p Environment=LIGHTNING_PORT=8093 \
        /usr/bin/python3.11 /opt/lightning/mock.py
    sudo systemctl start lightning-web.service   # konec, zpet na skutecna data
"""
from __future__ import annotations

import json
import math
import os
import random
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlsplit

PORT = int(os.environ.get("LIGHTNING_PORT", "8093"))
BIND = os.environ.get("LIGHTNING_BIND", "127.0.0.1")
RETENTION_SECONDS = 35 * 60
PREFILL_SECONDS = 20 * 60
CYCLE_SECONDS = 60 * 60
MAX_STROKES = 1000

# Poloha v km (vychod, sever) od hodin v case 0 cyklu, rychlost v km/min,
# rozptyl uderu kolem stredu v km a pocet uderu za minutu.
CELLS = [
    {"start": (-45.0, -4.0), "velocity": (1.5, 0.3), "sigma": 3.5, "rate": 30.0},
    {"start": (-35.0, 28.0), "velocity": (1.1, -0.1), "sigma": 6.0, "rate": 10.0},
]
SCATTER_RADIUS_KM = 55.0
SCATTER_RATE = 3.0


def distance_km(lat_a: float, lon_a: float, lat_b: float, lon_b: float) -> float:
    lat1 = math.radians(lat_a)
    lat2 = math.radians(lat_b)
    half_lat = (lat2 - lat1) / 2
    half_lon = math.radians(lon_b - lon_a) / 2
    a = math.sin(half_lat) ** 2 + math.cos(lat1) * math.cos(lat2) * math.sin(half_lon) ** 2
    return 2 * 6371.0088 * math.atan2(math.sqrt(min(a, 1.0)), math.sqrt(max(0.0, 1 - a)))


def offset(lat: float, lon: float, east_km: float, north_km: float) -> tuple[float, float]:
    return (lat + north_km / 111.32,
            lon + east_km / (111.32 * max(0.05, math.cos(math.radians(lat)))))


def poisson(mean: float) -> int:
    # Knuthuv algoritmus; stredni hodnoty jsou tu male.
    limit = math.exp(-mean)
    count, product = 0, random.random()
    while product > limit:
        count += 1
        product *= random.random()
    return count


class Area:
    """Vymyslena bourka kolem jedne polohy hodin."""

    def __init__(self, lat: float, lon: float, now: float) -> None:
        self.lat = lat
        self.lon = lon
        # Hlavni bunka je na zacatku 15 km zapadne, vystraha prijde za par minut.
        self.epoch = now - 20 * 60
        self.generated_until = now - PREFILL_SECONDS
        # (id, cas uderu ms, lat, lon, cas prijeti s)
        self.strokes: list[tuple[int, int, float, float, float]] = []

    def _generate(self, start: float, end: float, received: float, next_id) -> None:
        minutes = (end - start) / 60
        for cell in CELLS:
            for _ in range(poisson(cell["rate"] * minutes)):
                at = random.uniform(start, end)
                phase = ((at - self.epoch) % CYCLE_SECONDS) / 60
                east = cell["start"][0] + cell["velocity"][0] * phase + random.gauss(0, cell["sigma"])
                north = cell["start"][1] + cell["velocity"][1] * phase + random.gauss(0, cell["sigma"])
                self._add(next_id(), at, east, north, received)
        for _ in range(poisson(SCATTER_RATE * minutes)):
            at = random.uniform(start, end)
            angle = random.uniform(0, 2 * math.pi)
            radius = SCATTER_RADIUS_KM * math.sqrt(random.random())
            self._add(next_id(), at, radius * math.cos(angle), radius * math.sin(angle), received)

    def _add(self, stroke_id: int, at: float, east: float, north: float, received: float) -> None:
        lat, lon = offset(self.lat, self.lon, east, north)
        self.strokes.append((stroke_id, int(at * 1000), lat, lon, received))

    def advance(self, now: float, next_id) -> None:
        if now > self.generated_until:
            self._generate(self.generated_until, now, now, next_id)
            self.generated_until = now
        cutoff_ms = int((now - RETENTION_SECONDS) * 1000)
        self.strokes = [stroke for stroke in self.strokes if stroke[1] >= cutoff_ms]


class Mock:
    def __init__(self) -> None:
        self._lock = threading.Lock()
        self._areas: dict[tuple[float, float], Area] = {}
        self._next_id = int(time.time()) * 10

    def _id(self) -> int:
        self._next_id += 1
        return self._next_id

    def query(self, lat: float, lon: float, radius: float, since: float, now: float) -> list[dict]:
        key = (round(lat, 1), round(lon, 1))
        with self._lock:
            area = self._areas.get(key)
            if area is None:
                area = self._areas[key] = Area(lat, lon, now)
            area.advance(now, self._id)
            strokes = list(area.strokes)
        result = [
            {"time": time_ms, "lat": round(s_lat, 4), "lon": round(s_lon, 4), "id": stroke_id}
            for stroke_id, time_ms, s_lat, s_lon, received in strokes
            if received > since and distance_km(lat, lon, s_lat, s_lon) <= radius
        ]
        result.sort(key=lambda item: item["time"])
        return result[-MAX_STROKES:]

    def status(self) -> dict:
        with self._lock:
            return {"mock": True, "areas": [
                {"lat": area.lat, "lon": area.lon, "strokes": len(area.strokes)}
                for area in self._areas.values()
            ]}


mock = Mock()


class Handler(BaseHTTPRequestHandler):
    server_version = "lightning-mock/1.0"
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
            self._send(200, mock.status())
            return
        if url.path != "/lightning.json":
            self._send(404, {"error": "not found"})
            return
        try:
            params = parse_qs(url.query)
            lat = float(params["lat"][0])
            lon = float(params["lon"][0])
            radius = float(params["r"][0])
            since = float(params.get("since", ["0"])[0])
        except (KeyError, ValueError):
            self._send(400, {"error": "lat, lon a r jsou povinne"})
            return
        self._send(200, {"time": round(now, 3), "live": True,
                         "strokes": mock.query(lat, lon, radius, since, now)})


def main() -> None:
    server = ThreadingHTTPServer((BIND, PORT), Handler)
    print(f"lightning-mock: posloucham na {BIND}:{PORT}", flush=True)
    server.serve_forever()


if __name__ == "__main__":
    main()
