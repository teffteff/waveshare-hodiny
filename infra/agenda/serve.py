#!/usr/bin/env python3
"""Minimalni staticky server: vraci jen /agenda.json z AGENDA_WWW, nic jineho.

Zamerne neposkytuje vypis adresare ani jine cesty, aby byl utocny povrch co
nejmensi. Bezi pod systemd jako sluzba agenda-web.service.

Posloucha jen na 127.0.0.1, protoze jedinym klientem je Caddy na stejnem
stroji. Kdyby visel na verejne IP, dalo by se heslo z Caddyfile obejit
primym dotazem na port 8089.

Sam server nic neoveruje: heslo resi basic_auth v Caddy, aby zustalo na
jednom miste.
"""
import os
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

WWW = Path(os.environ.get("AGENDA_WWW", "/opt/agenda/www"))
PORT = int(os.environ.get("AGENDA_PORT", "8089"))
FILE = WWW / "agenda.json"


class Handler(BaseHTTPRequestHandler):
    server_version = "agenda-web/1.0"
    # ThreadingHTTPServer zaklada vlakno na spojeni. Bez timeoutu staci
    # pomale otevrena spojeni, aby se vlakna nahromadila; s nim se zaseknute
    # spojeni samo zavre.
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
    ThreadingHTTPServer((os.environ.get("AGENDA_BIND", "127.0.0.1"), PORT),
                        Handler).serve_forever()
