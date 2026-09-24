#!/usr/bin/env python3
"""Hlidac serveru: kdyz se neco rozbije, prijde push na telefon.

Do zari 2026 se o poruse vedelo jen tehdy, kdyz nekdo pustil
tools/check-stack.sh. news.service 16. 9. vyprsel timeout a reload Caddy
20. 9. spadl, a oboji se naslo nahodou. Tenhle skript bezi na serveru sam:

    check.py                 hodinova kontrola (health.timer); push jen pri zmene
    check.py --unit X        jednotka X selhala (notify-failure@X, pres OnFailure=)
    check.py --print         jen vypise vysledek kontrol, nic neposle ani neulozi
    check.py --test-push     posle zkusebni push
    check.py --notify T M    posle push s titulkem T a textem M (ha-update-check.sh)

Kontroly se ptaji sluzeb po loopbacku, bez Caddy a bez hesel, a zvenci chodi
jen na vlastni jmeno kvuli certifikatu. Nic z nich nestahuje od cizich zdroju:
/status adresy vraci jen to, co sluzba uz ma v pameti.

Aby jedno zakolisani nepipalo, problem z hodinove kontroly musi vydrzet dva
behy po sobe (HEALTH_CONFIRM_RUNS). Selhana jednotka (--unit) jde hned, ale
tataz nejvys jednou za 30 minut, aby smycka restartu nezahltila telefon.
Kdyz problem zmizi, prijde jeden push "vyreseno".

Tema ntfy je vlastni, ne to od upozorneni na dest a letadla ani od hlidani
obchodu, aby se dalo ztlumit zvlast. Tema je heslo.
"""
from __future__ import annotations

import argparse
import datetime as dt
import email.utils
import json
import os
import re
import shutil
import socket
import ssl
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request

NTFY_URL = os.environ.get("NTFY_URL", "https://ntfy.sh").rstrip("/")
NTFY_TOPIC = os.environ.get("NTFY_TOPIC", "").strip()
NTFY_TOKEN = os.environ.get("NTFY_TOKEN", "").strip()
# Jmeno serveru kvuli certifikatu. Bez nej se certifikat nekontroluje.
HOST = os.environ.get("HEALTH_HOST", "").strip()
STATE_DIR = os.environ.get("HEALTH_STATE_DIR", "/var/lib/health")
CONFIRM_RUNS = int(os.environ.get("HEALTH_CONFIRM_RUNS", "2"))
CERT_MIN_DAYS = int(os.environ.get("HEALTH_CERT_MIN_DAYS", "21"))
DISK_MAX_PERCENT = int(os.environ.get("HEALTH_DISK_MAX_PERCENT", "85"))
UNIT_REPEAT_SECONDS = 30 * 60
# Jednotky, ktere musi byt active. Selhane jednotky se hlasi vsechny, i ty,
# ktere tu nejsou, protoze systemctl --failed je bere z celeho stroje.
UNITS = os.environ.get(
    "HEALTH_UNITS",
    "caddy.service news-web.service agenda-web.service planes-web.service "
    "lightning-web.service settings-web.service school-web.service "
    "satellites-web.service rain-web.service warnings-web.service "
    "alerts-web.service fleet-web.service watch-web.service radar-collector.service "
    "radar-web.service "
    "docker.service "
    "news.timer agenda.timer backup.timer watch.timer ou-watch.timer ha-update.timer",
).split()
# timer:hodiny - nejdelsi povolena doba od posledniho spusteni. news.timer
# jede 06:05 az 20:05, takze rano je legitimne deset hodin stary.
TIMERS = os.environ.get(
    "HEALTH_TIMERS",
    "news.timer:14 agenda.timer:1 backup.timer:26 watch.timer:1 ou-watch.timer:1 "
    "ha-update.timer:170",
).split()
HTTP_TIMEOUT_SECONDS = 10
USER_AGENT = "WaveshareHodiny-health/1.0"


def local(port: int, path: str) -> str:
    return f"http://127.0.0.1:{port}{path}"


def fetch(url: str) -> tuple[int, bytes]:
    request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    try:
        with urllib.request.urlopen(request, timeout=HTTP_TIMEOUT_SECONDS) as response:
            return response.status, response.read()
    except urllib.error.HTTPError as error:
        return error.code, error.read()


def fetch_json(url: str) -> dict:
    status, body = fetch(url)
    if status != 200:
        raise RuntimeError(f"HTTP {status}")
    return json.loads(body)


def age_of_iso(value: str) -> float:
    return time.time() - dt.datetime.fromisoformat(value).timestamp()


def systemctl(*args: str) -> str:
    result = subprocess.run(["systemctl", *args], capture_output=True, text=True, timeout=30)
    return result.stdout.strip()


# --- kontroly ------------------------------------------------------------------
# Kazda vraci None, kdyz je vse v poradku, jinak kratky popis pro push.

def check_news() -> str | None:
    status, body = fetch(local(8088, "/top.xml"))
    if status != 200:
        return f"/top.xml vraci {status}"
    match = re.search(rb"<lastBuildDate>([^<]+)</lastBuildDate>", body)
    if not match:
        return "kanal nema lastBuildDate"
    built = email.utils.parsedate_to_datetime(match.group(1).decode()).timestamp()
    hours = (time.time() - built) / 3600
    return f"kanal je stary {hours:.0f} h" if hours > 14 else None


def check_agenda() -> str | None:
    hours = age_of_iso(fetch_json(local(8089, "/agenda.json"))["generated"]) / 3600
    return f"agenda je stara {hours:.1f} h" if hours > 1 else None


def check_school() -> str | None:
    status, body = fetch(local(8094, "/school.json"))
    if status != 200:
        return f"/school.json vraci {status} (data starsi nez SCHOOL_MAX_AGE_HOURS)"
    problem = json.loads(body).get("problem") or ""
    return f"posledni stazeni selhalo: {problem}" if problem else None


def check_rain() -> str | None:
    data = fetch_json(local(8096, "/rain/status"))
    if data.get("failures", 0) >= 3:
        return f"{data['failures']} stazeni z CHMU za sebou selhalo"
    age = data.get("age")
    # Alerts se ptaji kazde 2,5 minuty, takze starsi nez hodina je zaseknuti.
    if age is not None and age > 3600:
        return f"predpoved je stara {age / 60:.0f} min"
    return None


def check_warnings() -> str | None:
    data = fetch_json(local(8097, "/warnings/status"))
    if data.get("failures", 0) >= 3:
        return f"{data['failures']} stazeni CAP za sebou selhalo"
    checked = data.get("checkedAge")
    if checked is not None and checked > 12 * 3600:
        return f"CAP se nekontroloval {checked / 3600:.0f} h"
    return None


def check_satellites() -> str | None:
    data = fetch_json(local(8095, "/satellites/status"))
    problems = []
    for name, group in data.items():
        if name == "sky":
            if not group.get("ready"):
                problems.append(f"obloha neni pripravena ({group.get('error') or 'bez chyby'})")
            continue
        if not group.get("wanted"):
            continue
        age = group.get("ageHours")
        if age is None or age > 24:
            problems.append(f"{name}: drahy stare {age} h {group.get('error', '')}".strip())
    return "; ".join(problems) or None


def check_alerts() -> str | None:
    data = fetch_json(local(8098, "/alerts/status"))
    if not any(c.get("enabled") for c in data.get("configs", {}).values()):
        return None
    if not data.get("topic"):
        return "chybi NTFY_TOPIC, upozorneni se neposilaji"
    age = time.time() - (data.get("lastPlanes") or 0)
    return f"smycka letadel stoji {age:.0f} s" if age > 120 else None


def check_http(port: int, path: str) -> str | None:
    status, _ = fetch(local(port, path))
    return None if status == 200 else f"{path} vraci {status}"


def check_cert() -> str | None:
    if not HOST:
        return None
    context = ssl.create_default_context()
    with socket.create_connection((HOST, 443), timeout=HTTP_TIMEOUT_SECONDS) as sock:
        with context.wrap_socket(sock, server_hostname=HOST) as tls:
            not_after = ssl.cert_time_to_seconds(tls.getpeercert()["notAfter"])
    days = (not_after - time.time()) / 86400
    return f"certifikat vyprsi za {days:.0f} dni" if days < CERT_MIN_DAYS else None


def check_disk() -> str | None:
    usage = shutil.disk_usage("/")
    percent = usage.used * 100 / usage.total
    return f"disk je plny z {percent:.0f} %" if percent > DISK_MAX_PERCENT else None


def unit_problems() -> dict[str, str]:
    problems = {}
    failed = systemctl("list-units", "--state=failed", "--no-legend", "--plain")
    for line in failed.splitlines():
        unit = line.split()[0]
        problems[f"failed:{unit}"] = f"{unit} je ve stavu failed"
    for unit in UNITS:
        state = systemctl("is-active", unit)
        if state != "active":
            problems[f"inactive:{unit}"] = f"{unit} je {state or 'neznamy'}"
    for item in TIMERS:
        unit, _, hours = item.partition(":")
        last = systemctl("show", unit, "-p", "LastTriggerUSec", "--value")
        # "n/a" = od startu stroje jeste nebezel. U news.timer po nocnim
        # restartu legitimni az do 06:05; neaktivni timer hlasi UNITS vys.
        if not last or last in ("n/a", "0"):
            continue
        # LastTriggerUSec je text ("Tue 2026-09-22 06:30:20 GMT"); systemd 239
        # nema --timestamp=unix, prevede se pres date.
        epoch = subprocess.run(["date", "-d", last, "+%s"], capture_output=True, text=True)
        if epoch.returncode != 0:
            continue
        age_hours = (time.time() - int(epoch.stdout)) / 3600
        if age_hours > float(hours):
            problems[f"timer:{unit}"] = f"{unit} naposledy pred {age_hours:.1f} h"
    return problems


CHECKS = {
    "zpravy": check_news,
    "agenda": check_agenda,
    "skola": check_school,
    "srazky": check_rain,
    "vystrahy": check_warnings,
    "druzice": check_satellites,
    "upozorneni": check_alerts,
    "zalohy-nastaveni": lambda: check_http(8092, "/settings/"),
    "hlidac-obchodu": lambda: check_http(8091, "/health"),
    "home-assistant": lambda: check_http(8123, "/"),
    "certifikat": check_cert,
    "disk": check_disk,
}


def run_checks() -> dict[str, str]:
    problems = unit_problems()
    for name, check in CHECKS.items():
        try:
            message = check()
        except Exception as error:  # noqa: BLE001 - kazda chyba je vysledek kontroly
            message = f"kontrola selhala: {error.__class__.__name__}: {error}"
        if message:
            problems[name] = message
    return problems


# --- push a stav -----------------------------------------------------------------

def push(title: str, message: str, tags: list[str], priority: int = 3) -> bool:
    if not NTFY_TOPIC:
        print("NTFY_TOPIC neni nastaveny, push se neposila", file=sys.stderr)
        return False
    body = {"topic": NTFY_TOPIC, "title": title, "message": message[:3500],
            "tags": tags, "priority": priority}
    headers = {"Content-Type": "application/json", "User-Agent": USER_AGENT}
    if NTFY_TOKEN:
        headers["Authorization"] = f"Bearer {NTFY_TOKEN}"
    request = urllib.request.Request(NTFY_URL, data=json.dumps(body).encode("utf-8"),
                                     headers=headers, method="POST")
    try:
        with urllib.request.urlopen(request, timeout=HTTP_TIMEOUT_SECONDS) as response:
            return 200 <= response.status < 300
    except (urllib.error.URLError, OSError) as error:
        print(f"push selhal: {error}", file=sys.stderr)
        return False


def load_state(name: str) -> dict:
    try:
        with open(os.path.join(STATE_DIR, name), encoding="utf-8") as handle:
            return json.load(handle)
    except (OSError, ValueError):
        return {}


def save_state(name: str, data: dict) -> None:
    os.makedirs(STATE_DIR, exist_ok=True)
    fd, tmp = tempfile.mkstemp(dir=STATE_DIR, prefix=".tmp-")
    with os.fdopen(fd, "w", encoding="utf-8") as handle:
        json.dump(data, handle, ensure_ascii=False, indent=1)
    os.replace(tmp, os.path.join(STATE_DIR, name))


def hourly() -> int:
    problems = run_checks()
    state = load_state("checks.json")
    streaks = state.get("streaks", {})
    reported = state.get("reported", {})

    streaks = {key: streaks.get(key, 0) + 1 for key in problems}
    confirmed = {key: problems[key] for key, count in streaks.items() if count >= CONFIRM_RUNS}
    new = {key: msg for key, msg in confirmed.items() if key not in reported}
    resolved = {key: msg for key, msg in reported.items() if key not in problems}

    sent_ok = True
    if new:
        lines = "\n".join(f"- {msg}" for msg in new.values())
        sent_ok = push(f"Server hodin: {len(new)} problem(u)", lines, ["warning"], 4)
    if resolved:
        lines = "\n".join(f"- {msg}" for msg in resolved.values())
        push("Server hodin: vyreseno", lines, ["white_check_mark"], 2)

    # Neodeslany push se neoznaci jako nahlaseny, aby odesel priste.
    if sent_ok:
        reported = {key: msg for key, msg in {**reported, **new}.items() if key in problems}
    else:
        reported = {key: msg for key, msg in reported.items() if key in problems}
    save_state("checks.json", {"time": int(time.time()), "streaks": streaks,
                               "reported": reported, "problems": problems})
    for key, msg in problems.items():
        print(f"{key}: {msg}")
    print(f"problemu: {len(problems)}, potvrzenych: {len(confirmed)}, "
          f"novych: {len(new)}, vyresenych: {len(resolved)}")
    return 0


def unit_failed(unit: str) -> int:
    state = load_state("units.json")
    last = state.get(unit, 0)
    if time.time() - last < UNIT_REPEAT_SECONDS:
        print(f"{unit}: nahlaseno pred {time.time() - last:.0f} s, znovu se neposila")
        return 0
    journal = subprocess.run(
        ["journalctl", "-u", unit, "-n", "12", "--no-pager", "-o", "cat"],
        capture_output=True, text=True, timeout=30,
    ).stdout.strip()
    if push(f"Selhalo: {unit}", journal or "(v zurnalu nic)", ["rotating_light"], 4):
        state[unit] = int(time.time())
        save_state("units.json", state)
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    group = parser.add_mutually_exclusive_group()
    group.add_argument("--unit", help="jednotka, ktera selhala (z notify-failure@)")
    group.add_argument("--print", action="store_true", help="jen vypsat vysledek kontrol")
    group.add_argument("--test-push", action="store_true", help="poslat zkusebni push")
    group.add_argument("--notify", nargs=2, metavar=("TITULEK", "TEXT"),
                       help="poslat libovolny push (napr. nova verze HA)")
    args = parser.parse_args()

    if args.notify:
        return 0 if push(args.notify[0], args.notify[1], ["package"], 3) else 1

    if args.unit:
        return unit_failed(args.unit)
    if args.test_push:
        ok = push("Server hodin: zkouska", "Hlaseni poruch funguje.", ["white_check_mark"], 2)
        return 0 if ok else 1
    if args.print:
        problems = run_checks()
        for key, msg in problems.items():
            print(f"{key}: {msg}")
        print("vse v poradku" if not problems else f"problemu: {len(problems)}")
        return 0 if not problems else 1
    return hourly()


if __name__ == "__main__":
    sys.exit(main())
