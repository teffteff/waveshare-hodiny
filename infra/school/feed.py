"""Rozvrh a domaci ukoly ze Skoly OnLine v podobe, kterou hodiny jen opisou.

Skola OnLine nema verejne API. Mobilni aplikace ale mluvi s REST API na
/solapi/api (OAuth2 password grant, JSON), ktere zmapovaly neoficialni
projekty - dokumentace Libre-SkolaOnline/API-docs, integrace
elvisek2020/hacs-calendar_skolaonline pro Home Assistant a aplikace resol.
Je to podstatne pevnejsi zaklad nez skrabani ASP.NET stranek webove aplikace
(jako to dela skola-online-stahovani-znamek): JSON se s kazdym prebarvenim
webu nemeni.

Neoficialni API se ale zmenit muze, a zdroje se neshoduji ani v nazvech poli
(ukol ma podle jednoho "topic" a "dateTo", podle druheho "name" a "dateEnd").
Proto se u kazdeho pole zkousi vic variant a co se nepozna, se tise vynecha.
Hodiny tenhle tvar nikdy nevidi: dostanou jen hotove radky z render(), takze
zmena API znamena opravu tady, ne novy firmware.

Modul nema zavislosti mimo standardni knihovnu a nesaha na sit, aby sel
zkouset bez uctu ve Skole OnLine.
"""

from __future__ import annotations

import html
import os
import re
from datetime import date, datetime, timedelta
from html.parser import HTMLParser
from zoneinfo import ZoneInfo

TZ = ZoneInfo(os.environ.get("SCHOOL_TZ", "Europe/Prague"))
# Zakladni skola ma nejvys osm hodin denne, odpoledne s nultou devet. Strop je
# pojistka proti rozvrhu, ktery by vratil cely tyden pod jednim dnem.
MAX_LESSONS = int(os.environ.get("SCHOOL_MAX_LESSONS", "10"))
MAX_HOMEWORK = int(os.environ.get("SCHOOL_MAX_HOMEWORK", "12"))
# Ukol s terminem za mesic dnes nikoho nezajima a na kruhu by vytlacil ten
# na zitrek.
HOMEWORK_DAYS = int(os.environ.get("SCHOOL_HOMEWORK_DAYS", "14"))
# Po konci posledni hodiny se dnesek jeste chvili drzi, at rozvrh nezmizi
# presne ve chvili, kdy dite vychazi ze tridy. Pak se prepne na pristi
# skolni den - to je ten, na ktery se vecer bali taska.
DAY_GRACE_MINUTES = int(os.environ.get("SCHOOL_DAY_GRACE_MINUTES", "15"))
# Kolik skolnich dnu vedle sebe. Kruh pobere dva sloupce; tretimu by nezbylo
# misto ani na nazev predmetu.
DAY_COUNT = int(os.environ.get("SCHOOL_DAY_COUNT", "2"))
# Znaky, ne bajty. Radek na kruhu pobere kolem 25 znaku, predmet se ale na
# hodinach meri v pixelech a pripadne nahradi zkratkou; tady jde jen o to,
# aby do odpovedi nesel odstavec.
MAX_TITLE = int(os.environ.get("SCHOOL_MAX_TITLE", "60"))
MAX_SHORT = 24
# Druha stranka obrazovky Skola: neprectene zpravy a nove znamky. Starsi nez
# dva tydny uz nejsou "nove" - rodic je cte jinde a na hodinach by jen visely.
MESSAGE_DAYS = int(os.environ.get("SCHOOL_MESSAGE_DAYS", "14"))
MARK_DAYS = int(os.environ.get("SCHOOL_MARK_DAYS", "14"))
# Kolik radku posilat; celkovy pocet jde zvlast v messageCount a markCount.
MAX_MESSAGES = 6
MAX_MARKS = 8
MAX_MARK = 8
# Nastenka materske skoly (nasems.cz) pod znamkami. Oznameni starsi nez dva
# tydny uz na hodinach jen visi.
NOTICE_DAYS = int(os.environ.get("SCHOOL_NOTICE_DAYS", "14"))
MAX_NOTICES = 6
# Obedy pod rozvrhem: nejblizsi dva dny, kdy se vari, u kazdeho dne skolni
# jidelna a skolka. Obvykle to je dnesek a zitrek; v patek pondeli, a kdyz je
# pondeli svatek, utery. Prazdna sekce by v patek odpoledne rekla min nez
# jidelnicek na pondeli.
# Popisky stoji na displeji pred jidlem, takze musi byt kratke.
CANTEEN_LABEL = os.environ.get("SCHOOL_CANTEEN_LABEL", "ZŠ")
NASEMS_MENU_LABEL = os.environ.get("NASEMS_MENU_LABEL", "MŠ")
# Ktere jidlo z iCanteenu: jidelna vari dve, dite ma jedno.
CANTEEN_MEAL = os.environ.get("SCHOOL_CANTEEN_MEAL", "Oběd1")
MEAL_DAYS = 2
# Jak daleko se za temi dvema dny kouka. Tyden prekleni vikend i svatecni
# pondeli; delsi prazdniny jidelnicek stejne nema.
MEAL_HORIZON_DAYS = 7
# Tituly pred jmenem ("Mgr.", "PaedDr.") a za nim ("Ph.D.") koncici teckou.
_ACADEMIC_TITLE = re.compile(r"\S+\.$")

WEEKDAYS = ["PO", "ÚT", "ST", "ČT", "PÁ", "SO", "NE"]
WEEKDAY_NAMES = ["PONDĚLÍ", "ÚTERÝ", "STŘEDA", "ČTVRTEK", "PÁTEK", "SOBOTA", "NEDĚLE"]

# Stav hodiny pro hodiny. Cislo, ne text, aby se firmware nemusel ucit ceske
# nazvy typu hodin.
STATE_NORMAL = 0
# Suplovani, presun, skolni akce - hodina se kona, ale jinak nez obvykle.
STATE_CHANGED = 1
STATE_CANCELED = 2

# Typy hodin v hourType.id.
HOUR_TYPE_NORMAL = "ROZVRH"
# Puvodni hodina, za kterou nekdo zaskakuje. Chodi spolu s nahradou na stejnem
# case, takze by bez odfiltrovani byly v rozvrhu dve hodiny pres sebe.
HOUR_TYPE_SUBSTITUTED = "SUPLOVANA"
HOUR_TYPE_SUBSTITUTION = "SUPLOVANI"
CANCEL_KEYWORDS = ("odpad", "zrušen", "zrusen", "volno", "cancel")
DONE_KEYS = ("isDone", "done", "isCompleted", "completed", "isFinished", "finished")

_TAG = re.compile(r"<[^>]+>")
_BLOCK_TAG = re.compile(r"<\s*(br|/p|/div|/li)\b[^>]*>", re.IGNORECASE)


# --- pomocnici nad volnym JSONem ---------------------------------------------

def _text(source, *keys: str) -> str:
    """Prvni neprazdny retezec z dictu podle klicu, se sloucenymi mezerami."""
    if not isinstance(source, dict):
        return ""
    for key in keys:
        value = source.get(key)
        if isinstance(value, str) and value.strip():
            return " ".join(value.split())
    return ""


def _list_texts(source, key: str, *fields: str) -> list[str]:
    items = source.get(key) if isinstance(source, dict) else None
    if not isinstance(items, list):
        return []
    return [text for item in items if (text := _text(item, *fields))]


def _parse_datetime(value) -> datetime | None:
    """Casy chodi bez zony ("2026-09-07T08:00:00") a mysli se mistne."""
    if not isinstance(value, str) or not value.strip():
        return None
    try:
        parsed = datetime.fromisoformat(value.strip().replace("Z", "+00:00"))
    except ValueError:
        return None
    if parsed.tzinfo is not None:
        parsed = parsed.astimezone(TZ).replace(tzinfo=None)
    return parsed


def _parse_date(value) -> date | None:
    parsed = _parse_datetime(value)
    if parsed is not None:
        return parsed.date()
    if isinstance(value, str):
        try:
            return date.fromisoformat(value.strip()[:10])
        except ValueError:
            return None
    return None


def plain_text(value: str, limit: int = MAX_TITLE) -> str:
    """Z HTML popisu ukolu udela jeden radek. Zkracuje na hranici slova
    a pripoji vypustku; hodiny si ji v pismu prelozi na tri tecky."""
    if not isinstance(value, str):
        return ""
    text = _BLOCK_TAG.sub(" ", value)
    text = html.unescape(_TAG.sub("", text))
    text = " ".join(text.split())
    if len(text) <= limit:
        return text
    cut = text[: limit - 1]
    space = cut.rfind(" ")
    if space >= limit // 2:
        cut = cut[:space]
    return cut.rstrip(" ,.;:-") + "…"


def day_label(day: date, today: date) -> str:
    if day == today:
        return "DNES"
    if day == today + timedelta(days=1):
        return "ZÍTRA"
    if day == today - timedelta(days=1):
        return "VČERA"
    return f"{WEEKDAYS[day.weekday()]} {day.day}.{day.month}."


# --- /v1/user -----------------------------------------------------------------

def students_from_user(user) -> list[dict]:
    """Deti, jejichz rozvrh ucet vidi.

    Rodicovsky ucet ma userType "parent" a deti v "children"; jeho vlastni
    personID zadny rozvrh nema a /v1/timeTable by na nej odpovedel 403.
    Zakovsky ucet popisuje sam sebe.
    """
    if not isinstance(user, dict):
        return []
    students = []
    for child in user.get("children") or []:
        if isinstance(child, dict) and child.get("id"):
            students.append({
                "id": str(child["id"]),
                "name": _text(child, "firstName", "displayName", "fullName") or str(child["id"]),
                "fullName": _text(child, "displayName", "fullName"),
                "class": _text(child, "className"),
            })
    if students or user.get("userType") == "parent":
        return students
    person = user.get("personID") or user.get("personId")
    if not person:
        return []
    full = _text(user, "fullName", "displayName")
    return [{
        "id": str(person),
        "name": _text(user, "firstName") or (full.split()[0] if full else str(person)),
        "fullName": full,
        "class": _text(user.get("class"), "abbrev", "name"),
    }]


def pick_student(students: list[dict], wanted: str) -> dict | None:
    """SCHOOL_STUDENT vybira dite podle ID nebo casti jmena. Prazdny vyber
    znamena prvni dite, coz u uctu s jednim ditetem je jedina moznost."""
    if not students:
        return None
    wanted = (wanted or "").strip().casefold()
    if not wanted:
        return students[0]
    for student in students:
        if student["id"].casefold() == wanted:
            return student
    for student in students:
        if wanted in f"{student['name']} {student['fullName']}".casefold():
            return student
    return None


# --- /v1/timeTable ----------------------------------------------------------

def _hour_type(schedule: dict) -> str:
    hour_type = schedule.get("hourType")
    value = hour_type.get("id") if isinstance(hour_type, dict) else None
    return value.strip().upper() if isinstance(value, str) else ""


def _lesson_note(schedule: dict, hour_type: str) -> str:
    """Kratky popis zmeny. U bezne hodiny je hourKind prazdny objekt."""
    if hour_type in ("", HOUR_TYPE_NORMAL):
        return _text(schedule.get("hourKind"), "name", "description")
    return (_text(schedule.get("hourKind"), "name", "description")
            or _text(schedule.get("hourType"), "description", "name"))


def _is_canceled(schedule: dict, hour_type: str, note: str) -> bool:
    if any(schedule.get(key) is True for key in ("isCanceled", "isCancelled", "canceled", "removed")):
        return True
    haystack = f"{hour_type} {note}".casefold()
    return any(keyword in haystack for keyword in CANCEL_KEYWORDS)


def normalize_timetable(payload) -> list[dict]:
    """Hodiny ze vsech dnu odpovedi, serazene podle zacatku.

    Kazda hodina nese jen to, co hodiny ukazou: poradi, cas, predmet (plny
    nazev i zkratku, displej si vybere podle sirky) a pripadnou zmenu.
    Ucitele ani ucebny se schvalne neposilaji - na kruhu na ne neni misto
    a u skolni akce server vypise cely sbor.
    """
    days = payload.get("days") if isinstance(payload, dict) else None
    lessons = []
    for day in days if isinstance(days, list) else []:
        if not isinstance(day, dict) or not isinstance(day.get("schedules"), list):
            continue
        day_date = _parse_date(day.get("date"))
        for schedule in day["schedules"]:
            if not isinstance(schedule, dict):
                continue
            begin = _parse_datetime(schedule.get("beginTime"))
            end = _parse_datetime(schedule.get("endTime"))
            if begin is None:
                continue
            # beginTime je obvykle cele datum a cas; kdyby prisel jen cas
            # s nesmyslnym dnem (1900-01-01), plati datum dne.
            if day_date is not None and begin.date() != day_date:
                begin = datetime.combine(day_date, begin.time())
                if end is not None:
                    end = datetime.combine(day_date, end.time())
            if end is None or end <= begin:
                end = begin + timedelta(minutes=45)
            hour_type = _hour_type(schedule)
            note = _lesson_note(schedule, hour_type)
            subject = schedule.get("subject")
            name = _text(subject, "name", "abbrev")
            abbrev = _text(subject, "abbrev")
            if not name:
                # Skolni akce a podobne bloky nemaji predmet; smysluplny kratky
                # nazev nesou v hourType, hourKind.description byva uredni odstavec.
                name = _text(schedule.get("hourType"), "description", "name") or note or "Hodina"
            canceled = _is_canceled(schedule, hour_type, note)
            if canceled:
                state = STATE_CANCELED
            elif hour_type not in ("", HOUR_TYPE_NORMAL) or note:
                state = STATE_CHANGED
            else:
                state = STATE_NORMAL
            captions = _list_texts(schedule, "detailHours", "name", "caption")
            hour = captions[0] if captions else _text(schedule, "hourCaption", "caption")
            # Nektere skoly cisluji hodiny "1", jine "1."; na hodinach ma
            # sloupec vypadat vsude stejne.
            if hour.isdigit():
                hour += "."
            lessons.append({
                "id": str(schedule.get("scheduledHourId") or ""),
                "type": hour_type,
                "start": begin.isoformat(timespec="minutes"),
                "end": end.isoformat(timespec="minutes"),
                "hour": hour,
                "subject": plain_text(name, MAX_TITLE),
                "abbrev": plain_text(abbrev, MAX_SHORT),
                "note": plain_text(note, MAX_SHORT) if state != STATE_NORMAL else "",
                "state": state,
            })

    # Suplovana hodina, za kterou prisla nahrada, se zahodi. Bez nahrady
    # zustane - to, ze ji nekdo suplovat ma, je samo o sobe zprava.
    substituted = {(lesson["start"], lesson["end"]) for lesson in lessons
                   if lesson["type"] == HOUR_TYPE_SUBSTITUTION}
    lessons = [lesson for lesson in lessons
               if not (lesson["type"] == HOUR_TYPE_SUBSTITUTED
                       and (lesson["start"], lesson["end"]) in substituted)]
    lessons.sort(key=lambda lesson: (lesson["start"], lesson["end"], lesson["subject"]))
    return lessons


# --- /v1/students/{id}/homeworks ----------------------------------------------

def normalize_homework(payload) -> list[dict]:
    """Nesplnene ukoly s terminem. Ukol bez terminu se na hodiny nedostane:
    neni podle ceho ho zaradit a "nekdy" z rana nikoho nezajima."""
    items = payload.get("homeworks") if isinstance(payload, dict) else payload
    homework = []
    for item in items if isinstance(items, list) else []:
        if not isinstance(item, dict):
            continue
        if any(item.get(key) is True for key in DONE_KEYS):
            continue
        due = _parse_date(item.get("dateEnd") or item.get("dateTo") or item.get("deadline")
                          or item.get("dueDate"))
        if due is None:
            continue
        title = plain_text(_text(item, "name", "topic", "title"))
        if not title:
            title = plain_text(_text(item, "content", "detailedDescription", "description"))
        if not title:
            continue
        subject = item.get("subject")
        homework.append({
            "id": str(item.get("id") or ""),
            "due": due.isoformat(),
            "subject": plain_text(_text(subject, "name", "abbrev") or _text(item, "subjectName"), MAX_TITLE),
            "abbrev": plain_text(_text(subject, "abbrev"), MAX_SHORT),
            "title": title,
        })
    homework.sort(key=lambda entry: (entry["due"], entry["subject"], entry["title"]))
    return homework


# --- /v1/messages/received ---------------------------------------------------

def sender_surname(name: str) -> str:
    """"Mgr. Jana Novakova" -> "Novakova". Na kruh se cele jmeno s titulem
    vedle titulku zpravy nevejde a prijmeni ucitele dite pozna."""
    words = [word for word in (name or "").split() if not _ACADEMIC_TITLE.fullmatch(word)]
    return words[-1].strip(",") if words else ""


def normalize_messages(payload) -> list[dict]:
    """Neprectene prijate zpravy, nejnovejsi prvni.

    Posila se jen odesilatel a titulek. Telo zpravy (HTML, casto o diteti)
    na nastennych hodinach nema co delat a server ho nikam dal nepousti.
    Za neprectenou se bere jen zprava s read == false; chybejici priznak
    znamena zmenu API a radsi nic nez falesne upozorneni.
    """
    items = payload.get("messages") if isinstance(payload, dict) else None
    messages = []
    for item in items if isinstance(items, list) else []:
        if not isinstance(item, dict) or item.get("read") is not False:
            continue
        sent = _parse_datetime(item.get("sentDate"))
        if sent is None:
            continue
        messages.append({
            "id": str(item.get("id") or ""),
            "sent": sent.isoformat(timespec="minutes"),
            "sender": plain_text(sender_surname(_text(item.get("sender"), "name")), MAX_SHORT),
            "title": plain_text(_text(item, "title", "subject")) or "Zpráva",
        })
    messages.sort(key=lambda message: message["sent"], reverse=True)
    return messages


# --- /v1/students/{id}/marks/list ---------------------------------------------

def normalize_marks(payload) -> list[dict]:
    """Znamky s datem, nejnovejsi prvni. Predmet se dohleda v ciselniku
    "subjects" z te same odpovedi; znamka sama nese jen jeho ID."""
    if not isinstance(payload, dict):
        return []
    subjects = {str(subject.get("id")): subject for subject in payload.get("subjects") or []
                if isinstance(subject, dict)}
    marks = []
    for item in payload.get("marks") or []:
        if not isinstance(item, dict):
            continue
        day = _parse_date(item.get("markDate"))
        mark = _text(item, "markText")
        if day is None or not mark:
            continue
        subject = subjects.get(str(item.get("subjectId")), {})
        marks.append({
            "id": str(item.get("id") or ""),
            "date": day.isoformat(),
            "subject": plain_text(_text(subject, "name"), MAX_TITLE),
            "abbrev": plain_text(_text(subject, "abbrev"), MAX_SHORT),
            "mark": plain_text(mark, MAX_MARK),
            "theme": plain_text(_text(item, "theme")),
        })
    marks.sort(key=lambda entry: (entry["date"], entry["id"]), reverse=True)
    return marks


# --- nastenka nasems.cz ----------------------------------------------------------

# Osloveni na zacatku oznameni ("Vazeni rodice,") nic nerika a na radku by
# zabralo presne to misto, kam se vejde zacatek sdeleni.
_GREETING = re.compile(r"^(vážení|milí|dobrý den|dobrý večer|ahoj)\b[^,.!\n]{0,30}[,.!]?\s*",
                       re.IGNORECASE)


class _NoticeParser(HTMLParser):
    """Nastenka je serverem skladane HTML bez API. Kazde oznameni je
    <div class='podnadpis'> s titulkem (left) a casem (right), za nim
    <div class='nastenka_obsah'> s textem v <div class='section'>."""

    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.notices: list[dict] = []
        # Otevrene divy: (tridy, pole oznameni, do ktereho jde jejich text).
        self._stack: list[tuple[list[str], str | None]] = []

    def _inside(self, name: str) -> bool:
        return any(name in classes for classes, _ in self._stack)

    def handle_starttag(self, tag, attrs):
        if tag == "br":
            self.handle_data(" ")
        if tag != "div":
            return
        classes = (dict(attrs).get("class") or "").split()
        field = None
        if "podnadpis" in classes:
            self.notices.append({"title": "", "posted": "", "text": ""})
        elif self.notices and self._inside("podnadpis"):
            field = "title" if "left" in classes else "posted" if "right" in classes else None
        elif self.notices and "section" in classes and self._inside("nastenka_obsah"):
            field = "text"
        self._stack.append((classes, field))

    def handle_endtag(self, tag):
        if tag == "div" and self._stack:
            self._stack.pop()

    def handle_data(self, data):
        field = next((field for _, field in reversed(self._stack) if field), None)
        if field and self.notices:
            self.notices[-1][field] += data


def is_nasems_login_page(page: str) -> bool:
    """Neprihlasenemu nastenka vrati 200 s prihlasovacim formularem."""
    return bool(re.search(r"name=['\"]password['\"]", page or ""))


def notice_text(text: str) -> str:
    """Zacatek sdeleni bez osloveni, na jeden radek."""
    text = " ".join((text or "").split())
    return _GREETING.sub("", text, count=1)


def normalize_notices(page: str) -> list[dict]:
    """Oznameni z nastenky s casem, nejnovejsi prvni. Hodiny dostanou titulek
    a zacatek textu, radek si zkrati samy podle sirky pisma."""
    parser = _NoticeParser()
    parser.feed(page or "")
    parser.close()
    notices = []
    for item in parser.notices:
        title = plain_text(item["title"])
        match = re.search(r"(\d{1,2})\.\s*(\d{1,2})\.\s*(\d{4})(?:\s+(\d{1,2}):(\d{2}))?",
                          item["posted"])
        if not title or match is None:
            continue
        day, month, year, hour, minute = match.groups()
        try:
            posted = datetime(int(year), int(month), int(day), int(hour or 0), int(minute or 0))
        except ValueError:
            continue
        notices.append({
            "posted": posted.isoformat(timespec="minutes"),
            "title": title,
            "text": plain_text(notice_text(item["text"])),
        })
    notices.sort(key=lambda notice: notice["posted"], reverse=True)
    return notices


# --- jidelnicky ------------------------------------------------------------------

# Napoje jidelnicky pisou mezi jidla ("..., těstoviny, ovocný čaj, voda").
# Na displeji by jen zabraly misto, kam se vejde zbytek jidla. Polozka se
# zahodi jen cela: "rýže na mléce" je jidlo, "mléko" napoj.
_DRINK = re.compile(
    r"^(?:[\w-]+(?:\.\s*|\s+)){0,2}(?:čaj|voda|mléko|kakao|káva|šťáva|džus|sirup|"
    r"nápoj|limonáda|mošt|melta)(?:\s+s\s+[\w.]+)?$", re.IGNORECASE)
# iCanteen pise alergeny za jidlo mezi lomitka: "parmezán/1.1, 3, 7/".
_CANTEEN_ALLERGENS = re.compile(r"/\s*\d[\d.,\s]*(?:/|$)")
# Cislo jidla pred hlavnim chodem: "polévka …/1- Bramborové špecle".
_CANTEEN_MAIN = re.compile(r"(?:^|\s)\d+\s*-\s*")
_MENU_DATE = re.compile(r"(\d{1,2})\.\s*(\d{1,2})\.\s*(\d{4})")
# Den bez obeda: jidelnicky misto jidla napisou, proc se nevari ("Státní
# svátek", "Ředitelské volno - zavřeno"). Radek "ZŠ: Státní svátek" by na
# displeji zabral misto dne, kdy se opravdu vari, takze se takovy den
# preskoci. Posuzuje se cely popis, ne jeho cast: "svíčková" ani "volské oko"
# tim propadnout nesmi.
_NO_MEAL_SPLIT = re.compile(r"[,;]|\s+[-–—]\s+")
_NO_MEAL = re.compile(
    r"(?:státní\s+)?svátek|prázdniny|(?:ředitelské\s+)?volno|zavřeno|"
    r"nevaří\s+se|dovolená|sanitární\s+den", re.IGNORECASE)


def meal_text(text: str) -> str:
    """Jidlo na jeden radek: bez alergenu a napoju, s carkami po cesku.
    Den, kdy se nevari, vraci prazdno - hodiny pak misto nej ukazou nejblizsi
    dalsi den s jidlem."""
    parts = [" ".join(part.split()) for part in (text or "").split(",")]
    kept = [part for part in parts if part and not _DRINK.match(part)]
    text = plain_text(", ".join(kept))
    pieces = [piece.strip() for piece in _NO_MEAL_SPLIT.split(text)]
    pieces = [piece for piece in pieces if piece]
    if pieces and all(_NO_MEAL.fullmatch(piece) for piece in pieces):
        return ""
    return text


def meal_weekdays(today: date, count: int = MEAL_DAYS) -> list[date]:
    """Dny, na ktere se shani jidelnicek: dnesek a dalsi vsedni dny, dokud
    jich neni `count`. V patek to je patek a pondeli, takze uz v patek musi
    byt stazeny i pristi tyden. Svatky se tady nehlidaji - o tom, ze se ten
    den nevari, rekne az jidelnicek."""
    days = []
    day = today
    while len(days) < count:
        if day.weekday() < 5:
            days.append(day)
        day += timedelta(days=1)
    return days


def _menu_date(text: str) -> date | None:
    match = _MENU_DATE.search(text or "")
    if match is None:
        return None
    day, month, year = (int(part) for part in match.groups())
    try:
        return date(year, month, day)
    except ValueError:
        return None


class _CanteenParser(HTMLParser):
    """iCanteen ukazuje jidelnicek na tydny dopredu i neprihlasenemu, primo na
    prihlasovaci strance. Den je <div class="jidelnicekDen"> s datem v id
    "day-2026-09-21", jidla jsou <div class="container"> s nazvem
    ("Oběd1") ve shrinkedColumn a popisem v column."""

    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.days: list[dict] = []
        self._stack: list[str | None] = []

    def handle_starttag(self, tag, attrs):
        if tag != "div":
            return
        attributes = dict(attrs)
        classes = (attributes.get("class") or "").split()
        field = None
        match = re.fullmatch(r"day-(\d{4}-\d{2}-\d{2})", attributes.get("id") or "")
        if match:
            self.days.append({"date": match.group(1), "meals": []})
        elif self.days and "container" in classes:
            self.days[-1]["meals"].append({"name": "", "text": ""})
        elif self.days and self.days[-1]["meals"] and "jidelnicekItem" in classes:
            field = "name" if "shrinkedColumn" in classes else "text"
        self._stack.append(field)

    def handle_endtag(self, tag):
        if tag == "div" and self._stack:
            self._stack.pop()

    def handle_data(self, data):
        field = next((field for field in reversed(self._stack) if field), None)
        if field and self.days and self.days[-1]["meals"]:
            self.days[-1]["meals"][-1][field] += data


def normalize_canteen(page: str, meal: str | None = None) -> list[dict]:
    """Hlavni chod jednoho jidla (CANTEEN_MEAL) po dnech, bez polevky,
    alergenu a napoju."""
    wanted = "".join((meal or CANTEEN_MEAL).split()).lower()
    parser = _CanteenParser()
    parser.feed(page or "")
    parser.close()
    meals = []
    for day in parser.days:
        for item in day["meals"]:
            if "".join(item["name"].split()).lower() != wanted:
                continue
            text = _CANTEEN_ALLERGENS.sub(" ", " ".join(item["text"].split()))
            # Polevka stoji pred cislem jidla. Kdyby cislo chybelo, zustane
            # cely text: radsi polevka navic nez prazdny radek.
            parts = _CANTEEN_MAIN.split(text, maxsplit=1)
            text = meal_text(parts[1] if len(parts) == 2 else text)
            if text:
                meals.append({"date": day["date"], "text": text})
            break
    return meals


class _NasemsMenuParser(HTMLParser):
    """Jidelnicek skolky: <div class='podnadpis'> s dnem ("Pondělí -
    14.9.2026") v left_side, pod nim tabulka: nazev chodu ve <span>
    ("Hlavní chod:"), popis v <div class='bold'>, alergeny v
    <div class='alergeny'>."""

    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.days: list[dict] = []
        self._stack: list[tuple[str, str | None]] = []

    def handle_starttag(self, tag, attrs):
        classes = (dict(attrs).get("class") or "").split()
        field = None
        if tag == "div" and "podnadpis" in classes:
            self.days.append({"day": "", "courses": []})
        elif self.days and tag == "div" and "left_side" in classes and not self.days[-1]["courses"]:
            field = "day"
        elif self.days and tag == "tr":
            self.days[-1]["courses"].append({"name": "", "text": ""})
        elif self.days and self.days[-1]["courses"] and tag == "span" and "coloured" in classes:
            field = "name"
        elif self.days and self.days[-1]["courses"] and tag == "div" and "bold" in classes:
            field = "text"
        if tag in ("div", "span", "tr", "td", "table"):
            self._stack.append((tag, field))

    def handle_endtag(self, tag):
        # Uzavre posledni otevreny prvek stejneho jmena; HTML nasems.cz je
        # uzavrene poradne, ale spatny konec nesmi rozbit zbytek stranky.
        for index in range(len(self._stack) - 1, -1, -1):
            if self._stack[index][0] == tag:
                del self._stack[index:]
                break

    def handle_data(self, data):
        field = next((field for _, field in reversed(self._stack) if field), None)
        if field is None or not self.days:
            return
        day = self.days[-1]
        if field == "day":
            day["day"] += data
        elif day["courses"]:
            day["courses"][-1][field] += data


def normalize_nasems_menu(page: str) -> list[dict]:
    """Hlavni chod skolky po dnech, bez alergenu a napoju."""
    parser = _NasemsMenuParser()
    parser.feed(page or "")
    parser.close()
    meals = []
    for day in parser.days:
        when = _menu_date(day["day"])
        if when is None:
            continue
        for course in day["courses"]:
            name = " ".join(course["name"].split()).rstrip(":").lower()
            if name != "hlavní chod":
                continue
            text = meal_text(course["text"])
            if text:
                meals.append({"date": when.isoformat(), "text": text})
            break
    return meals


def nasems_next_week(page: str) -> str:
    """Hodnota tlacitka "Následující týden" (unixovy cas zacatku tydne),
    nebo prazdny retezec. Pristi tyden se nacita AJAXem pres tuto hodnotu.
    Tlacitko zpet ma pred popiskem ikonu, takze se s timhle nesplete."""
    match = re.search(r"data-polozka='(\d+)'>\s*<div class='button_description'>\s*Následující",
                      page or "")
    return match.group(1) if match else ""


# --- odpoved pro hodiny ------------------------------------------------------

def pick_day(lessons: list[dict], now: datetime) -> date | None:
    """Den, jehoz rozvrh se ukaze: dnesek, dokud neskoncila posledni hodina
    (a kratka rezerva), jinak nejblizsi dalsi den, kdy se uci. O vikendu a
    o prazdninach tak hodiny ukazou pondeli, respektive prvni skolni den.

    Odpadle hodiny se do rozhodovani nepocitaji: den, kdy vsechno odpada,
    se ukaze, ale neprodluzuje se jimi.
    """
    today = now.date()
    local_now = now.astimezone(TZ).replace(tzinfo=None)
    days: dict[date, datetime] = {}
    for lesson in lessons:
        start = datetime.fromisoformat(lesson["start"])
        end = datetime.fromisoformat(lesson["end"])
        day = start.date()
        if lesson["state"] == STATE_CANCELED and day in days:
            continue
        days[day] = max(days.get(day, end), end)
    todays_end = days.get(today)
    if todays_end is not None and local_now < todays_end + timedelta(minutes=DAY_GRACE_MINUTES):
        return today
    later = sorted(day for day in days if day > today)
    return later[0] if later else None


def school_days(lessons: list[dict], first: date | None, count: int) -> list[date]:
    """Den z pick_day() a za nim dalsi dny, kdy se uci. Vikend a volno se
    preskakuji: v patek odpoledne tak hodiny ukazou pondeli a utery."""
    if first is None:
        return []
    later = sorted({datetime.fromisoformat(lesson["start"]).date() for lesson in lessons}
                   - {first})
    return [first] + [day for day in later if day > first][: max(count - 1, 0)]


def render(snapshot: dict, now: datetime | None = None) -> dict:
    """Odpoved /school.json. Sklada se az pri dotazu, protoze o tom, ktery den
    je "dnes" a jestli uz skoncilo vyucovani, rozhoduje cas dotazu, ne cas
    posledniho stazeni."""
    now = (now or datetime.now(TZ)).astimezone(TZ)
    today = now.date()
    lessons = snapshot.get("lessons", [])
    student = snapshot.get("student") or {}

    days = []
    for day in school_days(lessons, pick_day(lessons, now), DAY_COUNT):
        rows = []
        end = None
        for lesson in lessons:
            start = datetime.fromisoformat(lesson["start"])
            if start.date() != day:
                continue
            lesson_end = datetime.fromisoformat(lesson["end"])
            # Konec vyucovani je konec posledni hodiny, ktera se opravdu kona.
            if lesson.get("state") != STATE_CANCELED:
                end = max(end or lesson_end, lesson_end)
            rows.append({
                "hour": lesson.get("hour", ""),
                "start": f"{start:%H:%M}",
                "end": f"{lesson_end:%H:%M}",
                "subject": lesson.get("subject", ""),
                "abbrev": lesson.get("abbrev", ""),
                "note": lesson.get("note", ""),
                "state": lesson.get("state", STATE_NORMAL),
            })
        days.append({
            # Popisek dne ve stejnem tvaru jako u agendy; weekday jen u DNES
            # a ZITRA, kde samotny popisek den v tydnu nerika.
            "day": day_label(day, today),
            "weekday": WEEKDAY_NAMES[day.weekday()] if day <= today + timedelta(days=1) else "",
            "today": day == today,
            "end": f"{end:%H:%M}" if end else "",
            "lessons": rows[:MAX_LESSONS],
        })

    # Zkratka predmetu z rozvrhu, protoze ukol casto nese jen plny nazev.
    abbrevs = {lesson["subject"]: lesson["abbrev"] for lesson in lessons
               if lesson.get("abbrev") and lesson.get("subject")}
    horizon = today + timedelta(days=HOMEWORK_DAYS)
    homework = []
    for item in snapshot.get("homework", []):
        due = date.fromisoformat(item["due"])
        if not today <= due <= horizon:
            continue
        homework.append({
            "due": day_label(due, today),
            "subject": item.get("subject", ""),
            "abbrev": item.get("abbrev") or abbrevs.get(item.get("subject", ""), ""),
            "title": item.get("title", ""),
        })
        if len(homework) >= MAX_HOMEWORK:
            break

    body = {
        "generated": snapshot.get("generated", ""),
        "student": student.get("name", ""),
        # Prazdne pole o prazdninach: v dohledu neni den, kdy se uci.
        "days": days,
        "homework": homework,
        # Posledni stazeni selhalo, ale starsi data jsou porad lepsi nez prazdno
        # (nejdyl serve.MAX_AGE_HOURS). Firmware pole necte, hlida ho
        # tools/check-stack.sh.
        "problem": snapshot.get("problem", ""),
    }
    # Zpravy a znamky jen tehdy, kdyz je server stahuje. Bez klice hodiny
    # druhou stranku vubec nenabidnou; prazdne pole znamena "nic noveho".
    if "messages" in snapshot:
        horizon = today - timedelta(days=MESSAGE_DAYS)
        recent = [message for message in snapshot["messages"]
                  if datetime.fromisoformat(message["sent"]).date() >= horizon]
        body["messageCount"] = len(recent)
        body["messages"] = [{
            "when": day_label(datetime.fromisoformat(message["sent"]).date(), today),
            "sender": message.get("sender", ""),
            "title": message.get("title", ""),
        } for message in recent[:MAX_MESSAGES]]
    if "marks" in snapshot:
        horizon = today - timedelta(days=MARK_DAYS)
        recent = [mark for mark in snapshot["marks"] if date.fromisoformat(mark["date"]) >= horizon]
        body["markCount"] = len(recent)
        body["marks"] = [{
            "when": day_label(date.fromisoformat(mark["date"]), today),
            "subject": mark.get("subject", ""),
            "abbrev": mark.get("abbrev", ""),
            "mark": mark.get("mark", ""),
            "theme": mark.get("theme", ""),
        } for mark in recent[:MAX_MARKS]]
    # Server s vypnutymi ukoly klic neposila: hodiny pak nepisou ani
    # "ŽÁDNÉ ÚKOLY", ktere by o ukolech nic nerikalo.
    if snapshot.get("homeworkEnabled") is False:
        del body["homework"]
    # Obedy na nejblizsi dva dny, kdy se vari: obvykle dnes a zitra, v patek
    # patek a pondeli. U kazdeho dne jidelna a skolka; hodiny z prazdneho pole
    # nic nekresli.
    # "today" rika, ktera jidla patri dnesku: hodiny je odpoledne schovaji,
    # kdyz je v nastaveni hodina prepnuti na zitrek. Rozhodnout to tady nejde,
    # odpoved je spolecna pro vsechny hodiny a kazde muze byt nastavena jinak.
    menus = [(label, snapshot[key]) for key, label in
             (("canteen", CANTEEN_LABEL), ("kindermenu", NASEMS_MENU_LABEL)) if key in snapshot]
    if menus:
        meals = []
        days = 0
        for offset in range(MEAL_HORIZON_DAYS):
            if days >= MEAL_DAYS:
                break
            day = today + timedelta(days=offset)
            rows = []
            for label, items in menus:
                item = next((item for item in items if item.get("date") == day.isoformat()), None)
                if item and item.get("text"):
                    rows.append({"when": day_label(day, today), "today": day == today,
                                 "who": label, "text": item["text"]})
            # Den bez jidla (vikend, svatek, prazdniny) se nepocita: misto nej
            # se kouka o den dal, at v patek odpoledne sviti pondeli.
            if not rows:
                continue
            meals += rows
            days += 1
        body["meals"] = meals
    if "notices" in snapshot:
        horizon = today - timedelta(days=NOTICE_DAYS)
        recent = [notice for notice in snapshot["notices"]
                  if datetime.fromisoformat(notice["posted"]).date() >= horizon]
        body["noticeCount"] = len(recent)
        body["notices"] = [{
            "when": day_label(datetime.fromisoformat(notice["posted"]).date(), today),
            "title": notice.get("title", ""),
            "text": notice.get("text", ""),
        } for notice in recent[:MAX_NOTICES]]
    return body
