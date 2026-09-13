#!/usr/bin/env python3
"""Uloziste zaloh nastaveni hodin: seznam, stazeni a ulozeni pod nazvem.

  GET  /settings/          seznam zaloh (nazev, cas, firmware, s tajemstvimi?)
  GET  /settings/<nazev>   jedna zaloha tak, jak ji hodiny poslaly
  PUT  /settings/<nazev>   ulozi nebo prepise zalohu
  DELETE /settings/<nazev> smaze zalohu

Zaloha muze nest token Home Assistantu a heslo webu, takze:

- posloucha jen na 127.0.0.1 a heslo resi basic_auth v Caddy, stejne jako
  u agendy; primy dotaz na port by heslo obesel,
- nazev je z uzkeho alfabetu, aby z nej nesla poskladat cesta mimo adresar,
- soubory maji prava 600 v adresari s pravy 700,
- telo ma strop a musi to byt obalka zalohy hodin, ne libovolny soubor,
- pocet zaloh je omezeny, aby se disk nedal zaplnit ani s heslem.

Stara zaloha se prepise novou pod stejnym nazvem. Mazat smi kdokoli s heslem,
stejne jako prepsat; ze seznamu zmizi hned, protoze seznam cte adresar.
"""
from __future__ import annotations

import json
import os
import re
import tempfile
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

DATA = Path(os.environ.get("SETTINGS_DATA", "/opt/settings/data"))
PORT = int(os.environ.get("SETTINGS_PORT", "8092"))
PREFIX = "/settings/"
# Stejny tvar jako settingsBackupValidName() ve firmwaru.
NAME = re.compile(r"^[a-z0-9][a-z0-9-]{0,31}$")
# Obalka ma kolem 13 kB; firmware sam neposle vic nez 32 kB.
MAX_BODY_BYTES = 64 * 1024
MAX_BACKUPS = 64
FORMAT = "waveshare-hodiny-settings"
VERSION = 3
DATA_TEXT = re.compile(r"^[A-Za-z0-9_-]+$")


def backup_path(name: str) -> Path:
    return DATA / f"{name}.json"


def valid_envelope(payload: object) -> bool:
    return (
        isinstance(payload, dict)
        and payload.get("format") == FORMAT
        and payload.get("version") == VERSION
        and isinstance(payload.get("data"), str)
        and DATA_TEXT.match(payload["data"]) is not None
    )


def describe(path: Path) -> dict | None:
    try:
        payload = json.loads(path.read_bytes())
        modified = datetime.fromtimestamp(path.stat().st_mtime, timezone.utc)
    except (OSError, ValueError):
        return None
    if not valid_envelope(payload):
        return None
    firmware = payload.get("firmware")
    return {
        "name": path.stem,
        "modified": modified.strftime("%Y-%m-%dT%H:%M:%SZ"),
        "firmware": firmware if isinstance(firmware, str) else "",
        "secrets": payload.get("secrets") is True,
        "size": path.stat().st_size,
    }


def stored_backups() -> list[Path]:
    return sorted(
        path for path in DATA.glob("*.json") if NAME.match(path.stem)
    )


class Handler(BaseHTTPRequestHandler):
    server_version = "settings-web/1.0"
    # Viz agenda/serve.py: bez timeoutu by se vlakna hromadila na pomalych
    # spojenich.
    timeout = 15

    def _send(self, code: int, body: bytes = b"",
              ctype: str = "application/json; charset=utf-8"):
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(body)

    def _error(self, code: int, message: str):
        self._send(code, json.dumps({"ok": False, "message": message}).encode())

    def _name(self) -> str | None:
        path = self.path.split("?", 1)[0]
        if not path.startswith(PREFIX):
            return None
        name = path[len(PREFIX):]
        return name if NAME.match(name) else None

    def do_GET(self):
        path = self.path.split("?", 1)[0]
        if path == PREFIX:
            backups = [item for item in map(describe, stored_backups()) if item]
            self._send(200, json.dumps({"backups": backups}).encode())
            return
        name = self._name()
        if name is None:
            self._error(404, "not found")
            return
        try:
            data = backup_path(name).read_bytes()
        except OSError:
            self._error(404, "not found")
            return
        self._send(200, data)

    do_HEAD = do_GET

    def do_PUT(self):
        name = self._name()
        if name is None:
            self._error(404, "not found")
            return
        try:
            length = int(self.headers.get("Content-Length", ""))
        except ValueError:
            self._error(411, "length required")
            return
        if length <= 0 or length > MAX_BODY_BYTES:
            self._error(413, "too large")
            return
        body = self.rfile.read(length)
        if len(body) != length:
            self._error(400, "incomplete body")
            return
        try:
            payload = json.loads(body)
        except ValueError:
            self._error(400, "not json")
            return
        if not valid_envelope(payload):
            self._error(400, "not a clock settings backup")
            return

        target = backup_path(name)
        if not target.exists() and len(stored_backups()) >= MAX_BACKUPS:
            self._error(507, "too many backups")
            return
        # Zapis pres docasny soubor a os.replace: hodiny, ktere zrovna
        # stahuji, nikdy nedostanou napul zapsanou zalohu.
        handle, temporary = tempfile.mkstemp(dir=DATA, prefix=".upload-")
        try:
            with os.fdopen(handle, "wb") as output:
                output.write(body)
            os.chmod(temporary, 0o600)
            os.replace(temporary, target)
        except OSError:
            try:
                os.unlink(temporary)
            except OSError:
                pass
            self._error(500, "write failed")
            return
        self._send(200, json.dumps({"ok": True, "name": name}).encode())

    def do_DELETE(self):
        name = self._name()
        if name is None:
            self._error(404, "not found")
            return
        try:
            backup_path(name).unlink()
        except FileNotFoundError:
            self._error(404, "not found")
            return
        except OSError:
            self._error(500, "delete failed")
            return
        self._send(200, json.dumps({"ok": True, "name": name}).encode())

    def do_POST(self):
        self._error(405, "method not allowed")

    def log_message(self, *args):  # tise, at neplni journal
        pass


if __name__ == "__main__":
    DATA.mkdir(mode=0o700, parents=True, exist_ok=True)
    os.chmod(DATA, 0o700)
    ThreadingHTTPServer((os.environ.get("SETTINGS_BIND", "127.0.0.1"), PORT),
                        Handler).serve_forever()
