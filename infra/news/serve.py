#!/usr/bin/env python3
"""Minimalni staticky server: vraci jen /top.xml z NEWS_WWW, nic jineho.

Zamerne neposkytuje vypis adresare ani jine cesty, aby byl na verejne IP co
nejmensi utocny povrch. Bezi pod systemd jako sluzba news-web.service.
"""
import os
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

WWW = Path(os.environ.get("NEWS_WWW", "/opt/news/www"))
PORT = int(os.environ.get("NEWS_PORT", "8088"))
FILE = WWW / "top.xml"


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
        if self.path.split("?", 1)[0] in ("/top.xml", "/"):
            try:
                data = FILE.read_bytes()
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
    ThreadingHTTPServer(("0.0.0.0", PORT), Handler).serve_forever()
