#!/usr/bin/env python3
"""Rozvrh a domaci ukoly ze Skoly OnLine pro hodiny.

Hodiny se do Skoly OnLine neprihlasuji: heslo ditete by muselo lezet v jejich
konfiguraci (a v zaloze nastaveni), OAuth token by hlidaly samy a odpoved
rozvrhu ma desitky kilobajtu, ze kterych displej potrebuje par radku. Tenhle
server se proto prihlasi jednou, drzi si token v pameti, sam se po case
zepta na rozvrh a ukoly a hodinam vraci jen hotove radky z feed.render().

    GET /school.json   odpoved pro hodiny; 503, dokud prvni stazeni nedobehlo
                       nebo kdyz jsou data starsi nez MAX_AGE_HOURS

Server posloucha jen na 127.0.0.1 a Caddy ho pousti s heslem: odpoved nese
jmeno ditete a jeho ukoly.

Prihlaseni jde pres refresh token, heslem jen na zacatku a kdyz refresh
selze. Spatne heslo se neopakuje kazdych dvacet minut - skolni system by ucet
po serii chyb mohl zamknout a dite by se neprihlasilo ani samo. Po odmitnutem
hesle proto server ceka AUTH_BACKOFF_HOURS a starsi data dal vydava, nejdyl
ale MAX_AGE_HOURS. Pak radsi 503 nez vcerejsi rozvrh bez suplovani, ktery by
se po dvou tydnech tvaril jako prazdniny.

Skola OnLine je cizi server a neoficialni API, takze se pta co nejmene: dite
se hleda v /v1/user jednou denne, ne pri kazdem stazeni; o vikendu a
o prazdninach se stahuje zridka; chyby se opakuji s rostoucim odstupem.
Zpravy a znamky (druha stranka obrazovky) se pripojuji k beznemu stazeni
nejvys jednou za hodinu, respektive za tri, a v noci jen poprve. Stejne
se pripojuje nastenka materske skoly z nasems.cz (NASEMS_LOGIN), jen kdyz je
vyplnene prihlaseni, nejvys jednou za dve hodiny.

    python3.11 serve.py --probe   prihlasi se, vypise deti a tvar odpovedi

Posledni stazena data se po kazdem uspechu ulozi do SCHOOL_STATE_DIR
(state.json, jen pro uzivatele sluzby). Po restartu, treba po nocni
aktualizaci v tichych hodinach, je server vydava hned a nemusi cekat na rano;
kdyz jsou mladsi nez jejich interval, prvni dotaz do Skoly OnLine pocka.

--probe je na overeni, ze API porad vraci to, co feed.py cte. Vypisuje
skutecna data (jmena, ukoly), takze patri jen do terminalu spravce.
"""
from __future__ import annotations

import json
import math
import os
import random
import sys
import http.cookiejar
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
from datetime import datetime, timedelta
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

import feed

PORT = int(os.environ.get("SCHOOL_PORT", "8094"))
BIND = os.environ.get("SCHOOL_BIND", "127.0.0.1")
API = os.environ.get("SCHOOL_API", "https://aplikace.skolaonline.cz/solapi/api").rstrip("/")
USERNAME = os.environ.get("SCHOOL_USERNAME", "")
PASSWORD = os.environ.get("SCHOOL_PASSWORD", "")
STUDENT = os.environ.get("SCHOOL_STUDENT", "")
# client_id a scope pouziva mobilni aplikace; jine server odmitne.
CLIENT_ID = "test_client"
SCOPE = "openid offline_access profile sol_api"
USER_AGENT = "WaveshareHodiny/1.0 (+https://github.com/teffteff/waveshare-hodiny)"
TIMEOUT_SECONDS = 30
# Ve dne se rozvrh meni (suplovani se vyvesi rano i vecer predtem), ukoly
# pribyvaji po vyucovani. V noci se nedeje nic a skolni server nema duvod
# dostavat dotazy i tehdy.
DAY_POLL_MINUTES = int(os.environ.get("SCHOOL_DAY_POLL_MINUTES", "20"))
NIGHT_POLL_MINUTES = int(os.environ.get("SCHOOL_NIGHT_POLL_MINUTES", "120"))
# Ve dne mimo vyucovani a mimo odpoledne pred skolnim dnem (patek po skole,
# sobota, nedelni dopoledne): nic, co by dite hned potrebovalo, neprijde.
IDLE_POLL_MINUTES = int(os.environ.get("SCHOOL_IDLE_POLL_MINUTES", "120"))
# V celem stazenem rozvrhu neni jedina hodina: prazdniny.
HOLIDAY_POLL_MINUTES = int(os.environ.get("SCHOOL_HOLIDAY_POLL_MINUTES", "360"))
DAY_HOURS = (6, 21)
# Kazdy odstup se nahodne natahne nebo zkrati o tolik procent. Dotazy pak
# nechodi v presnem rytmu a po restartu nekolika sluzeb najednou se nesejdou.
POLL_JITTER_PERCENT = float(os.environ.get("SCHOOL_POLL_JITTER_PERCENT", "10"))
# Noc bez jedineho dotazu, "od-do" v celych hodinach mistniho casu; prazdne
# = bez ticha. Plati pro vsechno vcetne opakovani po chybe a startu sluzby:
# kdo se v tichu probudi, pocka do konce a pak jeste nahodnou chvili, at se
# rano nepta presne v pet.
QUIET_HOURS = os.environ.get("SCHOOL_QUIET_HOURS", "23-5")
QUIET_SPREAD_MINUTES = 10
# Chyba site nebo serveru: 10, 20, 40... minut, nejvys MAX_RETRY_MINUTES.
RETRY_MINUTES = 10
MAX_RETRY_MINUTES = 120
AUTH_BACKOFF_HOURS = int(os.environ.get("SCHOOL_AUTH_BACKOFF_HOURS", "6"))
# Dite se v /v1/user hleda jednou denne; trida se meni jednou za rok.
STUDENT_REFRESH_HOURS = 24
# Starsi snimek uz hodinam nevydavat. Ctrnact hodin prezije noc bez dotazu
# (posledni prazdninove stazeni pred 17. hodinou, dalsi az po peti rano)
# i jeden odklad po spatnem hesle, ale ne cely den bez suplovani.
MAX_AGE_HOURS = float(os.environ.get("SCHOOL_MAX_AGE_HOURS", "14"))
# Kam se uklada posledni stazeny snimek; prazdne = jen v pameti. Soubor nese
# jmeno ditete, rozvrh, ukoly, titulky zprav a znamky, proto 600.
STATE_DIR = os.environ.get("SCHOOL_STATE_DIR", "")
STATE_VERSION = 1
# Neprectene zpravy a znamky pro druhou stranku obrazovky. Vypnuti (0) je
# i vec soukromi: titulky zprav a znamky pak nikdy neopusti Skolu OnLine.
MESSAGES_ENABLED = os.environ.get("SCHOOL_MESSAGES", "1") != "0"
MARKS_ENABLED = os.environ.get("SCHOOL_MARKS", "1") != "0"
MESSAGES_POLL_MINUTES = int(os.environ.get("SCHOOL_MESSAGES_POLL_MINUTES", "60"))
MARKS_POLL_MINUTES = int(os.environ.get("SCHOOL_MARKS_POLL_MINUTES", "180"))
# Neprectene zpravy za dva tydny jsou mezi nejnovejsimi; tricet staci i pro
# tridu, ktera pise casto.
MESSAGES_PAGE_SIZE = 30
# Nastenka materske skoly na nasems.cz. Prazdne prihlaseni = nestahuje se.
# Web nema API: server se prihlasi formularem a cte HTML nastenky.
NASEMS_URL = os.environ.get("NASEMS_URL", "https://nasems.cz").rstrip("/")
NASEMS_LOGIN = os.environ.get("NASEMS_LOGIN", "")
NASEMS_PASSWORD = os.environ.get("NASEMS_PASSWORD", "")
NOTICES_ENABLED = bool(NASEMS_LOGIN and NASEMS_PASSWORD)
NOTICES_POLL_MINUTES = int(os.environ.get("NASEMS_POLL_MINUTES", "120"))
# Rozvrh na dva tydny dopredu: pres vikend a kratke volno se tak vzdycky najde
# pristi skolni den. Delsi prazdniny hodiny poznaji podle prazdneho rozvrhu.
TIMETABLE_DAYS = int(os.environ.get("SCHOOL_TIMETABLE_DAYS", "14"))
# Token vyprsi za hodinu; minuta rezervy, aby dotaz nespadl na hrane.
TOKEN_MARGIN_SECONDS = 60


class SchoolError(Exception):
    """Sit, neocekavana odpoved nebo chyba serveru Skoly OnLine."""

    def __init__(self, message: str, retry_after: float | None = None):
        super().__init__(message)
        # Sekundy z hlavicky Retry-After u 429 a 503, jinak None.
        self.retry_after = retry_after


def _retry_after(status: int, headers) -> float | None:
    if status not in (429, 503) or headers is None:
        return None
    value = (headers.get("Retry-After") or "").strip()
    # Datum misto sekund se nepouziva; odstup pak urci backoff sam.
    return float(value) if value.isdigit() else None


class AuthError(SchoolError):
    """Skola OnLine odmitla jmeno a heslo."""


class Client:
    def __init__(self, username: str, password: str, api: str = API):
        self.username = username
        self.password = password
        self.api = api
        self.access_token = ""
        self.refresh_token = ""
        self.expires_at = 0.0

    def _request(self, url: str, data: bytes | None = None, headers: dict | None = None):
        request = urllib.request.Request(url, data=data, headers={
            "User-Agent": USER_AGENT, "Accept": "application/json", **(headers or {})})
        try:
            with urllib.request.urlopen(request, timeout=TIMEOUT_SECONDS) as response:
                return response.status, response.read(), response.headers
        except urllib.error.HTTPError as error:
            return error.code, error.read(), error.headers
        except (urllib.error.URLError, OSError) as error:
            raise SchoolError(f"Skola OnLine neodpovida: {error}") from error

    def _token(self, form: dict) -> None:
        status, body, headers = self._request(
            f"{self.api}/connect/token", urllib.parse.urlencode(form).encode(),
            {"Content-Type": "application/x-www-form-urlencoded"})
        # Spatne heslo i neplatny refresh token vraci 400 invalid_grant.
        if status in (400, 401):
            raise AuthError(f"prihlaseni odmitnuto (HTTP {status})")
        if status != 200:
            raise SchoolError(f"token vratil HTTP {status}", _retry_after(status, headers))
        try:
            payload = json.loads(body)
        except ValueError as error:
            raise SchoolError("token nevratil JSON") from error
        if not payload.get("access_token"):
            raise SchoolError("odpoved tokenu nema access_token")
        self.access_token = payload["access_token"]
        self.refresh_token = payload.get("refresh_token") or self.refresh_token
        self.expires_at = time.time() + int(payload.get("expires_in") or 3600)

    def _ensure_token(self) -> None:
        if self.access_token and time.time() + TOKEN_MARGIN_SECONDS < self.expires_at:
            return
        if self.refresh_token:
            try:
                self._token({"grant_type": "refresh_token", "client_id": CLIENT_ID,
                             "refresh_token": self.refresh_token})
                return
            except AuthError:
                self.refresh_token = ""
        if not self.username or not self.password:
            raise AuthError("chybi SCHOOL_USERNAME nebo SCHOOL_PASSWORD")
        self._token({"grant_type": "password", "client_id": CLIENT_ID, "scope": SCOPE,
                     "username": self.username, "password": self.password})

    def get(self, path: str, params: dict | None = None):
        self._ensure_token()
        url = f"{self.api}{path}"
        if params:
            url += "?" + urllib.parse.urlencode(params)
        for attempt in range(2):
            status, body, headers = self._request(
                url, headers={"Authorization": f"Bearer {self.access_token}"})
            if status == 401 and attempt == 0:
                # Token mohl server zneplatnit driv, nez mel vyprset.
                self.access_token = ""
                self._ensure_token()
                continue
            break
        if status != 200:
            raise SchoolError(f"{path} vratil HTTP {status}", _retry_after(status, headers))
        try:
            return json.loads(body)
        except ValueError as error:
            raise SchoolError(f"{path} nevratil JSON") from error

    def user(self):
        return self.get("/v1/user")

    def timetable(self, student_id: str, first, last):
        return self.get("/v1/timeTable", {"StudentId": student_id, "DateFrom": first.isoformat(),
                                          "DateTo": last.isoformat()})

    def homework(self, student_id: str):
        path = f"/v1/students/{urllib.parse.quote(student_id, safe='')}/homeworks"
        return self.get(path, {"StudentId": student_id, "Filter": "active",
                               "Pagination.PageNumber": 1, "Pagination.PageSize": 50})

    def messages(self):
        # Seznam zpravy neoznaci jako prectene (overeno 15. 9. 2026: dve cteni
        # za sebou vratila stejne priznaky read). Detail zpravy se nevola.
        return self.get("/v1/messages/received", {"Pagination.PageNumber": 1,
                                                  "Pagination.PageSize": MESSAGES_PAGE_SIZE})

    def marks(self, student_id: str):
        path = f"/v1/students/{urllib.parse.quote(student_id, safe='')}/marks/list"
        return self.get(path, {"SigningFilter": "all"})


class NasemsClient:
    """Prihlaseni do nasems.cz drzi PHP session v cookie. Dokud plati, stoji
    jedno stazeni jediny dotaz; po vyprseni nastenka vrati prihlasovaci
    formular a klient se prihlasi znovu."""

    def __init__(self, login: str, password: str, base: str = NASEMS_URL):
        self.login = login
        self.password = password
        self.base = base
        self.cookies = http.cookiejar.CookieJar()
        self.opener = urllib.request.build_opener(urllib.request.HTTPCookieProcessor(self.cookies))

    def _open(self, url: str, data: bytes | None = None) -> str:
        request = urllib.request.Request(url, data=data, headers={"User-Agent": USER_AGENT})
        try:
            with self.opener.open(request, timeout=TIMEOUT_SECONDS) as response:
                charset = response.headers.get_content_charset() or "utf-8"
                return response.read().decode(charset, errors="replace")
        except urllib.error.HTTPError as error:
            raise SchoolError(f"nasems.cz vratil HTTP {error.code}",
                              _retry_after(error.code, error.headers)) from error
        except (urllib.error.URLError, OSError) as error:
            raise SchoolError(f"nasems.cz neodpovida: {error}") from error

    def _sign_in(self) -> None:
        form = urllib.parse.urlencode({"login": self.login, "password": self.password}).encode()
        self._open(f"{self.base}/", form)

    def board(self) -> str:
        url = f"{self.base}/prihlaseno/nastenka"
        if not any(True for _ in self.cookies):
            self._sign_in()
        page = self._open(url)
        if feed.is_nasems_login_page(page):
            self._sign_in()
            page = self._open(url)
            if feed.is_nasems_login_page(page):
                self.cookies.clear()
                raise AuthError("nasems.cz odmitl prihlaseni")
        return page


def find_student(client: Client) -> dict:
    students = feed.students_from_user(client.user())
    student = feed.pick_student(students, STUDENT)
    if student is None:
        names = ", ".join(item["name"] for item in students) or "zadne"
        raise SchoolError(f"ucet nevidi zadane dite (SCHOOL_STUDENT); deti: {names}")
    return student


def collect(client: Client, now: datetime, student: dict | None = None) -> dict:
    """Jedno kompletni stazeni do snimku. Selze-li cokoli, vyhodi vyjimku
    a predchozi snimek zustane - napul stazeny rozvrh by vypadal jako volno.

    Dite z predchoziho snimku se znovu nehleda; bez nej se zepta /v1/user."""
    if student is None:
        student = find_student(client)
    today = now.date()
    lessons = feed.normalize_timetable(
        client.timetable(student["id"], today, today + timedelta(days=TIMETABLE_DAYS)))
    homework = feed.normalize_homework(client.homework(student["id"]))
    return {
        "generated": now.isoformat(timespec="seconds"),
        # ID jen pro pristi stazeni; render() ho hodinam neposila.
        "student": {"id": student["id"], "name": student["name"]},
        "lessons": lessons,
        "homework": homework,
    }


def poll_minutes(snapshot: dict, now: datetime) -> int:
    """Za kolik minut se zeptat znovu po uspesnem stazeni.

    Casto jen tehdy, kdyz se neco muze zmenit a dite to potrebuje vedet:
    dnes do konce vyucovani (suplovani) a od poledne pred skolnim dnem
    (ukoly po vyucovani, suplovani na zitrek). Patek odpoledne, sobota
    a nedelni dopoledne staci obcas."""
    lessons = snapshot["lessons"]
    if not lessons:
        return HOLIDAY_POLL_MINUTES
    local = now.astimezone(feed.TZ).replace(tzinfo=None)
    if local.hour < DAY_HOURS[0]:
        # Rano po tichych hodinach nespat pres zacatek dne, jinak by prvni
        # suplovani prislo az po sedme. Kratsi odstup nez denni ne: jitter
        # by pred sestou jinak pridal jeste jeden drobny dotaz.
        day_start = local.replace(hour=DAY_HOURS[0], minute=0, second=0, microsecond=0)
        until_day = math.ceil((day_start - local).total_seconds() / 60)
        return min(NIGHT_POLL_MINUTES, max(DAY_POLL_MINUTES, until_day))
    if local.hour >= DAY_HOURS[1]:
        return NIGHT_POLL_MINUTES
    today = local.date().isoformat()
    tomorrow = (local.date() + timedelta(days=1)).isoformat()
    today_end = max((lesson["end"] for lesson in lessons if lesson["start"].startswith(today)),
                    default="")
    # Casy jsou mistni ISO retezce, takze se daji porovnat primo.
    if local.isoformat(timespec="minutes") < today_end:
        return DAY_POLL_MINUTES
    if local.hour >= 12 and any(lesson["start"].startswith(tomorrow) for lesson in lessons):
        return DAY_POLL_MINUTES
    return IDLE_POLL_MINUTES


def _quiet_bounds(spec: str) -> tuple[int, int] | None:
    try:
        start, end = (int(part) % 24 for part in spec.split("-"))
    except ValueError:
        return None
    return None if start == end else (start, end)


def quiet_wait(now: datetime, spec: str | None = None, rng=random) -> float:
    """Kolik sekund jeste mlcet, nebo 0 mimo tiche hodiny."""
    bounds = _quiet_bounds(QUIET_HOURS if spec is None else spec)
    if bounds is None:
        return 0.0
    start, end = bounds
    local = now.astimezone(feed.TZ)
    inside = start <= local.hour < end if start < end else (local.hour >= start or local.hour < end)
    if not inside:
        return 0.0
    wake = local.replace(hour=end, minute=0, second=0, microsecond=0)
    if wake <= local:
        wake = (wake.replace(tzinfo=None) + timedelta(days=1)).replace(tzinfo=feed.TZ)
    # Pres casove razitko, aby prechod na letni cas nepocital hodinu navic.
    return wake.timestamp() - local.timestamp() + rng.uniform(0, QUIET_SPREAD_MINUTES * 60)


def _state_problem(state) -> str:
    """Proc ulozeny stav nepouzit, nebo prazdny retezec."""
    if not isinstance(state, dict) or state.get("version") != STATE_VERSION:
        return "jina verze souboru"
    if state.get("account") != USERNAME or state.get("selector") != STUDENT:
        return "zmenil se ucet nebo SCHOOL_STUDENT"
    snapshot = state.get("snapshot")
    if not isinstance(state.get("fetched_at"), (int, float)) or not isinstance(snapshot, dict):
        return "chybi snimek"
    if not all(isinstance(snapshot.get(key), list) for key in ("lessons", "homework")):
        return "snimek nema rozvrh a ukoly"
    if not isinstance(snapshot.get("student"), dict):
        return "snimek nema dite"
    return ""


def jittered(seconds: float, percent: float | None = None, rng=random) -> float:
    """Odstup nahodne v rozmezi +-percent (vychozi POLL_JITTER_PERCENT)."""
    spread = max(0.0, min(POLL_JITTER_PERCENT if percent is None else percent, 50.0)) / 100
    return seconds * rng.uniform(1 - spread, 1 + spread)


class Poller:
    def __init__(self, client: Client, state_path: str | None = None,
                 nasems: NasemsClient | None = None):
        self.client = client
        self.nasems = nasems
        self.state_path = state_path
        self.lock = threading.Lock()
        self.snapshot: dict | None = None
        self.fetched_at = 0.0
        self.problem = ""
        self.failures = 0
        self.student: dict | None = None
        self.student_until = 0.0
        # Zpravy a znamky: posledni uspesna data, kdy se naposled zkousely
        # a kdy se naposled povedly.
        self.extras: dict[str, list] = {}
        self.extras_tried: dict[str, float] = {}
        self.extras_ok: dict[str, float] = {}

    def current(self) -> dict | None:
        """Snimek pro hodiny, nebo None, dokud neni nebo kdyz uz je prilis stary."""
        with self.lock:
            if self.snapshot is None or time.time() - self.fetched_at > MAX_AGE_HOURS * 3600:
                return None
            return {**self.snapshot, "problem": self.problem}

    def poll_once(self) -> float:
        """Stahne a vrati, za kolik sekund znovu."""
        now = datetime.now(feed.TZ)
        if time.time() >= self.student_until:
            self.student = None
        try:
            snapshot = collect(self.client, now, self.student)
        except AuthError as error:
            self._fail(f"Skola OnLine odmitla prihlaseni: {error}")
            return AUTH_BACKOFF_HOURS * 3600
        except SchoolError as error:
            self._fail(str(error))
            return self._retry_seconds(error.retry_after)
        except Exception as error:  # noqa: BLE001 - vlakno nesmi umrit na zmene API
            self._fail(f"neocekavana odpoved: {error!r}")
            return self._retry_seconds()
        self._refresh_extras(snapshot["student"]["id"], now)
        snapshot.update(self.extras)
        with self.lock:
            self.snapshot = snapshot
            self.fetched_at = time.time()
            self.problem = ""
        self.failures = 0
        if self.student is None:
            self.student = snapshot["student"]
            self.student_until = time.time() + STUDENT_REFRESH_HOURS * 3600
        self.save_state()
        extras = "".join(f", {name}: {len(items)}" for name, items in self.extras.items())
        print(f"stazeno: {len(snapshot['lessons'])} hodin, {len(snapshot['homework'])} ukolu"
              f"{extras}", flush=True)
        return poll_minutes(snapshot, now) * 60

    # --- ulozeny stav ------------------------------------------------------

    def save_state(self) -> None:
        """Atomicky zapis: docasny soubor, fsync, prejmenovani. Vypadek proudu
        pri zapisu tak necha stary soubor, ne pulku noveho."""
        if not self.state_path:
            return
        with self.lock:
            snapshot, fetched_at = self.snapshot, self.fetched_at
        state = {
            "version": STATE_VERSION,
            # Jiny ucet nebo jine dite v school.env ulozeny stav zneplatni.
            "account": USERNAME,
            "selector": STUDENT,
            "fetched_at": fetched_at,
            "snapshot": snapshot,
            "student": self.student,
            "student_until": self.student_until,
            "extras": self.extras,
            "extras_ok": self.extras_ok,
            "extras_tried": self.extras_tried,
        }
        temporary = f"{self.state_path}.tmp"
        try:
            descriptor = os.open(temporary, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
            with os.fdopen(descriptor, "w", encoding="utf-8") as handle:
                json.dump(state, handle, ensure_ascii=False, separators=(",", ":"))
                handle.flush()
                os.fsync(handle.fileno())
            os.replace(temporary, self.state_path)
        except OSError as error:
            # Nezapsany stav neni duvod prestat vydavat rozvrh.
            print(f"stav se nepodarilo ulozit: {error}", file=sys.stderr, flush=True)

    def load_state(self) -> bool:
        """Nacte ulozeny stav. Cokoli podezreleho se zahodi a server zacne
        jako po prvnim startu; stary soubor prepise prvni uspesne stazeni."""
        if not self.state_path:
            return False
        try:
            with open(self.state_path, encoding="utf-8") as handle:
                state = json.load(handle)
        except FileNotFoundError:
            return False
        except (OSError, ValueError) as error:
            print(f"ulozeny stav je necitelny, zahazuji: {error}", file=sys.stderr, flush=True)
            return False
        reason = _state_problem(state)
        if reason:
            print(f"ulozeny stav se nepouzije: {reason}", flush=True)
            return False
        snapshot = state["snapshot"]
        extras = {name: items for name, items in state.get("extras", {}).items()
                  if isinstance(items, list)}
        # Mezitim vypnute zpravy nebo znamky se nevydavaji ani z disku.
        for name, enabled in (("messages", MESSAGES_ENABLED), ("marks", MARKS_ENABLED),
                              ("notices", self.nasems is not None)):
            if not enabled:
                snapshot.pop(name, None)
                extras.pop(name, None)
        try:
            feed.render(snapshot)
        except Exception as error:  # noqa: BLE001 - radsi zadna data nez pad obsluhy
            print(f"ulozeny stav nejde vykreslit, zahazuji: {error!r}", file=sys.stderr, flush=True)
            return False
        numbers = lambda value: {key: float(stamp) for key, stamp in value.items()
                                 if isinstance(stamp, (int, float))} if isinstance(value, dict) else {}
        with self.lock:
            self.snapshot = snapshot
            # Cas z budoucnosti (posunute hodiny serveru) by drzel data vecne cerstva.
            self.fetched_at = min(float(state["fetched_at"]), time.time())
        student = state.get("student")
        if isinstance(student, dict) and student.get("id"):
            self.student = student
            self.student_until = float(state.get("student_until") or 0)
        self.extras = extras
        self.extras_ok = numbers(state.get("extras_ok"))
        self.extras_tried = numbers(state.get("extras_tried"))
        age = (time.time() - self.fetched_at) / 3600
        print(f"nacten ulozeny stav, star {age:.1f} h", flush=True)
        return True

    def initial_wait(self) -> float:
        """Prvni dotaz po startu: hned, nebo az data z disku zestarnou na svuj
        interval. Restart kvuli nasazeni tak nestoji dalsi kolo dotazu."""
        with self.lock:
            snapshot, fetched_at = self.snapshot, self.fetched_at
        if snapshot is None:
            return 0.0
        fetched = datetime.fromtimestamp(fetched_at, feed.TZ)
        return max(0.0, fetched_at + poll_minutes(snapshot, fetched) * 60 - time.time())

    def _refresh_extras(self, student_id: str, now: datetime) -> None:
        """Zpravy a znamky, pokud uz na ne prisel cas. Chyba tady rozvrh
        nezastavi: zustanou starsi data, po MAX_AGE_HOURS se zahodi."""
        hour = now.astimezone(feed.TZ).hour
        night = not DAY_HOURS[0] <= hour < DAY_HOURS[1]
        sources = (
            ("messages", MESSAGES_ENABLED, MESSAGES_POLL_MINUTES,
             lambda: feed.normalize_messages(self.client.messages())),
            ("marks", MARKS_ENABLED, MARKS_POLL_MINUTES,
             lambda: feed.normalize_marks(self.client.marks(student_id))),
            ("notices", self.nasems is not None, NOTICES_POLL_MINUTES,
             lambda: feed.normalize_notices(self.nasems.board())),
        )
        for name, enabled, minutes, fetch in sources:
            if not enabled:
                continue
            clock = time.time()
            if clock - self.extras_tried.get(name, 0.0) < minutes * 60:
                continue
            # V noci se nic nenovi; jen prvni stazeni po startu nesmi cekat do rana.
            if night and name in self.extras:
                continue
            self.extras_tried[name] = clock
            try:
                self.extras[name] = fetch()
                self.extras_ok[name] = clock
            except Exception as error:  # noqa: BLE001 - rozvrh nesmi padnout kvuli zpravam
                print(f"chyba ({name}): {error!r}", file=sys.stderr, flush=True)
                if clock - self.extras_ok.get(name, 0.0) > MAX_AGE_HOURS * 3600:
                    self.extras.pop(name, None)

    def _retry_seconds(self, retry_after: float | None = None) -> float:
        minutes = min(RETRY_MINUTES * 2 ** min(self.failures - 1, 8), MAX_RETRY_MINUTES)
        # Retry-After se dodrzi, ale ne dele nez odklad po spatnem hesle.
        return max(minutes * 60, min(retry_after or 0, AUTH_BACKOFF_HOURS * 3600))

    def _fail(self, message: str) -> None:
        print(f"chyba: {message}", file=sys.stderr, flush=True)
        self.failures += 1
        # Chyba muze znamenat, ze ucet dite uz nevidi; pristi pokus ho najde znovu.
        self.student = None
        with self.lock:
            self.problem = message

    def run(self) -> None:
        wait = self.initial_wait()
        while True:
            quiet = quiet_wait(datetime.now(feed.TZ))
            if quiet > 0:
                print(f"ticho do rana: {quiet / 3600:.1f} h", flush=True)
                time.sleep(quiet)
                continue
            if wait > 0:
                # Po probuzeni se znovu zkontroluje ticho.
                time.sleep(wait)
                wait = 0.0
                continue
            time.sleep(jittered(self.poll_once()))


class Handler(BaseHTTPRequestHandler):
    server_version = "school-web/1.0"
    # Zaseknute spojeni se samo zavre a nedrzi vlakno.
    timeout = 15
    poller: Poller

    def _send(self, code: int, body: bytes, ctype: str = "text/plain; charset=utf-8") -> None:
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(body)

    def do_GET(self):
        path = urllib.parse.urlsplit(self.path).path
        if path != "/school.json":
            self._send(404, b"not found\n")
            return
        snapshot = self.poller.current()
        if snapshot is None:
            ready = self.poller.snapshot is not None
            self._send(503, b"school data stale\n" if ready else b"school data not ready\n")
            return
        body = json.dumps(feed.render(snapshot), ensure_ascii=False,
                          separators=(",", ":")).encode("utf-8")
        self._send(200, body, "application/json; charset=utf-8")

    do_HEAD = do_GET

    def log_message(self, *args):  # tise, at neplni journal
        pass


def probe() -> None:
    client = Client(USERNAME, PASSWORD)
    now = datetime.now(feed.TZ)
    user = client.user()
    print("user keys:", sorted(user) if isinstance(user, dict) else type(user).__name__)
    students = feed.students_from_user(user)
    for student in students:
        print("dite:", student)
    student = feed.pick_student(students, STUDENT)
    if student is None:
        sys.exit("SCHOOL_STUDENT nesedi na zadne dite")
    timetable = client.timetable(student["id"], now.date(), now.date() + timedelta(days=TIMETABLE_DAYS))
    days = timetable.get("days", []) if isinstance(timetable, dict) else []
    print("dnu v rozvrhu:", len(days))
    first = next((day["schedules"][0] for day in days if day.get("schedules")), None)
    print("prvni hodina:", json.dumps(first, ensure_ascii=False, indent=1) if first else "zadna")
    homework = client.homework(student["id"])
    items = homework.get("homeworks", []) if isinstance(homework, dict) else []
    print("homework keys:", sorted(homework) if isinstance(homework, dict) else type(homework).__name__)
    print("ukolu:", len(items))
    if items:
        print("prvni ukol:", json.dumps(items[0], ensure_ascii=False, indent=1))
    snapshot = collect(client, now)
    if MESSAGES_ENABLED:
        snapshot["messages"] = feed.normalize_messages(client.messages())
        print("neprectenych zprav:", len(snapshot["messages"]))
    if MARKS_ENABLED:
        snapshot["marks"] = feed.normalize_marks(client.marks(student["id"]))
        print("znamek:", len(snapshot["marks"]))
    if NOTICES_ENABLED:
        snapshot["notices"] = feed.normalize_notices(NasemsClient(NASEMS_LOGIN, NASEMS_PASSWORD).board())
        print("oznameni na nastence:", len(snapshot["notices"]))
    print("odpoved pro hodiny:")
    print(json.dumps(feed.render(snapshot, now), ensure_ascii=False, indent=1))


def main() -> None:
    state_path = os.path.join(STATE_DIR, "state.json") if STATE_DIR else None
    nasems = NasemsClient(NASEMS_LOGIN, NASEMS_PASSWORD) if NOTICES_ENABLED else None
    poller = Poller(Client(USERNAME, PASSWORD), state_path, nasems)
    poller.load_state()
    Handler.poller = poller
    # Port nejdriv: kdyby byl obsazeny, proces spadne driv, nez se zacne
    # prihlasovat, a restart od systemd neposle do Skoly OnLine dalsi login.
    server = ThreadingHTTPServer((BIND, PORT), Handler)
    threading.Thread(target=poller.run, name="poller", daemon=True).start()
    server.serve_forever()


if __name__ == "__main__":
    if sys.argv[1:] == ["--probe"]:
        probe()
    else:
        main()
