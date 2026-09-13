#!/usr/bin/env python3
"""Maly server agendy: vraci jen /agenda.json, nic jineho.

Zamerne neposkytuje vypis adresare ani jine cesty, aby byl utocny povrch co
nejmensi. Bezi pod systemd jako sluzba agenda-web.service.

Posloucha jen na 127.0.0.1, protoze jedinym klientem je Caddy na stejnem
stroji. Kdyby visel na verejne IP, dalo by se heslo z Caddyfile obejit
primym dotazem na port 8089.

Heslo k agende resi basic_auth v Caddy. Tady se overuje jen druhe heslo, to
k soukromym kalendarum: hodiny ho posilaji v hlavicce X-Agenda-Key. Heslo
k agende totiz zna kazdy, kdo vidi webove rozhrani hodin, protoze je soucasti
adresy - na soukromy kalendar proto nestaci.

Odpoved se sklada pri kazdem dotazu ze snimku, ktery zapisuje generate.py:
?hide=1,3 schova kalendare, ktere majitel ve webovem rozhrani odskrtl.

    python serve.py --hash    vypise AGENDA_PRIVATE_HASH pro zadane heslo
"""
import getpass
import json
import os
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlsplit

from feed import hash_key, key_matches, parse_hidden, render

WWW = Path(os.environ.get("AGENDA_WWW", "/opt/agenda/www"))
PORT = int(os.environ.get("AGENDA_PORT", "8089"))
FILE = WWW / "events.json"
PRIVATE_HASH = os.environ.get("AGENDA_PRIVATE_HASH", "").strip()
# Hadani hesla brzdi uz basic_auth v Caddy - kdo se sem dostal, zna heslo
# k agende. Pojistka navic pro pripad, ze by uniklo: po tolika chybach za
# ctvrt hodiny se soukrome kalendare neodemknou nikomu, ani se spravnym heslem.
FAILURE_LIMIT = int(os.environ.get("AGENDA_KEY_FAILURE_LIMIT", "10"))
FAILURE_WINDOW_S = 15 * 60

failures: list[float] = []
failures_lock = threading.Lock()


def recent_failures(now: float) -> int:
    with failures_lock:
        failures[:] = [moment for moment in failures if now - moment < FAILURE_WINDOW_S]
        return len(failures)


def record_failure(now: float) -> None:
    with failures_lock:
        failures.append(now)


class Handler(BaseHTTPRequestHandler):
    server_version = "agenda-web/2.0"
    # ThreadingHTTPServer zaklada vlakno na spojeni. Bez timeoutu staci
    # pomale otevrena spojeni, aby se vlakna nahromadila; s nim se zaseknute
    # spojeni samo zavre.
    timeout = 15

    def _send(self, code: int, body: bytes = b"", ctype: str = "text/plain; charset=utf-8"):
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(body)

    def _unlocked(self) -> bool | None:
        """True/False podle hesla, None pri spatnem hesle. Bez hlavicky se
        soukrome kalendare jen vynechaji - to je bezny stav, ne chyba."""
        key = self.headers.get("X-Agenda-Key", "")
        if not key:
            return False
        now = time.monotonic()
        if recent_failures(now) >= FAILURE_LIMIT:
            return None
        if PRIVATE_HASH and key_matches(key, PRIVATE_HASH):
            return True
        record_failure(now)
        return None

    def do_GET(self):
        url = urlsplit(self.path)
        if url.path not in ("/agenda.json", "/"):
            self._send(404, b"not found\n")
            return
        unlocked = self._unlocked()
        if unlocked is None:
            # 403, ne 200 bez soukromych kalendaru: spatne heslo ma byt videt
            # ve webovem rozhrani hodin, ne se tvarit jako prazdny kalendar.
            self._send(403, b"wrong private calendar key\n")
            return
        try:
            snapshot = json.loads(FILE.read_bytes())
        except (OSError, ValueError):
            # Chybejici nebo nedopsany snimek. Den bez udalosti je naopak
            # v poradku: generator zapise platny JSON s prazdnym polem.
            self._send(503, b"agenda not ready\n")
            return
        hidden = parse_hidden(parse_qs(url.query).get("hide", [""])[0])
        body = json.dumps(
            render(snapshot, hidden, unlocked), ensure_ascii=False, separators=(",", ":")
        ).encode("utf-8")
        self._send(200, body, "application/json; charset=utf-8")

    do_HEAD = do_GET

    def log_message(self, *args):  # tise, at neplni journal
        pass


def print_hash() -> None:
    first = getpass.getpass("Heslo k soukromym kalendarum: ")
    if not first or getpass.getpass("Znovu: ") != first:
        sys.exit("Hesla se neshoduji nebo jsou prazdna.")
    # Hodiny heslo posilaji v HTTP hlavicce, a ta snese jen tisknutelne ASCII.
    if not all(" " <= char <= "~" for char in first) or len(first) > 63:
        sys.exit("Heslo smi mit nejvys 63 tisknutelnych ASCII znaku (bez diakritiky).")
    print(f"AGENDA_PRIVATE_HASH={hash_key(first)}")


if __name__ == "__main__":
    if sys.argv[1:] == ["--hash"]:
        print_hash()
        sys.exit(0)
    ThreadingHTTPServer((os.environ.get("AGENDA_BIND", "127.0.0.1"), PORT),
                        Handler).serve_forever()
