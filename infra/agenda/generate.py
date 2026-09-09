#!/usr/bin/env python3
"""Slozi agendu z Google Kalendare a zapise ji jako maly JSON pro hodiny.

Cte jeden nebo vic kalendaru pres servisni ucet (klic v AGENDA_KEY), slouci je
do jednoho seznamu serazeneho podle casu a ulozi atomicky do weboveho korene,
odkud ho serviruje agenda-web.service na portu 8089. Pri jakekoli chybe skript
skonci nenulove a ponecha predchozi soubor, aby na hodinach nezustal prazdny
seznam.

Vsechno, co jde spocitat tady, se pocita tady: casova zona, expanze opakovani,
skladani popisku dne i zkraceni titulku. Hodiny dostanou hotove retezce, takze
ve firmwaru nezustava zadna datumova aritmetika.
"""

from __future__ import annotations

import json
import os
import sys
from datetime import datetime, timedelta
from pathlib import Path
from urllib.parse import quote
from zoneinfo import ZoneInfo

from google.auth.transport.requests import AuthorizedSession
from google.oauth2 import service_account

SCOPES = ["https://www.googleapis.com/auth/calendar.readonly"]
KEY_FILE = Path(os.environ.get("AGENDA_KEY", "/opt/agenda/key.json"))
OUTPUT = Path(os.environ.get("AGENDA_OUTPUT", "/opt/agenda/www/agenda.json"))
TZ = ZoneInfo(os.environ.get("AGENDA_TZ", "Europe/Prague"))
# Kolik dni dopredu se ptat. Pri zhruba tech udalostech denne pokryje trojka
# vic, nez se na displej vejde, a delsi okno by jen zvetsovalo odpoved.
DAYS_AHEAD = int(os.environ.get("AGENDA_DAYS", "3"))
# Strop pro hodiny. Displej jich ukaze min, ale rezerva umozni menit pocet
# radku ve firmwaru bez zasahu do serveru.
MAX_ITEMS = int(os.environ.get("AGENDA_MAX_ITEMS", "12"))
# Delsi titulek utne LVGL trema teckami; usekem uz tady se setri prenos.
MAX_TITLE_CHARS = int(os.environ.get("AGENDA_MAX_TITLE", "48"))
# Jmeno kalendare do legendy na displeji. Delsi se utne; na kruh se stejne vejde
# jen par znaku vedle druheho jmena.
MAX_CALENDAR_NAME_CHARS = int(os.environ.get("AGENDA_MAX_CALENDAR_NAME", "20"))
# Prave probihajici udalost ma na displeji zustat. Bez tohohle okna by zmizela
# v okamziku, kdy zacala, coz je presne ta chvile, kdy je nejzajimavejsi.
GRACE_MINUTES = int(os.environ.get("AGENDA_GRACE_MINUTES", "60"))
# Poradi urcuje index, ktery hodiny pouziji jako barvu kalendare.
CALENDARS = [c.strip() for c in os.environ.get("AGENDA_CALENDARS", "").split(",") if c.strip()]

# Verzalkami, at popisek dne vypada stejne jako DNES a ZITRA. Font hodin ma
# velka pismena s diakritikou taky, takze se PA a CT napisou spravne.
WEEKDAYS = ["PO", "ÚT", "ST", "ČT", "PÁ", "SO", "NE"]


def session() -> AuthorizedSession:
    credentials = service_account.Credentials.from_service_account_file(
        str(KEY_FILE), scopes=SCOPES
    )
    return AuthorizedSession(credentials)


def calendar_name(http: AuthorizedSession, calendar_id: str) -> str:
    """Jmeno kalendare pro legendu. Bere se z Googlu, ne z konfigurace: jinak by
    se po prejmenovani kalendare musel editovat agenda.env."""
    response = http.get(
        f"https://www.googleapis.com/calendar/v3/calendars/{quote(calendar_id)}",
        timeout=30,
    )
    if response.status_code == 404:
        raise RuntimeError(f"kalendar {calendar_id} neni sdileny se servisnim uctem (404)")
    response.raise_for_status()
    summary = (response.json().get("summary") or "").strip()
    return summary[:MAX_CALENDAR_NAME_CHARS]


def fetch(http: AuthorizedSession, calendar_id: str, start: datetime, end: datetime) -> list[dict]:
    # singleEvents rozbali opakovani na serveru Googlu, takze RRULE nikdy
    # nedojde ani sem, ani do firmwaru. orderBy jde zapnout jen s nim.
    params = {
        "timeMin": start.isoformat(),
        "timeMax": end.isoformat(),
        "singleEvents": "true",
        "orderBy": "startTime",
        "maxResults": 100,
    }
    url = f"https://www.googleapis.com/calendar/v3/calendars/{quote(calendar_id)}/events"
    response = http.get(url, params=params, timeout=30)
    # Kalendar, ke kteremu ucet nema pristup, vraci 404, ne 403. Chyba tady
    # znamena spis odebrane sdileni nez preklep, at to hlaska rekne rovnou.
    if response.status_code == 404:
        raise RuntimeError(f"kalendar {calendar_id} neni sdileny se servisnim uctem (404)")
    response.raise_for_status()
    return response.json().get("items", [])


def starts_at(event: dict) -> tuple[datetime, bool]:
    start = event.get("start", {})
    if start.get("date"):
        # Celodenni udalost nema cas. Pripne se na pulnoc, aby sla radit, a
        # priznak allday urci, ze se misto casu vykresli pomlcka.
        return datetime.fromisoformat(start["date"]).replace(tzinfo=TZ), True
    return datetime.fromisoformat(start["dateTime"]).astimezone(TZ), False


def day_label(day, today) -> str:
    if day == today:
        return "DNES"
    if day == today + timedelta(days=1):
        return "ZÍTRA"
    return f"{WEEKDAYS[day.weekday()]} {day.day}.{day.month}."


def collect(http: AuthorizedSession) -> list[dict]:
    now = datetime.now(TZ)
    start = now - timedelta(minutes=GRACE_MINUTES)
    end = (now + timedelta(days=DAYS_AHEAD)).replace(hour=23, minute=59, second=59)
    rows = []
    for index, calendar_id in enumerate(CALENDARS):
        for event in fetch(http, calendar_id, start, end):
            # Odrieknuta instance opakovane udalosti prijde s status=cancelled
            # a bez tohohle by na displeji strasila dal.
            if event.get("status") == "cancelled":
                continue
            when, allday = starts_at(event)
            title = (event.get("summary") or "").strip()
            if not title:
                continue
            rows.append({"when": when, "allday": allday, "cal": index, "title": title})
    # Celodenni udalosti patri na zacatek sveho dne: nemaji cas, podle ktereho
    # by se daly zaradit mezi ostatni.
    rows.sort(key=lambda row: (row["when"].date(), not row["allday"], row["when"]))
    return rows


def render(rows: list[dict], names: list[str]) -> dict:
    now = datetime.now(TZ)
    today = now.date()
    items = []
    previous_day = None
    for row in rows[:MAX_ITEMS]:
        day = row["when"].date()
        # Popisek dne nese jen prvni polozka toho dne. Hodiny podle neprazdneho
        # pole poznaji, kde zacit novou hlavicku, a nemusi porovnavat datumy.
        if day == previous_day:
            label, iso = "", ""
        else:
            label, iso = day_label(day, today), day.isoformat()
            previous_day = day
        items.append({
            "day": label,
            "date": iso,
            "time": "" if row["allday"] else f"{row['when']:%H:%M}",
            "title": row["title"][:MAX_TITLE_CHARS],
            "cal": row["cal"],
        })
    return {
        "generated": now.isoformat(timespec="seconds"),
        # Poradi odpovida indexu "cal" u udalosti, takze si podle nej displej
        # obarvi legendu i casy.
        "calendars": names,
        "count": len(items),
        "items": items,
    }


def main() -> None:
    if not CALENDARS:
        sys.exit("AGENDA_CALENDARS je prazdne, neni co cist.")
    if not KEY_FILE.is_file():
        sys.exit(f"Klic servisniho uctu {KEY_FILE} neexistuje.")

    http = session()
    # Jmena se ctou driv nez udalosti: kalendar, ke kteremu ucet ztratil pristup,
    # tak spadne na prvni zadosti a necha lezet predchozi soubor cely.
    names = [calendar_name(http, calendar_id) for calendar_id in CALENDARS]
    payload = render(collect(http), names)

    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    temporary = OUTPUT.with_suffix(".tmp")
    # Bez fsync muze vypadek napajeni nechat na disku prazdny agenda.json:
    # prejmenovani je atomicke, ale zapis obsahu jeste nemusi byt na plotne.
    with open(temporary, "w", encoding="utf-8") as handle:
        json.dump(payload, handle, ensure_ascii=False, separators=(",", ":"))
        handle.flush()
        os.fsync(handle.fileno())
    os.replace(temporary, OUTPUT)
    print(f"Zapsano {payload['count']} udalosti do {OUTPUT}")


if __name__ == "__main__":
    main()
