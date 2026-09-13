#!/usr/bin/env python3
"""Stahne udalosti z Google Kalendare a ulozi je jako snimek pro serve.py.

Cte jeden nebo vic kalendaru pres servisni ucet (klic v AGENDA_KEY) a zapise
atomicky vsechny jejich udalosti do jednoho souboru. Hotovou odpoved pro hodiny
z nej sklada az serve.py pri kazdem dotazu (viz feed.py), protoze kazde hodiny
mohou chtit jiny vyber kalendaru a soukrome kalendare dostane jen ten, kdo zna
heslo. Pri jakekoli chybe skript skonci nenulove a ponecha predchozi soubor,
aby na hodinach nezustal prazdny seznam.

Snimek obsahuje i udalosti soukromych kalendaru, takze nesmi byt citelny
nikomu jinemu nez uctu, pod kterym bezi agenda: lezi v /opt/agenda s pravy 700.
"""

from __future__ import annotations

import json
import os
import sys
from datetime import datetime, timedelta
from pathlib import Path
from urllib.parse import quote

from google.auth.transport.requests import AuthorizedSession
from google.oauth2 import service_account

from feed import TZ, parse_calendar_list

SCOPES = ["https://www.googleapis.com/auth/calendar.readonly"]
KEY_FILE = Path(os.environ.get("AGENDA_KEY", "/opt/agenda/key.json"))
OUTPUT = Path(os.environ.get("AGENDA_OUTPUT", "/opt/agenda/www/events.json"))
# Kolik dni dopredu se ptat. Displej se plni, dokud je misto, takze tyden
# pokryje i klidne dny, kdy se do tri dnu vejde jen par udalosti.
DAYS_AHEAD = int(os.environ.get("AGENDA_DAYS", "7"))
# Delsi titulek utne LVGL trema teckami; usekem uz tady se setri prenos.
MAX_TITLE_CHARS = int(os.environ.get("AGENDA_MAX_TITLE", "48"))
# Jmeno kalendare do legendy na displeji. Delsi se utne; na kruh se stejne vejde
# jen par znaku vedle ostatnich jmen.
MAX_CALENDAR_NAME_CHARS = int(os.environ.get("AGENDA_MAX_CALENDAR_NAME", "20"))
# Stejne okno jako v feed.py: dotaz musi vratit i udalost, ktera prave bezi.
GRACE_MINUTES = int(os.environ.get("AGENDA_GRACE_MINUTES", "60"))
# Poradi urcuje index, ktery hodiny pouziji jako barvu kalendare. Soukrome
# kalendare jdou az za verejnymi, aby jejich pridani neprebarvilo ty stavajici.
CALENDARS = parse_calendar_list(os.environ.get("AGENDA_CALENDARS", ""), private=False) + \
    parse_calendar_list(os.environ.get("AGENDA_PRIVATE_CALENDARS", ""), private=True)


def session() -> AuthorizedSession:
    credentials = service_account.Credentials.from_service_account_file(
        str(KEY_FILE), scopes=SCOPES
    )
    return AuthorizedSession(credentials)


def calendar_name(http: AuthorizedSession, calendar: dict) -> str:
    """Jmeno kalendare pro legendu. Bere se z Googlu, ne z konfigurace: jinak by
    se po prejmenovani kalendare musel editovat agenda.env. Vyjimkou je jmeno
    zapsane v agenda.env za svislitkem."""
    calendar_id = calendar["id"]
    response = http.get(
        f"https://www.googleapis.com/calendar/v3/calendars/{quote(calendar_id)}",
        timeout=30,
    )
    if response.status_code == 404:
        raise RuntimeError(f"kalendar {calendar_id} neni sdileny se servisnim uctem (404)")
    response.raise_for_status()
    summary = calendar["name"] or (response.json().get("summary") or "").strip()
    return summary[:MAX_CALENDAR_NAME_CHARS]


def fetch(http: AuthorizedSession, calendar_id: str, start: datetime, end: datetime) -> list[dict]:
    # singleEvents rozbali opakovani na serveru Googlu, takze RRULE nikdy
    # nedojde ani sem, ani do firmwaru. orderBy jde zapnout jen s nim.
    params = {
        "timeMin": start.isoformat(),
        "timeMax": end.isoformat(),
        "singleEvents": "true",
        "orderBy": "startTime",
        "maxResults": 250,
    }
    url = f"https://www.googleapis.com/calendar/v3/calendars/{quote(calendar_id)}/events"
    response = http.get(url, params=params, timeout=30)
    # Kalendar, ke kteremu ucet nema pristup, vraci 404, ne 403. Chyba tady
    # znamena spis odebrane sdileni nez preklep, at to hlaska rekne rovnou.
    if response.status_code == 404:
        raise RuntimeError(f"kalendar {calendar_id} neni sdileny se servisnim uctem (404)")
    response.raise_for_status()
    return response.json().get("items", [])


def boundary(value: dict) -> tuple[str, bool]:
    # Celodenni udalost nema cas. Pripne se na pulnoc, aby sla radit, a
    # priznak allday urci, ze se misto casu vykresli pomlcka.
    if value.get("date"):
        return datetime.fromisoformat(value["date"]).replace(tzinfo=TZ).isoformat(), True
    return datetime.fromisoformat(value["dateTime"]).astimezone(TZ).isoformat(), False


def collect(http: AuthorizedSession) -> list[dict]:
    now = datetime.now(TZ)
    start = now - timedelta(minutes=GRACE_MINUTES)
    end = (now + timedelta(days=DAYS_AHEAD)).replace(hour=23, minute=59, second=59)
    events = []
    for index, calendar in enumerate(CALENDARS):
        for event in fetch(http, calendar["id"], start, end):
            # Odrieknuta instance opakovane udalosti prijde s status=cancelled
            # a bez tohohle by na displeji strasila dal.
            if event.get("status") == "cancelled":
                continue
            title = (event.get("summary") or "").strip()
            if not title:
                continue
            begins, allday = boundary(event.get("start", {}))
            ends, _ = boundary(event.get("end", event.get("start", {})))
            events.append({
                "start": begins,
                "end": ends,
                "allday": allday,
                "cal": index,
                "title": title[:MAX_TITLE_CHARS],
            })
    return events


def main() -> None:
    if not CALENDARS:
        sys.exit("AGENDA_CALENDARS je prazdne, neni co cist.")
    if not KEY_FILE.is_file():
        sys.exit(f"Klic servisniho uctu {KEY_FILE} neexistuje.")

    http = session()
    # Jmena se ctou driv nez udalosti: kalendar, ke kteremu ucet ztratil pristup,
    # tak spadne na prvni zadosti a necha lezet predchozi soubor cely.
    calendars = [
        {"name": calendar_name(http, calendar), "private": calendar["private"]}
        for calendar in CALENDARS
    ]
    snapshot = {
        "generated": datetime.now(TZ).isoformat(timespec="seconds"),
        "calendars": calendars,
        "events": collect(http),
    }

    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    temporary = OUTPUT.with_suffix(".tmp")
    # Bez fsync muze vypadek napajeni nechat na disku prazdny snimek:
    # prejmenovani je atomicke, ale zapis obsahu jeste nemusi byt na plotne.
    # Prava 600 uz pri zalozeni, protoze snimek nese soukrome kalendare.
    descriptor = os.open(temporary, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    with os.fdopen(descriptor, "w", encoding="utf-8") as handle:
        json.dump(snapshot, handle, ensure_ascii=False, separators=(",", ":"))
        handle.flush()
        os.fsync(handle.fileno())
    os.replace(temporary, OUTPUT)
    print(f"Zapsano {len(snapshot['events'])} udalosti z {len(calendars)} kalendaru do {OUTPUT}")


if __name__ == "__main__":
    main()
