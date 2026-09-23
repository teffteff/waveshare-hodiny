#!/usr/bin/env python3
"""Jedno nastaveni pro vsechny hodiny: webove rozhrani hodin pres server.

Hodiny sedi doma za NAT a server v OCI se k nim nedovola. Proto se k nemu
hodiny pripojuji samy a drzi jedno odchozi spojeni (long-poll):

    hodiny  --GET /fleet/agent/poll-->   server   (drzi az 25 s, pak 204)
            <--200 + pozadavek---------           (kdyz ma prohlizec co chtit)
            --POST /fleet/agent/result->          (odpoved vlastniho webu)

Server tak nikdy nemusi znat adresu hodin a v domaci siti se nic neotevira.
Hodiny pozadavek nevykladaji: predaji ho svemu vlastnimu web serveru pres
loopback (127.0.0.1:80) a odpoved poslou zpet, takze stranka i API jsou
presne ty z firmwaru daneho kusu, vcetne verze. Server ke strance jen prida
vyber hodin a prefix /fleet/d/<nazev>/ pred adresy API.

Pristup:

- Prihlaseni heslem a kodem TOTP (RFC 6238, aplikace typu Aegis nebo Google
  Authenticator) v jednom kroku; neuspech nerekne, co bylo spatne. Heslo je
  ulozene jako scrypt, kod se nesmi pouzit dvakrat.
- Brzda: z jedne IP nejvys 5 neuspechu za 15 minut, celkem 30 za hodinu,
  pak se prihlaseni docasne vubec nezkousi. Neuspech jde do journalu
  i s IP (pro fail2ban).
- Relace je nahodny token v cookie __Secure-fleet (HttpOnly, Secure,
  SameSite=Strict, Path=/fleet/), vyprsi po 30 minutach necinnosti
  a nejpozdeji po 12 hodinach. Drzi se jen v pameti: restart sluzby odhlasi.
- Kazdy pozadavek na API hodin a kazdy POST nese hlavicku X-Fleet-Csrf
  s tokenem relace. Home Assistant bezi na stejnem jmenu serveru, takze
  SameSite sam nestaci.
- Hodiny se hlasi vlastnim tokenem (Authorization: Bearer). Server drzi jen
  jeho SHA-256; token se ukaze jednou pri `add-device`.
- Pres server nejde zmenit heslo webu hodin ani nastaveni vzdalene spravy
  a nejde na ovladaci API; totez hlidaji i hodiny samy. Firmware se instaluje
  jen z oficialniho vydani (hodiny si ho stahuji samy), podvrhnout se neda.

Poslouchat smi jen na 127.0.0.1 za Caddy: X-Forwarded-For se veri, protoze
ho nastavuje Caddy.

Sprava (na serveru pod uzivatelem fleet, viz infra/README.md):

    serve.py hash-password         vypise FLEET_PASSWORD_HASH=... do fleet.env
    serve.py new-totp              vypise FLEET_TOTP_SECRET=... a otpauth:// adresu
    serve.py add-device <nazev>    zaregistruje hodiny a vypise jejich token
    serve.py remove-device <nazev>
    serve.py list-devices

GET /fleet-status vraci prehled pro check-stack; ven pres Caddy nevede.
"""
from __future__ import annotations

import argparse
import base64
import getpass
import gzip
import hashlib
import hmac
import html
import json
import os
import re
import secrets
import struct
import sys
import tempfile
import threading
import time
import urllib.parse
from collections import deque
from dataclasses import dataclass, field
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

PORT = int(os.environ.get("FLEET_PORT", "8099"))
BIND = os.environ.get("FLEET_BIND", "127.0.0.1")
STATE = Path(os.environ.get("FLEET_STATE", "/opt/fleet/state"))
PREFIX = "/fleet"

# Hodiny se ptaji znovu hned po odpovedi; 25 s je pod vychozim limitem
# necinnosti vetsiny proxy i NAT v routerech.
POLL_WAIT_S = 25.0
# Hodiny, ktere se tak dlouho neozvaly, jsou offline a pozadavek se ani
# nezaradi.
ONLINE_WINDOW_S = 45.0
# Nejpomalejsi obsluhy ve firmwaru (zkouska kanalu, sifrovani zalohy) trvaji
# jednotky az desitky sekund.
JOB_TIMEOUT_S = 75.0
MAX_QUEUE = 16
# Kolik velkych odpovedi na hodiny si server pamatuje (stranka, preklady...).
CACHE_ENTRIES = 8
TAG = re.compile(r"^[0-9a-f]{16}$")
MAX_REQUEST_BODY = 64 * 1024
# Stranka nastaveni je gzipem kolem 80 kB, preklady 40 kB.
MAX_RESULT_BODY = 1024 * 1024

SESSION_IDLE_S = 30 * 60
SESSION_MAX_S = 12 * 3600
COOKIE = "__Secure-fleet"
IP_FAILURES = 5
IP_WINDOW_S = 15 * 60
GLOBAL_FAILURES = 30
GLOBAL_WINDOW_S = 3600
GLOBAL_LOCK_S = 15 * 60

TOTP_STEP = 30
TOTP_DIGITS = 6

NAME = re.compile(r"^[a-z0-9][a-z0-9-]{0,31}$")
# Co smi prohlizec po hodinach chtit. Zbytek (ovladaci API, prihlaseni do
# hodin, heslo webu, nastaveni vzdalene spravy) jde jen z domaci site.
RELAY_PATH = re.compile(r"^/(|ui-language\.js|diagnostics|api/[a-z0-9][a-z0-9/_-]*)$")
RELAY_BLOCKED = ("/api/web-password", "/api/control/", "/api/auth/")
# Stav vzdalene spravy si stranka precist smi, zmenit ho jde jen doma.
RELAY_READ_ONLY = ("/api/remote-admin",)
QUERY = re.compile(r"^[A-Za-z0-9=&%._~+-]*$")
CONTENT_TYPE = re.compile(r"^[A-Za-z0-9!#$&^_.+/;=\- ]{1,100}$")


def log(message: str) -> None:
    print(message, file=sys.stderr, flush=True)


# --- heslo a TOTP ---------------------------------------------------------

SCRYPT_N, SCRYPT_R, SCRYPT_P = 2 ** 15, 8, 1


def hash_password(password: str, salt: bytes | None = None) -> str:
    salt = salt or secrets.token_bytes(16)
    digest = hashlib.scrypt(password.encode(), salt=salt, n=SCRYPT_N,
                            r=SCRYPT_R, p=SCRYPT_P, maxmem=64 * 1024 * 1024,
                            dklen=32)
    encode = lambda raw: base64.b64encode(raw).decode()
    return f"scrypt${SCRYPT_N}${SCRYPT_R}${SCRYPT_P}${encode(salt)}${encode(digest)}"


def verify_password(password: str, stored: str) -> bool:
    try:
        kind, n, r, p, salt, digest = stored.split("$")
        if kind != "scrypt":
            return False
        expected = base64.b64decode(digest)
        candidate = hashlib.scrypt(password.encode(),
                                   salt=base64.b64decode(salt), n=int(n),
                                   r=int(r), p=int(p),
                                   maxmem=64 * 1024 * 1024,
                                   dklen=len(expected))
    except (ValueError, TypeError):
        return False
    return hmac.compare_digest(candidate, expected)


def totp_code(secret: str, counter: int) -> str:
    key = base64.b32decode(secret.upper() + "=" * (-len(secret) % 8))
    mac = hmac.new(key, struct.pack(">Q", counter), hashlib.sha1).digest()
    offset = mac[-1] & 0x0F
    value = struct.unpack(">I", mac[offset:offset + 4])[0] & 0x7FFFFFFF
    return str(value % 10 ** TOTP_DIGITS).zfill(TOTP_DIGITS)


def totp_match(secret: str, code: str, now: float) -> int | None:
    """Vrati krok, ke kteremu kod patri (tolerance +-1 krok), jinak None."""
    if not re.fullmatch(r"\d{6}", code or ""):
        return None
    current = int(now // TOTP_STEP)
    for counter in (current - 1, current, current + 1):
        if hmac.compare_digest(totp_code(secret, counter), code):
            return counter
    return None


def token_hash(token: str) -> str:
    return hashlib.sha256(token.encode()).hexdigest()


# --- registrace hodin -----------------------------------------------------

def devices_path() -> Path:
    return STATE / "devices.json"


def load_devices() -> dict[str, dict]:
    try:
        payload = json.loads(devices_path().read_text())
    except (OSError, ValueError):
        return {}
    devices = payload.get("devices") if isinstance(payload, dict) else None
    if not isinstance(devices, dict):
        return {}
    return {name: item for name, item in devices.items()
            if NAME.match(name) and isinstance(item, dict)
            and isinstance(item.get("token_sha256"), str)}


def save_devices(devices: dict[str, dict]) -> None:
    STATE.mkdir(mode=0o700, parents=True, exist_ok=True)
    handle, temporary = tempfile.mkstemp(dir=STATE, prefix=".devices-")
    with os.fdopen(handle, "w") as output:
        json.dump({"devices": devices}, output, indent=2, sort_keys=True)
    os.chmod(temporary, 0o600)
    os.replace(temporary, devices_path())


# --- predavani pozadavku --------------------------------------------------

@dataclass
class Job:
    id: str
    method: str
    path: str
    content_type: str
    body: bytes
    created: float = field(default_factory=time.monotonic)
    picked: float = 0.0
    # Otisk odpovedi, kterou server pro tuhle cestu uz ma (posle se hodinam).
    if_none_match: str = ""
    from_cache: bool = False
    done: threading.Event = field(default_factory=threading.Event)
    status: int = 0
    result_type: str = ""
    result_encoding: str = ""
    result_body: bytes = b""


@dataclass
class Device:
    name: str
    token_sha256: str
    queue: deque = field(default_factory=deque)
    inflight: dict = field(default_factory=dict)
    last_poll: float = 0.0
    firmware: str = ""
    clock_name: str = ""
    polling: int = 0
    # Posledni velke odpovedi na GET: cesta -> (otisk, typ, kodovani, telo).
    # Hodiny pri shode otisku telo znovu neposilaji.
    cache: dict = field(default_factory=dict)

    def online(self, now: float) -> bool:
        return self.polling > 0 or now - self.last_poll < ONLINE_WINDOW_S


class Relay:
    def __init__(self):
        self.lock = threading.Condition()
        self.devices: dict[str, Device] = {}
        self.devices_mtime = -1.0
        self.reload()

    def reload(self) -> None:
        """Nacte devices.json, kdyz se zmenil (add-device bez restartu)."""
        try:
            mtime = devices_path().stat().st_mtime
        except OSError:
            mtime = 0.0
        if mtime == self.devices_mtime:
            return
        registered = load_devices()
        with self.lock:
            self.devices_mtime = mtime
            for name in list(self.devices):
                if name not in registered or \
                        registered[name]["token_sha256"] != self.devices[name].token_sha256:
                    del self.devices[name]
            for name, item in registered.items():
                if name not in self.devices:
                    self.devices[name] = Device(name, item["token_sha256"])
            self.lock.notify_all()

    def device_for_token(self, token: str) -> Device | None:
        self.reload()
        wanted = token_hash(token)
        with self.lock:
            for device in self.devices.values():
                if hmac.compare_digest(device.token_sha256, wanted):
                    return device
        return None

    def device(self, name: str) -> Device | None:
        self.reload()
        with self.lock:
            return self.devices.get(name)

    def overview(self) -> list[dict]:
        self.reload()
        now = time.monotonic()
        with self.lock:
            return [{
                "name": device.name,
                "clockName": device.clock_name,
                "firmware": device.firmware,
                "online": device.online(now),
                "lastSeenSeconds": (round(now - device.last_poll)
                                    if device.last_poll else None),
            } for device in sorted(self.devices.values(), key=lambda d: d.name)]

    def poll(self, device: Device, wait: float) -> Job | None:
        deadline = time.monotonic() + wait
        with self.lock:
            device.polling += 1
            try:
                while True:
                    self._expire(device)
                    if device.queue:
                        job = device.queue.popleft()
                        job.picked = time.monotonic()
                        device.inflight[job.id] = job
                        return job
                    remaining = deadline - time.monotonic()
                    if remaining <= 0 or self.devices.get(device.name) is not device:
                        return None
                    self.lock.wait(remaining)
            finally:
                device.polling -= 1
                device.last_poll = time.monotonic()

    def submit(self, device: Device, job: Job) -> str | None:
        """Zaradi pozadavek; vrati chybu, kdyz to nejde."""
        with self.lock:
            if not device.online(time.monotonic()):
                return "offline"
            if len(device.queue) + len(device.inflight) >= MAX_QUEUE:
                return "busy"
            device.queue.append(job)
            self.lock.notify_all()
        return None

    def wait(self, device: Device, job: Job, timeout: float) -> bool:
        finished = job.done.wait(timeout)
        with self.lock:
            if job in device.queue:
                device.queue.remove(job)
            device.inflight.pop(job.id, None)
        return finished

    def complete(self, device: Device, job_id: str, status: int,
                 result_type: str, encoding: str, body: bytes,
                 tag: str = "", not_modified: bool = False) -> bool:
        with self.lock:
            job = device.inflight.pop(job_id, None)
            if job is None:
                return False
            if not_modified:
                cached = device.cache.get(job.path)
                if job.method == "GET" and cached is not None and cached[0] == tag:
                    _, result_type, encoding, body = cached
                    job.from_cache = True
                else:
                    status, result_type, encoding, body = 502, "", "", b""
            elif tag and job.method == "GET" and status == 200 and body:
                device.cache.pop(job.path, None)
                device.cache[job.path] = (tag, result_type, encoding, body)
                while len(device.cache) > CACHE_ENTRIES:
                    device.cache.pop(next(iter(device.cache)))
        job.status, job.result_type = status, result_type
        job.result_encoding, job.result_body = encoding, body
        job.done.set()
        return True

    def cached_tag(self, device: Device, path: str) -> str:
        with self.lock:
            cached = device.cache.get(path)
            return cached[0] if cached else ""

    def _expire(self, device: Device) -> None:
        limit = time.monotonic() - JOB_TIMEOUT_S
        for job_id, job in list(device.inflight.items()):
            if job.created < limit:
                del device.inflight[job_id]


# --- prihlaseni -----------------------------------------------------------

@dataclass
class Session:
    csrf: str
    created: float
    seen: float


class Guard:
    """Relace a brzda proti hadani hesla."""

    def __init__(self, password_hash: str, totp_secret: str):
        self.password_hash = password_hash
        self.totp_secret = totp_secret
        self.lock = threading.Lock()
        self.sessions: dict[str, Session] = {}
        self.ip_failures: dict[str, list[float]] = {}
        self.global_failures: list[float] = []
        self.locked_until = 0.0
        self.last_counter = self._load_counter()

    def configured(self) -> bool:
        return bool(self.password_hash and self.totp_secret)

    @staticmethod
    def _load_counter() -> int:
        try:
            return int((STATE / "totp-last").read_text().strip())
        except (OSError, ValueError):
            return 0

    def _store_counter(self, counter: int) -> None:
        try:
            STATE.mkdir(mode=0o700, parents=True, exist_ok=True)
            (STATE / "totp-last").write_text(str(counter))
        except OSError:
            pass

    def login_blocked(self, ip: str, now: float) -> bool:
        with self.lock:
            recent = [t for t in self.ip_failures.get(ip, []) if now - t < IP_WINDOW_S]
            self.ip_failures[ip] = recent
            return now < self.locked_until or len(recent) >= IP_FAILURES

    def login(self, ip: str, password: str, code: str, now: float) -> str | None:
        """Vrati token nove relace, nebo None."""
        if not self.configured() or self.login_blocked(ip, now):
            return None
        # Obe kontroly vzdy; scrypt je pomaly schvalne.
        password_ok = verify_password(password, self.password_hash)
        counter = totp_match(self.totp_secret, code, now)
        with self.lock:
            fresh = counter is not None and counter > self.last_counter
            if password_ok and fresh:
                self.last_counter = counter
                self._store_counter(counter)
                token = secrets.token_urlsafe(32)
                self.sessions[token] = Session(secrets.token_urlsafe(24), now, now)
                self.ip_failures.pop(ip, None)
                return token
            self.ip_failures.setdefault(ip, []).append(now)
            self.global_failures = [t for t in self.global_failures
                                    if now - t < GLOBAL_WINDOW_S] + [now]
            if len(self.global_failures) >= GLOBAL_FAILURES:
                self.locked_until = now + GLOBAL_LOCK_S
                self.global_failures = []
        return None

    def session(self, token: str, now: float) -> Session | None:
        if not token:
            return None
        with self.lock:
            for key, item in list(self.sessions.items()):
                if now - item.seen > SESSION_IDLE_S or now - item.created > SESSION_MAX_S:
                    del self.sessions[key]
            item = self.sessions.get(token)
            if item is not None:
                item.seen = now
            return item

    def logout(self, token: str) -> None:
        with self.lock:
            self.sessions.pop(token, None)


# --- stranky --------------------------------------------------------------

STYLE = """
:root{--bg:#101418;--surface:#171c20;--field:#1c2227;--line:#3b444b;--text:#f3f6f8;--muted:#9da7ae;--cyan:#4ccbec;--green:#65c744;--error:#ff6262}
*{box-sizing:border-box}html{background:var(--bg);color:var(--text);font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif;font-size:16px}
body{margin:0}main{width:min(760px,calc(100% - 32px));margin:0 auto;padding:38px 0 64px}
h1{font-size:34px;letter-spacing:-.025em;margin:0 0 24px}label{display:block;margin:0 0 7px;color:var(--muted);font-size:14px}
input{width:100%;min-height:46px;padding:0 14px;margin:0 0 18px;border:1px solid var(--line);border-radius:10px;background:var(--field);color:var(--text);font-size:16px}
input:focus{outline:3px solid rgba(76,203,236,.22);border-color:var(--cyan)}
button,.button{display:inline-flex;align-items:center;justify-content:center;min-height:46px;padding:0 20px;border:1px solid var(--line);border-radius:10px;background:var(--surface);color:var(--text);font-size:16px;font-weight:700;text-decoration:none;cursor:pointer}
button.primary,.button.primary{border-color:var(--cyan);background:var(--cyan);color:#071015}
.card{padding:24px;border:1px solid var(--line);border-radius:12px;background:rgba(23,28,32,.58)}
.error{color:var(--error);margin:0 0 18px}.hint{color:var(--muted);font-size:14px}
ul{list-style:none;margin:0;padding:0}li{display:flex;align-items:center;gap:16px;padding:16px 0;border-bottom:1px solid var(--line)}li:last-child{border-bottom:0}
.dot{width:10px;height:10px;border-radius:50%;background:var(--error);flex:0 0 auto}.dot.on{background:var(--green)}
.grow{flex:1 1 auto;min-width:0}.name{font-weight:700}
header{display:flex;align-items:center;justify-content:space-between;gap:16px}
"""

LOGIN_PAGE = """<!doctype html><html lang="cs"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><meta name="color-scheme" content="dark">
<title>Hodiny – přihlášení</title><style>{style}</style>{guard}</head><body><main>
<h1>Nastavení hodin</h1><form class="card" method="post" action="/fleet/login">
{error}<label for="password">Heslo</label><input id="password" name="password" type="password" autocomplete="current-password" required autofocus>
<label for="code">Kód z ověřovací aplikace</label><input id="code" name="code" inputmode="numeric" pattern="[0-9]{{6}}" maxlength="6" autocomplete="one-time-code" required>
<button class="primary" type="submit">Přihlásit</button></form></main></body></html>"""

DEVICES_PAGE = """<!doctype html><html lang="cs"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><meta name="color-scheme" content="dark">
<title>Hodiny</title><style>{style}</style>{guard}</head><body><main>
<header><h1>Hodiny</h1><form method="post" action="/fleet/logout"><input type="hidden" name="csrf" value="{csrf}"><button type="submit">Odhlásit</button></form></header>
<div class="card"><ul>{items}</ul></div>
<p class="hint">Hodiny se připojují samy; „offline“ znamená, že se neozvaly {online} s. Nové hodiny se přidávají na serveru příkazem <code>serve.py add-device</code>.</p>
</main></body></html>"""

MESSAGE_PAGE = """<!doctype html><html lang="cs"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><meta name="color-scheme" content="dark">
<title>Hodiny</title><style>{style}</style></head><body><main>
<h1>Hodiny</h1><div class="card"><p>{message}</p><a class="button" href="/fleet/">Zpět na seznam hodin</a></div>
</main></body></html>"""

# Vlozi se na zacatek <head> stranky z hodin, pred jeji vlastni skripty.
# Adresy zacinajici lomitkem dostanou prefix hodin a kazdy dotaz nese token
# proti CSRF. Do hlavicky stranky pribude vyber hodin a odhlaseni.
SHIM = """<script>(()=>{{const base={base};let csrf={csrf};const devices={devices};const current={name};
const fix=u=>typeof u==="string"&&u.startsWith("/")&&!u.startsWith("//")&&!u.startsWith("/fleet/")?base+u:u;
const nativeFetch=window.fetch.bind(window);
// Token proti CSRF je vazany na relaci. Stranka z drivejska (jina karta,
// obnovena karta, nove prihlaseni) si pri odmitnuti vezme aktualni a pozadavek
// zopakuje; vyprsela relace vede na prihlaseni.
const refreshCsrf=async()=>{{const r=await nativeFetch("/fleet/api/session",{{credentials:"same-origin",headers:{{"X-Fleet-Session":"1"}}}});
if(!r.ok){{location.href="/fleet/";return false}}const j=await r.json();csrf=j.csrf;return true}};
const send=(input,options)=>{{const headers=new Headers(options.headers||{{}});headers.set("X-Fleet-Csrf",csrf);return nativeFetch(fix(input),{{...options,headers,credentials:"same-origin"}})}};
window.fetch=async(input,options={{}})=>{{let response=await send(input,options);const action=response.headers.get("X-Fleet-Action");
if(action==="refresh"&&await refreshCsrf())response=await send(input,options);else if(action==="login")location.href="/fleet/";return response}};
const addBar=()=>{{const host=document.querySelector("header .header-actions")||document.querySelector("header")||document.body;if(!host||document.getElementById("fleetBar"))return;
const bar=document.createElement("div");bar.id="fleetBar";bar.style.cssText="display:flex;align-items:center;gap:8px";
const select=document.createElement("select");select.setAttribute("aria-label","Hodiny");select.style.cssText="min-height:48px;padding:0 12px;border:1px solid var(--line,#3b444b);border-radius:12px;background:var(--surface,#171c20);color:var(--text,#f3f6f8);font-size:16px;font-weight:700";
for(const d of devices){{const o=document.createElement("option");o.value=d.name;o.textContent=(d.online?"● ":"○ ")+(d.clockName&&d.clockName!==d.name?d.name+" ("+d.clockName+")":d.name);o.selected=d.name===current;select.append(o)}}
select.addEventListener("change",()=>{{location.href="/fleet/d/"+encodeURIComponent(select.value)+"/"}});
const list=document.createElement("a");list.href="/fleet/";list.textContent="☰";list.title="Všechny hodiny";list.style.cssText="display:grid;place-items:center;width:48px;height:48px;border:1px solid var(--line,#3b444b);border-radius:12px;color:var(--text,#f3f6f8);text-decoration:none;font-size:20px";
bar.append(list,select);host.prepend(bar)}};
if(document.readyState==="loading")document.addEventListener("DOMContentLoaded",addBar);else addBar();
addEventListener("pageshow",e=>{{if(e.persisted)location.reload()}});
if(document.prerendering)document.addEventListener("prerenderingchange",()=>location.reload(),{{once:true}})}})();</script>"""

# Stranka vracena z pameti prohlizece (zpet/vpred, obnovena karta) muze patrit
# jine relaci nez aktualni cookie: nacte se znovu ze serveru.
PAGE_GUARD = ('<script>addEventListener("pageshow",e=>{if(e.persisted)location.reload()});'
              'if(document.prerendering)document.addEventListener("prerenderingchange",'
              '()=>location.reload(),{once:true});</script>')

ROOTED_ATTRIBUTE = re.compile(r'\b(src|href|action)="/(?!/)')


def security_headers() -> list[tuple[str, str]]:
    return [
        ("Cache-Control", "no-store"),
        ("X-Content-Type-Options", "nosniff"),
        ("X-Frame-Options", "DENY"),
        # Ne no-referrer: s ním Chrome posílá u formulářů Origin: null
        # a kontrola původu by odmítla i vlastní přihlášení.
        ("Referrer-Policy", "same-origin"),
        ("Content-Security-Policy",
         "default-src 'self'; img-src 'self' data:; style-src 'unsafe-inline'; "
         "script-src 'self' 'unsafe-inline'; connect-src 'self'; "
         "form-action 'self'; frame-ancestors 'none'; base-uri 'none'"),
    ]


def inject(page: str, name: str, csrf: str, devices: list[dict]) -> str:
    base = f"{PREFIX}/d/{name}"
    page = ROOTED_ATTRIBUTE.sub(lambda m: f'{m.group(1)}="{base}/', page)
    # </script> v nazvu hodin nehrozi (NAME), ale json.dumps je i tak
    # zabalen, aby "<" nemohlo ukoncit skript.
    as_js = lambda value: json.dumps(value).replace("<", "\\u003c")
    shim = SHIM.format(base=as_js(base), csrf=as_js(csrf),
                       devices=as_js(devices), name=as_js(name))
    match = re.search(r"<head[^>]*>", page, re.IGNORECASE)
    if match:
        return page[:match.end()] + shim + page[match.end():]
    return shim + page


# --- HTTP -----------------------------------------------------------------

class Handler(BaseHTTPRequestHandler):
    server_version = "fleet-web/1.0"
    protocol_version = "HTTP/1.1"
    # Hodiny drzi spojeni mezi dotazy; long-poll sam ceka uvnitr obsluhy.
    timeout = 60

    relay: Relay
    guard: Guard

    # -- pomocne --

    def _send(self, code: int, body: bytes = b"",
              ctype: str = "application/json; charset=utf-8",
              headers: list[tuple[str, str]] | None = None):
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        for key, value in headers or security_headers():
            self.send_header(key, value)
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(body)

    def _json(self, code: int, payload: dict, headers=None):
        self._send(code, json.dumps(payload).encode(), headers=headers)

    def _error(self, code: int, message: str):
        self._json(code, {"ok": False, "message": message})

    def _page_error(self, code: int, message: str):
        page = MESSAGE_PAGE.format(style=STYLE, message=html.escape(message))
        self._send(code, page.encode(), "text/html; charset=utf-8")

    def _redirect(self, location: str, headers: list[tuple[str, str]] = ()):
        self._send(303, b"", "text/plain", security_headers() +
                   [("Location", location), *headers])

    def _client_ip(self) -> str:
        forwarded = self.headers.get("X-Forwarded-For", "")
        return forwarded.split(",")[0].strip() or self.client_address[0]

    def _read_body(self, limit: int) -> bytes | None:
        try:
            length = int(self.headers.get("Content-Length", "0"))
        except ValueError:
            self._error(411, "length required")
            return None
        if length < 0 or length > limit:
            self._error(413, "too large")
            self.close_connection = True
            return None
        body = self.rfile.read(length) if length else b""
        if len(body) != length:
            self._error(400, "incomplete body")
            self.close_connection = True
            return None
        return body

    def _cookie(self) -> str:
        for part in self.headers.get("Cookie", "").split(";"):
            key, _, value = part.strip().partition("=")
            if key == COOKIE:
                return value
        return ""

    def _session(self) -> Session | None:
        return self.guard.session(self._cookie(), time.time())

    def _same_origin(self) -> bool:
        origin = self.headers.get("Origin")
        if not origin:
            return True
        host = self.headers.get("X-Forwarded-Host") or self.headers.get("Host", "")
        return origin in (f"https://{host}", f"http://{host}")

    def _csrf_problem(self, session: Session, submitted: str | None = None) -> str:
        """Prazdny retezec, kdyz token proti CSRF sedi; jinak duvod do journalu."""
        value = submitted if submitted is not None else self.headers.get("X-Fleet-Csrf", "")
        if not self._same_origin():
            return "foreign origin"
        if not value:
            return "missing token"
        if not hmac.compare_digest(value.encode(), session.csrf.encode()):
            # Typicky stranka vykreslena pro starsi relaci (druhe prihlaseni,
            # jina karta, obnovena karta prohlizece).
            return "stale token"
        return ""

    def _csrf_ok(self, session: Session, submitted: str | None = None) -> bool:
        problem = self._csrf_problem(session, submitted)
        if problem:
            log(f"fleet: csrf rejected ({problem}) {self.command} {self.path.partition('?')[0]}")
        return not problem

    # -- smerovani --

    def do_GET(self):
        self._route()

    def do_POST(self):
        self._route()

    def do_HEAD(self):
        self._error(405, "method not allowed")

    do_PUT = do_DELETE = do_HEAD

    def _route(self):
        path, _, query = self.path.partition("?")
        try:
            if path.startswith(f"{PREFIX}/") and not path.startswith(f"{PREFIX}/agent/"):
                purpose = self.headers.get("Sec-Purpose", "") or self.headers.get("Purpose", "")
                if "prefetch" in purpose or "prerender" in purpose:
                    # Predem nactena stranka by mohla patrit jine relaci nez ta
                    # po kliknuti; prohlizec ji pri odmitnuti nacte az na klik.
                    log(f"fleet: refused speculative {self.command} {path} ({purpose})")
                    return self._send(503, b"", "text/plain", security_headers())
                if self.headers.get("Sec-Fetch-Mode") == "navigate":
                    agent = self.headers.get("User-Agent", "")
                    browser = next((name for name in ("Edg/", "OPR/", "Firefox/", "Chrome/", "Safari/")
                                    if name in agent), "?").rstrip("/")
                    log(f"fleet: navigate {self.command} {path} session={'yes' if self._session() else 'no'} "
                        f"browser={browser}")
            if path == "/fleet-status" and self.command == "GET":
                return self._status()
            if path.startswith(f"{PREFIX}/agent/"):
                return self._agent(path[len(PREFIX) + 7:])
            if path in (PREFIX, f"{PREFIX}/"):
                return self._index() if self.command == "GET" else self._error(405, "method not allowed")
            if path == f"{PREFIX}/login" and self.command == "POST":
                return self._login()
            if path == f"{PREFIX}/logout" and self.command == "POST":
                return self._logout()
            if path == f"{PREFIX}/api/session" and self.command == "GET":
                return self._session_token()
            if path == f"{PREFIX}/api/devices" and self.command == "GET":
                session = self._session()
                if session is None or not self._csrf_ok(session):
                    return self._error(401, "Přihlášení vypršelo.")
                return self._json(200, {"devices": self.relay.overview()})
            match = re.match(rf"^{PREFIX}/d/([a-z0-9][a-z0-9-]{{0,31}})(/.*)?$", path)
            if match:
                if match.group(2) is None:
                    return self._redirect(f"{PREFIX}/d/{match.group(1)}/")
                return self._device(match.group(1), match.group(2), query)
            self._error(404, "not found")
        except (BrokenPipeError, ConnectionResetError):
            self.close_connection = True

    def _session_token(self):
        # Vlastni hlavicku cizi stranka bez CORS neposle a cookie je
        # SameSite=Strict; odpoved si stejne precist nemuze.
        session = self._session()
        if session is None or self.headers.get("X-Fleet-Session") != "1" or \
                not self._same_origin():
            return self._json(401, {"ok": False, "message": "Přihlášení vypršelo."})
        log("fleet: csrf refreshed")
        self._json(200, {"ok": True, "csrf": session.csrf})

    def _status(self):
        if self.client_address[0] != "127.0.0.1" or self.headers.get("X-Forwarded-For"):
            return self._error(404, "not found")
        self._json(200, {"configured": self.guard.configured(),
                         "devices": self.relay.overview()})

    # -- prihlaseni --

    def _index(self):
        session = self._session()
        if session is None:
            return self._login_page()
        rows = []
        for device in self.relay.overview():
            label = html.escape(device["name"])
            if device["clockName"] and device["clockName"] != device["name"]:
                label += f' <span class="hint">({html.escape(device["clockName"])})</span>'
            state = "online" if device["online"] else "offline"
            detail = html.escape(device["firmware"] or "—")
            link = (f'<a class="button primary" href="{PREFIX}/d/{device["name"]}/">Nastavit</a>'
                    if device["online"] else "")
            rows.append(f'<li><span class="dot{" on" if device["online"] else ""}" title="{state}"></span>'
                        f'<span class="grow"><span class="name">{label}</span><br>'
                        f'<span class="hint">{state} · firmware {detail}</span></span>{link}</li>')
        if not rows:
            rows.append('<li class="hint">Zatím nejsou zaregistrované žádné hodiny.</li>')
        page = DEVICES_PAGE.format(style=STYLE, guard=PAGE_GUARD, csrf=html.escape(session.csrf),
                                   items="".join(rows), online=int(ONLINE_WINDOW_S))
        self._send(200, page.encode(), "text/html; charset=utf-8")

    def _login_page(self, error: str = "", code: int = 200):
        message = f'<p class="error">{html.escape(error)}</p>' if error else ""
        if not self.guard.configured():
            message = '<p class="error">Přihlášení není na serveru nastavené (fleet.env).</p>'
        page = LOGIN_PAGE.format(style=STYLE, guard=PAGE_GUARD, error=message)
        self._send(code, page.encode(), "text/html; charset=utf-8")

    def _login(self):
        body = self._read_body(4096)
        if body is None:
            return
        if not self._same_origin():
            return self._login_page("Přihlášení z cizí stránky bylo odmítnuto.", 403)
        form = urllib.parse.parse_qs(body.decode("utf-8", "replace"))
        ip = self._client_ip()
        now = time.time()
        if self.guard.login_blocked(ip, now):
            log(f"fleet: login blocked for {ip}")
            return self._login_page("Příliš mnoho pokusů. Zkus to později.", 429)
        token = self.guard.login(ip, form.get("password", [""])[0],
                                 form.get("code", [""])[0].strip(), now)
        if token is None:
            log(f"fleet: failed login from {ip}")
            return self._login_page("Heslo nebo kód nesouhlasí.", 401)
        log(f"fleet: login from {ip}")
        # Bez Max-Age: zavreni prohlizece prihlaseni ukonci (limity hlida server).
        cookie = f"{COOKIE}={token}; Path={PREFIX}/; Secure; HttpOnly; SameSite=Strict"
        self._redirect(f"{PREFIX}/", [("Set-Cookie", cookie)])

    def _logout(self):
        body = self._read_body(4096)
        if body is None:
            return
        session = self._session()
        form = urllib.parse.parse_qs(body.decode("utf-8", "replace"))
        if session is not None and self._csrf_ok(session, form.get("csrf", [""])[0]):
            self.guard.logout(self._cookie())
            log(f"fleet: logout from {self._client_ip()}")
        cookie = f"{COOKIE}=; Path={PREFIX}/; Secure; HttpOnly; SameSite=Strict; Max-Age=0"
        self._redirect(f"{PREFIX}/", [("Set-Cookie", cookie)])

    # -- hodiny --

    def _agent_device(self) -> Device | None:
        auth = self.headers.get("Authorization", "")
        if not auth.startswith("Bearer "):
            return None
        return self.relay.device_for_token(auth[7:].strip())

    def _agent(self, action: str):
        device = self._agent_device()
        if device is None:
            log(f"fleet: agent rejected from {self._client_ip()}")
            # Tělo se nečte; spojení se zavře, aby nezůstalo rozbité.
            self.close_connection = True
            return self._error(401, "unknown device")
        if action == "poll" and self.command == "GET":
            device.firmware = self.headers.get("X-Clock-Firmware", "")[:64]
            device.clock_name = self.headers.get("X-Clock-Name", "")[:32]
            job = self.relay.poll(device, POLL_WAIT_S)
            if job is None:
                return self._send(204, headers=[("Cache-Control", "no-store")])
            headers = [("Cache-Control", "no-store"), ("X-Job-Id", job.id),
                       ("X-Job-Method", job.method), ("X-Job-Path", job.path)]
            if job.if_none_match:
                headers.append(("X-Job-If-None-Match", job.if_none_match))
            return self._send(200, job.body, job.content_type or "application/octet-stream",
                              headers)
        if action == "result" and self.command == "POST":
            body = self._read_body(MAX_RESULT_BODY)
            if body is None:
                return
            try:
                status = int(self.headers.get("X-Job-Status", "0"))
            except ValueError:
                status = 0
            if not 100 <= status <= 599:
                status = 502
            result_type = self.headers.get("X-Job-Content-Type", "")
            if not CONTENT_TYPE.match(result_type):
                result_type = "application/octet-stream"
            encoding = self.headers.get("X-Job-Content-Encoding", "")
            encoding = "gzip" if encoding == "gzip" else ""
            tag = self.headers.get("X-Job-Tag", "")
            tag = tag if TAG.match(tag) else ""
            not_modified = bool(tag) and self.headers.get("X-Job-Not-Modified") == "1"
            accepted = self.relay.complete(device, self.headers.get("X-Job-Id", ""),
                                           status, result_type, encoding, body,
                                           tag, not_modified)
            return self._send(204 if accepted else 410,
                              headers=[("Cache-Control", "no-store")])
        self._error(404, "not found")

    def _device(self, name: str, path: str, query: str):
        session = self._session()
        is_page = self.command == "GET" and path in ("/", "/diagnostics")
        if session is None:
            if is_page:
                return self._redirect(f"{PREFIX}/")
            # Stranka podle hlavicky presmeruje na prihlaseni (SHIM).
            return self._json(401, {"ok": False, "message": "Přihlášení na serveru vypršelo."},
                              security_headers() + [("X-Fleet-Action", "login")])
        # Stranky (navigace prohlizece) hlavicku nemaji; vse ostatni ano.
        if not is_page and path != "/ui-language.js":
            problem = self._csrf_problem(session)
            if problem:
                log(f"fleet: csrf rejected ({problem}) {self.command} {path}")
                extra = []
                if problem == "stale token":
                    # Relace plati, jen stranka nese token starsi relace:
                    # SHIM si vezme aktualni z /fleet/api/session a zopakuje.
                    extra = [("X-Fleet-Action", "refresh")]
                return self._json(403, {"ok": False, "message": "Požadavek z cizí stránky byl odmítnut."},
                                  security_headers() + extra)
        if self.command not in ("GET", "POST") or not RELAY_PATH.match(path) or \
                any(path.startswith(blocked) or path == blocked.rstrip("/")
                    for blocked in RELAY_BLOCKED) or \
                (self.command == "POST" and path in RELAY_READ_ONLY) or \
                not QUERY.match(query):
            return self._error(403, "Tohle jde nastavit jen v domácí síti přímo na hodinách.")
        fail = self._page_error if is_page else self._error
        device = self.relay.device(name)
        if device is None:
            return fail(404, "Takové hodiny server nezná.")
        body = b""
        content_type = ""
        if self.command == "POST":
            body = self._read_body(MAX_REQUEST_BODY)
            if body is None:
                return
            content_type = self.headers.get("Content-Type", "")
            if not CONTENT_TYPE.match(content_type):
                content_type = "application/x-www-form-urlencoded"
        job = Job(secrets.token_hex(8), self.command,
                  path + (f"?{query}" if query else ""), content_type, body)
        if job.method == "GET":
            job.if_none_match = self.relay.cached_tag(device, job.path)
        problem = self.relay.submit(device, job)
        if problem == "offline":
            return fail(503, "Hodiny nejsou připojené k serveru.")
        if problem == "busy":
            return fail(503, "Hodiny mají rozpracováno příliš mnoho požadavků.")
        finished = self.relay.wait(device, job, JOB_TIMEOUT_S)
        # Jeden radek na pozadavek: kolik ceka na hodiny (fronta + long-poll)
        # a kolik trva cely. Jen cesta, bez dotazu.
        now = time.monotonic()
        queued = (job.picked or now) - job.created
        log(f"fleet: relay {name} {self.command} {path} "
            f"{job.status if finished else 'timeout'} {len(job.result_body)} B"
            f"{' (cache)' if job.from_cache else ''} "
            f"wait {queued * 1000:.0f} ms total {(now - job.created) * 1000:.0f} ms")
        if not finished:
            return fail(504, "Hodiny neodpověděly včas.")
        payload = job.result_body
        ctype = job.result_type or "application/octet-stream"
        extra = []
        if is_page and ctype.startswith("text/html"):
            if job.result_encoding == "gzip":
                try:
                    payload = gzip.decompress(payload)
                except (OSError, EOFError):
                    return fail(502, "Hodiny poslaly poškozenou stránku.")
            page = inject(payload.decode("utf-8", "replace"), name, session.csrf,
                          self.relay.overview())
            payload = page.encode()
        elif job.result_encoding == "gzip":
            extra.append(("Content-Encoding", "gzip"))
        self._send(job.status, payload, ctype, security_headers() + extra)

    def log_message(self, *args):  # tise, at neplni journal
        pass


def make_server(bind: str, port: int, guard: Guard, relay: Relay) -> ThreadingHTTPServer:
    handler = type("BoundHandler", (Handler,), {"guard": guard, "relay": relay})
    server = ThreadingHTTPServer((bind, port), handler)
    server.daemon_threads = True
    return server


# --- prikazy --------------------------------------------------------------

def command_hash_password() -> None:
    first = getpass.getpass("Nové heslo (aspoň 12 znaků): ")
    if len(first) < 12:
        raise SystemExit("Heslo je kratší než 12 znaků.")
    if getpass.getpass("Znovu: ") != first:
        raise SystemExit("Hesla se neshodují.")
    print(f"FLEET_PASSWORD_HASH={hash_password(first)}")


def command_new_totp(account: str) -> None:
    secret = base64.b32encode(secrets.token_bytes(20)).decode().rstrip("=")
    label = urllib.parse.quote(f"Hodiny:{account}")
    print(f"FLEET_TOTP_SECRET={secret}")
    print(f"otpauth://totp/{label}?secret={secret}&issuer=Hodiny&digits=6&period=30",
          file=sys.stderr)


def command_add_device(name: str) -> None:
    if not NAME.match(name):
        raise SystemExit("Název: 1–32 malých písmen, číslic a pomlček.")
    devices = load_devices()
    token = secrets.token_urlsafe(32)
    devices[name] = {"token_sha256": token_hash(token),
                     "added": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())}
    save_devices(devices)
    print(f"Hodiny {name} zaregistrované. Token (ukáže se jen teď):")
    print(token)


def command_remove_device(name: str) -> None:
    devices = load_devices()
    if devices.pop(name, None) is None:
        raise SystemExit(f"Hodiny {name} nejsou zaregistrované.")
    save_devices(devices)
    print(f"Hodiny {name} odebrané; jejich token přestal platit.")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = parser.add_subparsers(dest="command")
    sub.add_parser("hash-password")
    totp = sub.add_parser("new-totp")
    totp.add_argument("--account", default="spravce")
    add = sub.add_parser("add-device")
    add.add_argument("name")
    remove = sub.add_parser("remove-device")
    remove.add_argument("name")
    sub.add_parser("list-devices")
    args = parser.parse_args()
    if args.command == "hash-password":
        return command_hash_password()
    if args.command == "new-totp":
        return command_new_totp(args.account)
    if args.command == "add-device":
        return command_add_device(args.name)
    if args.command == "remove-device":
        return command_remove_device(args.name)
    if args.command == "list-devices":
        for name, item in sorted(load_devices().items()):
            print(name, item.get("added", ""))
        return
    STATE.mkdir(mode=0o700, parents=True, exist_ok=True)
    guard = Guard(os.environ.get("FLEET_PASSWORD_HASH", ""),
                  os.environ.get("FLEET_TOTP_SECRET", ""))
    if not guard.configured():
        log("fleet: FLEET_PASSWORD_HASH or FLEET_TOTP_SECRET missing, login disabled")
    make_server(BIND, PORT, guard, Relay()).serve_forever()


if __name__ == "__main__":
    main()
