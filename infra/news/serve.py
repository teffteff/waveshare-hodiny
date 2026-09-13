#!/usr/bin/env python3
"""Minimalni staticky server: vraci jen /top.xml z NEWS_WWW, nic jineho.

Zamerne neposkytuje vypis adresare ani jine cesty, aby byl na verejne IP co
nejmensi utocny povrch. Bezi pod systemd jako sluzba news-web.service.

Hodiny mohou v dotazu poslat svou polohu (?city=Brno&lat=49.19&lon=16.61;
firmware ji doplni za znacky {city}, {lat} a {lon} v adrese kanalu). Server
pak misto spolecneho vyberu vrati vyber pro tu polohu, pokud ho generate.py uz
pripravil, a polohu si zapise, aby ji pri dalsim behu zahrnul. Samotny model
se odsud nevola nikdy: dotaz z verejne IP tak nemuze nic stat, jen zabrat
jedno z mala mist v registru (NEWS_MAX_LOCATIONS).
"""
import os
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs

from locations import (
    MAX_LOCATIONS,
    location_from_query,
    location_output,
    register,
)

WWW = Path(os.environ.get("NEWS_WWW", "/opt/news/www"))
PORT = int(os.environ.get("NEWS_PORT", "8088"))
FILE = WWW / "top.xml"
# Vyber pro polohu, ktery zaostava za spolecnym o vic nez tohle, se nevydava:
# znamena to, ze beh pro tu polohu opakovane selhava, zatimco spolecny prosel.
# Dva behy timeru jsou od sebe dve hodiny, takze jeden vypadek se jeste snese.
LOCAL_MAX_LAG_SECONDS = 3 * 3600


def pick_file(query: str) -> Path:
    location = location_from_query(parse_qs(query))
    if location is None:
        return FILE
    try:
        register(location)
    except OSError as error:
        # Registr je jen prianim do pristiho behu; kvuli nemu nesmi odpoved
        # selhat.
        print(f"registr poloh nejde zapsat: {error}", file=sys.stderr)
    local = location_output(WWW, location)
    try:
        local_mtime = local.stat().st_mtime
    except OSError:
        return FILE
    try:
        if local_mtime < FILE.stat().st_mtime - LOCAL_MAX_LAG_SECONDS:
            return FILE
    except OSError:
        pass
    return local


class Handler(BaseHTTPRequestHandler):
    server_version = "news-web/1.0"
    # Server visi na verejne IP a ThreadingHTTPServer zaklada vlakno na
    # spojeni. Bez timeoutu staci pomale otevrena spojeni, aby se vlakna
    # nahromadila; s nim se zaseknute spojeni samo zavre.
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
        path, _, query = self.path.partition("?")
        if path in ("/top.xml", "/"):
            try:
                data = pick_file(query).read_bytes()
            except OSError:
                self._send(503, b"news not ready\n")
                return
            # Prazdny soubor po nedopsanem zapisu vypada jako platna odpoved;
            # hodiny by na nej rekly "Kanal neobsahuje zadne zpravy".
            if not data:
                self._send(503, b"news not ready\n")
                return
            self._send(200, data, "application/rss+xml; charset=utf-8")
        else:
            self._send(404, b"not found\n")

    do_HEAD = do_GET

    def log_message(self, *args):  # tise, at neplni journal
        pass


if __name__ == "__main__":
    print(f"news-web na portu {PORT}, nejvys {MAX_LOCATIONS} poloh", file=sys.stderr)
    ThreadingHTTPServer((os.environ.get("NEWS_BIND", "127.0.0.1"), PORT),
                        Handler).serve_forever()
