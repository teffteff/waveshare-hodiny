#!/usr/bin/env python3
"""Prevypravec letadel pro hodiny: stahne adsb.fi a posle dal jen to podstatne.

Hodiny se ptaji kazdych 5 az 15 sekund a odpoved adsb.fi ma pri dosahu 100 km
pres 50 kB, z toho ale ctou dvanact klicu ze zhruba padesati. Tenhle server je
proto orezava na to, co firmware opravdu parsuje, zahazuje letadla na zemi
a nechava nejblizsi stovku a pul. Ze stejneho vzorku tim zbyde kolem 13 kB.

Tvar odpovedi zustava zamerne stejny jako u adsb.fi ({"ac":[...]}), takze
firmware nepotrebuje druhy parser a da se prepnout zpatky na primy zdroj
pouhym vymazanim adresy v nastaveni.

Odpovedi se kratce drzi v pameti. Vic hodin v jedne domacnosti tak sdili jedno
stazeni a adsb.fi (verejne API zdarma) dostane min dotazu, ne vic. Nic se
nestahuje dopredu: kdyz se nikdo neptá, server mlci.

Bezi pod systemd jako planes-web.service a posloucha jen na 127.0.0.1, protoze
jedinym klientem je Caddy na stejnem stroji.
"""
from __future__ import annotations

import json
import os
import time
import urllib.error
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from threading import Lock
from urllib.parse import parse_qs, urlparse

PORT = int(os.environ.get("PLANES_PORT", "8090"))
BIND = os.environ.get("PLANES_BIND", "127.0.0.1")
UPSTREAM = os.environ.get("PLANES_UPSTREAM", "https://opendata.adsb.fi/api/v3")
# adsb.fi i adsb.lol prosi, at se volajici predstavi.
USER_AGENT = os.environ.get(
    "PLANES_USER_AGENT",
    "WaveshareHodiny/1.0 (+https://github.com/CooLajz/waveshare-hodiny)",
)
UPSTREAM_TIMEOUT_SECONDS = 12
# Doba, po kterou se odpoved pujcuje dalsim dotazum. Kratsi nez nejkratsi
# interval hodin (5 s), takze se na ni nikdo necaka; delsi by uz bylo videt,
# protoze letadlo za pet sekund ulet i kilometr.
CACHE_SECONDS = 4
# Po chybe se stara odpoved pujcuje jeste chvili dal. Prazdna obloha po jednom
# nepovedenem stazeni vypada jako pravda, ale neni.
STALE_SECONDS = 60
# Strop firmwaru; vic letadel by stejne zahodil, jen by je nejdriv stahl.
MAX_AIRCRAFT = 150
# Dosah se udava v namornich milich, stejne jako u adsb.fi: hodiny si kilometry
# z nastaveni prepoctou uz u sebe a "dist" tudy jen prochazi. Sto namornich mil
# je 185 km, tedy s rezervou nad nejvetsi dosah, ktery umi nastaveni hodin.
MAX_DISTANCE_NM = 250.0
# Klice, ktere firmware opravdu cte (AdsbParser.cpp). Cokoli dalsiho je bajt
# navic na drate i v PSRAM hodin.
KEPT_KEYS = (
    "hex",
    "flight",
    "t",
    "desc",
    "r",
    "squawk",
    "lat",
    "lon",
    "alt_baro",
    "gs",
    "track",
    "true_heading",
    "baro_rate",
)

_cache: dict[tuple[float, float, int], tuple[float, bytes]] = {}
_cache_lock = Lock()


def _trim(payload: dict) -> bytes:
    aircraft = payload.get("ac")
    if aircraft is None:
        aircraft = payload.get("aircraft", [])
    kept = []
    for item in aircraft:
        if not isinstance(item, dict):
            continue
        # Letadla na zemi zahazujeme uz tady, at u letiste neberou misto tem
        # ve vzduchu. Firmware dela totez, jen o 50 kB pozdeji.
        if item.get("alt_baro") == "ground":
            continue
        # "dst" je vzdalenost od stredu dotazu v NM. Radi se podle ni, ale do
        # odpovedi nepatri: firmware si vzdalenost pocita sam z polohy.
        distance = item.get("dst")
        order = distance if isinstance(distance, (int, float)) else float("inf")
        kept.append((order, {key: item[key] for key in KEPT_KEYS if key in item}))
    # Nejblizsi napred, aby strop uriznul ta vzdalena, ne ta nad hlavou.
    kept.sort(key=lambda pair: pair[0])
    body = {
        "ac": [item for _, item in kept[:MAX_AIRCRAFT]],
        "msg": payload.get("msg", "No error"),
        "now": payload.get("now", time.time()),
        "total": len(kept),
    }
    return json.dumps(body, separators=(",", ":")).encode("utf-8")


def _fetch(latitude: float, longitude: float, distance: float) -> bytes:
    url = f"{UPSTREAM}/lat/{latitude:.5f}/lon/{longitude:.5f}/dist/{distance:.1f}"
    request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(request, timeout=UPSTREAM_TIMEOUT_SECONDS) as response:
        payload = json.loads(response.read())
    return _trim(payload)


def _cached(latitude: float, longitude: float, distance: float) -> tuple[bytes | None, str]:
    # Klic se zaokrouhluje na dve desetinna mista, tedy zhruba kilometr. Hodiny
    # posilaji porad tutez polohu, takze presnejsi klic by jen tristil cache.
    key = (round(latitude, 2), round(longitude, 2), distance)
    now = time.monotonic()
    with _cache_lock:
        entry = _cache.get(key)
    if entry is not None and now - entry[0] < CACHE_SECONDS:
        return entry[1], "cache"
    try:
        body = _fetch(latitude, longitude, distance)
    except (urllib.error.URLError, OSError, ValueError, json.JSONDecodeError):
        if entry is not None and now - entry[0] < STALE_SECONDS:
            return entry[1], "stale"
        return None, "error"
    with _cache_lock:
        _cache[key] = (now, body)
        # Hodiny maji ctyri dosahy a jednu polohu; vic nez hrst klicu znamena,
        # ze se na server dobyva nekdo jiny, a pamet mu patrit nema.
        if len(_cache) > 32:
            oldest = sorted(_cache.items(), key=lambda item: item[1][0])
            for stale_key, _ in oldest[: len(_cache) - 32]:
                _cache.pop(stale_key, None)
    return body, "fresh"


class Handler(BaseHTTPRequestHandler):
    server_version = "planes-web/1.0"
    # ThreadingHTTPServer zaklada vlakno na spojeni. Bez timeoutu staci pomale
    # otevrene spojeni, aby se vlakna nahromadila.
    timeout = 15

    def _send(self, code: int, body: bytes = b"", ctype: str = "text/plain; charset=utf-8"):
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-cache")
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(body)

    def do_GET(self):
        parsed = urlparse(self.path)
        if parsed.path not in ("/planes.json", "/"):
            self._send(404, b"not found\n")
            return
        query = parse_qs(parsed.query)
        try:
            latitude = float(query.get("lat", [""])[0])
            longitude = float(query.get("lon", [""])[0])
            distance = round(float(query.get("dist", [""])[0]), 1)
        except (TypeError, ValueError):
            self._send(400, b"expected lat, lon and dist\n")
            return
        if not -90.0 <= latitude <= 90.0 or not -180.0 <= longitude <= 180.0:
            self._send(400, b"latitude or longitude out of range\n")
            return
        if distance < 1.0:
            self._send(400, b"dist must be at least 1 nautical mile\n")
            return
        # Strop je tu kvuli adsb.fi, ne kvuli hodinam: dotaz na pul kontinentu
        # vraci megabajty a tenhle server nema byt cesta, jak ho o ne pripravit.
        distance = min(distance, MAX_DISTANCE_NM)

        body, source = _cached(latitude, longitude, distance)
        if body is None:
            self._send(502, b"upstream unavailable\n")
            return
        self.send_response(200)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-cache")
        # Jen pro cloveka s curlem; hodiny hlavicku nectou.
        self.send_header("X-Planes-Source", source)
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(body)

    do_HEAD = do_GET

    def log_message(self, *args):  # tise, at neplni journal
        pass


if __name__ == "__main__":
    ThreadingHTTPServer((BIND, PORT), Handler).serve_forever()
