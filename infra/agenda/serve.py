#!/usr/bin/env python3
"""Minimalni staticky server: vraci jen /agenda.json z AGENDA_WWW, nic jineho.

Zamerne neposkytuje vypis adresare ani jine cesty, aby byl na verejne IP co
nejmensi utocny povrch. Bezi pod systemd jako sluzba agenda-web.service.

Obsah je stejne verejny jako sam kalendar sdileny se servisnim uctem, takze
tenhle server nic neoveruje; utahovat pristup ma smysl az v Caddy.
"""
import os
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

WWW = Path(os.environ.get("AGENDA_WWW", "/opt/agenda/www"))
PORT = int(os.environ.get("AGENDA_PORT", "8089"))
FILE = WWW / "agenda.json"


class Handler(BaseHTTPRequestHandler):
    server_version = "agenda-web/1.0"
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
        if self.path.split("?", 1)[0] in ("/agenda.json", "/"):
            try:
                data = FILE.read_bytes()
            except OSError:
                self._send(503, b"agenda not ready\n")
                return
            # Prazdny soubor po nedopsanem zapisu vypada jako platna odpoved.
            # Den bez udalosti je naopak v poradku: generator zapise platny
            # JSON s prazdnym polem, ktery sem dorazi jako beznych par set bajtu.
            if not data:
                self._send(503, b"agenda not ready\n")
                return
            self._send(200, data, "application/json; charset=utf-8")
        else:
            self._send(404, b"not found\n")

    do_HEAD = do_GET

    def log_message(self, *args):  # tise, at neplni journal
        pass


if __name__ == "__main__":
    ThreadingHTTPServer(("0.0.0.0", PORT), Handler).serve_forever()
