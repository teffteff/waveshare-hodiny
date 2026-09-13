"""Sklada odpoved /agenda.json z udalosti, ktere pripravil generate.py.

Generator uklada vsechny kalendare vcetne soukromych do jednoho snimku.
Odpoved se z nej sklada az pri dotazu, protoze kazde hodiny chteji jinou
podmnozinu: nektere kalendare si majitel ve webovem rozhrani schoval a
soukrome se pridaji jen tomu, kdo zna jejich heslo. Popisky dnu se pocitaji
taky az tady, takze DNES a ZITRA po pulnoci neukazuji na vcerejsek, dokud
generator znovu nedobehne.

Modul nema zadne zavislosti mimo standardni knihovnu, aby sel zkouset bez
klice servisniho uctu i bez site.
"""

from __future__ import annotations

import hashlib
import hmac
import os
from datetime import date, datetime, timedelta
from zoneinfo import ZoneInfo

TZ = ZoneInfo(os.environ.get("AGENDA_TZ", "Europe/Prague"))
# Strop pro hodiny. Displej ukaze jen tolik, kolik se vejde, ale nemuze to
# poznat, dokud nema aspon o udalost vic, nez nakresli.
MAX_ITEMS = int(os.environ.get("AGENDA_MAX_ITEMS", "30"))
# Prave probihajici udalost ma na displeji zustat. Bez tohohle okna by zmizela
# v okamziku, kdy zacala, coz je presne ta chvile, kdy je nejzajimavejsi.
GRACE_MINUTES = int(os.environ.get("AGENDA_GRACE_MINUTES", "60"))
# Hodiny si schovane kalendare pamatuji jako 32bitovou masku, takze vyssi
# index v ?hide= nemuze prijit od nich.
MAX_CALENDARS = 32

# Verzalkami, at popisek dne vypada stejne jako DNES a ZITRA. Font hodin ma
# velka pismena s diakritikou taky, takze se PA a CT napisou spravne.
WEEKDAYS = ["PO", "ÚT", "ST", "ČT", "PÁ", "SO", "NE"]

HASH_SCHEME = "pbkdf2_sha256"
HASH_ITERATIONS = 600_000


def parse_calendar_list(value: str, private: bool) -> list[dict]:
    """AGENDA_CALENDARS a AGENDA_PRIVATE_CALENDARS: carkou oddelene polozky
    "id" nebo "id|Jmeno". Jmeno prepise to, co o kalendari rika Google - hodi
    se u hlavniho kalendare uctu, ktery se v Googlu jmenuje e-mailovou adresou.
    """
    calendars = []
    for entry in value.split(","):
        entry = entry.strip()
        if not entry:
            continue
        calendar_id, _, name = entry.partition("|")
        calendars.append({"id": calendar_id.strip(), "name": name.strip(), "private": private})
    return calendars


def parse_hidden(value: str | None) -> set[int]:
    """Parametr ?hide=1,3 - indexy kalendaru, ktere hodiny nechteji. Neplatne
    kousky se tise zahodi: spatne poskladana adresa nesmi agendu shodit."""
    hidden = set()
    for part in (value or "").split(","):
        part = part.strip()
        if part.isdigit() and int(part) < MAX_CALENDARS:
            hidden.add(int(part))
    return hidden


def hash_key(key: str, salt: bytes | None = None, iterations: int = HASH_ITERATIONS) -> str:
    """Otisk hesla do AGENDA_PRIVATE_HASH. Oddelovac je dvojtecka, ne dolar:
    systemd v EnvironmentFile dolar sice nerozviji, ale shell pri rucnim
    source agenda.env ano."""
    salt = salt if salt is not None else os.urandom(16)
    digest = hashlib.pbkdf2_hmac("sha256", key.encode("utf-8"), salt, iterations)
    return f"{HASH_SCHEME}:{iterations}:{salt.hex()}:{digest.hex()}"


def key_matches(key: str, stored: str) -> bool:
    try:
        scheme, iterations, salt, digest = stored.split(":")
        if scheme != HASH_SCHEME:
            return False
        candidate = hashlib.pbkdf2_hmac(
            "sha256", key.encode("utf-8"), bytes.fromhex(salt), int(iterations)
        )
        return hmac.compare_digest(candidate.hex(), digest)
    except ValueError:
        return False


def day_label(day: date, today: date) -> str:
    if day == today:
        return "DNES"
    if day == today + timedelta(days=1):
        return "ZÍTRA"
    return f"{WEEKDAYS[day.weekday()]} {day.day}.{day.month}."


def _event_times(event: dict) -> tuple[datetime, datetime]:
    start = datetime.fromisoformat(event["start"])
    end = datetime.fromisoformat(event["end"])
    if start.tzinfo is None:
        start = start.replace(tzinfo=TZ)
    if end.tzinfo is None:
        end = end.replace(tzinfo=TZ)
    return start.astimezone(TZ), end.astimezone(TZ)


def render(snapshot: dict, hidden: set[int], unlocked: bool, now: datetime | None = None) -> dict:
    """Odpoved pro jedny hodiny.

    hidden jsou indexy, ktere majitel schoval; soukromy kalendar se navic
    ukaze jen s unlocked. Index kalendare zustava stejny bez ohledu na vyber,
    protoze podle nej hodiny barvi - schovany kalendar proto v poli
    "calendars" nechava prazdne jmeno misto toho, aby z nej zmizel.
    """
    now = (now or datetime.now(TZ)).astimezone(TZ)
    today = now.date()
    calendars = snapshot.get("calendars", [])
    shown = [
        index not in hidden and (unlocked or not calendar.get("private"))
        for index, calendar in enumerate(calendars)
    ]
    horizon = now - timedelta(minutes=GRACE_MINUTES)

    rows = []
    for event in snapshot.get("events", []):
        index = event.get("cal", -1)
        if not (0 <= index < len(shown)) or not shown[index]:
            continue
        start, end = _event_times(event)
        # Snimek je az ctvrt hodiny stary, takze uz skoncene udalosti se musi
        # odfiltrovat tady. Stejne pravidlo jako timeMin v dotazu na Google:
        # rozhoduje konec, ne zacatek, aby vicedenni udalost nezmizela.
        if end <= horizon:
            continue
        # Vicedenni udalost, ktera zacala driv, patri pod dnesek. Jinak by
        # nesla na zacatek seznamu s popiskem dne, ktery uz byl.
        day = max(start.date(), today)
        rows.append((day, not event.get("allday"), start, event))
    # Celodenni udalosti patri na zacatek sveho dne: nemaji cas, podle ktereho
    # by se daly zaradit mezi ostatni.
    rows.sort(key=lambda row: (row[0], row[1], row[2]))

    if len(rows) > MAX_ITEMS:
        # Hodiny poznaji, ze posledni zobrazeny den pokracuje, jen podle
        # dalsi udalosti v odpovedi. Useknuty den by proto vypadal kompletni -
        # radsi se zahodi cely, pokud pred nim nejaky zbyva.
        last_day = rows[MAX_ITEMS - 1][0]
        cut = rows[:MAX_ITEMS]
        if rows[MAX_ITEMS][0] == last_day:
            complete = [row for row in cut if row[0] != last_day]
            if complete:
                cut = complete
        rows = cut

    items = []
    previous_day = None
    for day, timed, start, event in rows:
        # Popisek dne nese jen prvni polozka toho dne. Hodiny podle neprazdneho
        # pole poznaji, kde zacit novou hlavicku, a nemusi porovnavat datumy.
        if day == previous_day:
            label, iso = "", ""
        else:
            label, iso = day_label(day, today), day.isoformat()
            previous_day = day
        # Cas ma smysl jen u udalosti, ktera zacina v den, pod kterym stoji.
        show_time = timed and start.date() == day
        items.append({
            "day": label,
            "date": iso,
            "time": f"{start:%H:%M}" if show_time else "",
            "title": event.get("title", ""),
            "cal": event["cal"],
        })

    return {
        "generated": snapshot.get("generated", ""),
        # Poradi odpovida indexu "cal" u udalosti, takze si podle nej displej
        # obarvi legendu i casy. Prazdne jmeno = kalendar v teto odpovedi neni.
        "calendars": [
            calendar.get("name", "") if visible else ""
            for calendar, visible in zip(calendars, shown)
        ],
        # Vsechny kalendare, ktere server zna, pro vyber ve webovem rozhrani
        # hodin. Soukromy tu je i bez hesla: jinak by ho nebylo jak zapnout.
        "available": [
            {"name": calendar.get("name", ""), "private": bool(calendar.get("private"))}
            for calendar in calendars
        ],
        "count": len(items),
        "items": items,
    }
