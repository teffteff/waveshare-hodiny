#!/usr/bin/env python3
"""Vybere nejdůležitější zprávy dne a zapíše je jako RSS 2.0 pro hodiny.

Používá Google Gemini se strukturovaným výstupem (JSON schema), takže model vrací
přímo pole položek, ne volný text. Soubor se ukládá atomicky do webového kořene,
odkud ho servíruje news-web.service na portu 8088. Při jakékoli chybě skript
skončí nenulově a ponechá předchozí soubor, aby na hodinách nezůstal prázdný
seznam.
"""

from __future__ import annotations

import html
import os
import socket
import sys
import time
from datetime import datetime, timezone
from email.utils import format_datetime
from pathlib import Path

import feedparser
from google import genai
from google.genai import types
from google.genai import errors as genai_errors
from pydantic import BaseModel, Field

FEEDS = [
    "https://www.irozhlas.cz/rss/irozhlas",
    "https://ct24.ceskatelevize.cz/rss/hlavni-zpravy",
    # Mezinárodní zdroj. Reuters už veřejné RSS nemá, BBC World je spolehlivý.
    "https://feeds.bbci.co.uk/news/world/rss.xml",
]
CANDIDATES_PER_FEED = 15
# feedparser si vlastní timeout nenastavuje a news.service je Type=oneshot,
# kde systemd TimeoutStartSec ve výchozím stavu vypíná. Bez tohohle by jeden
# zaseknutý zdroj zablokoval jednotku napořád a timer by další běhy přeskakoval,
# takže by kanál tiše přestal být čerstvý.
FEED_TIMEOUT_SECONDS = 20
socket.setdefaulttimeout(FEED_TIMEOUT_SECONDS)
# Strop displeje: ClockConfig.h dovolí 3 až 6 zpráv (CLOCK_RSS_MIN_ITEMS až
# CLOCK_RSS_MAX_ITEMS), výchozí je 5, a hodiny si z kanálu vezmou jen prvních
# tolik, kolik mají nastaveno. Psát jich do kanálu víc nemá smysl — stáhly by
# je a zahodily. Pozor na RSS_MAX_ITEMS = 8 v RssParser.h: to je jen rezerva
# v bufferu parseru, aby nezávisel na ClockConfig, ne počet, který jde zobrazit.
PUBLISHED_MAX = 6
# Od modelu se jich chce víc, než se vydá. Kontrola v main() zahazuje neplatné
# a zdvojené indexy, takže bez rezervy by jedna vadná volba stlačila výsledek
# pod šestku. Dřív dělalo jedno číslo obojí, takže „chtít víc zpráv“ zároveň
# znamenalo „častěji to celé vzdát“.
REQUESTED = 8
# Práh k vydání. Pod ním zůstane ležet předchozí soubor: jedna vadná volba nesmí
# shodit celý běh, ale ani se nesmí vydat skoro prázdný kanál.
MINIMUM_TO_PUBLISH = 5
# Starší zprávy se modelu vůbec nenabídnou. Kanál ČT24 drží položky i přes
# dvacet hodin, takže bez tohohle filtru soutěží včerejšek s tím, co vyšlo před
# chvílí. Okno je schválně široké: ranní běh jde po noci, kdy toho vyšlo málo,
# a užší okno by ho shodilo pod práh k vydání. Hlavní užitek je proto stáří
# vypsané modelu (viz choose) a odstavení zdroje, který přestal vydávat.
MAX_AGE_HOURS = float(os.environ.get("NEWS_MAX_AGE_HOURS", "24"))
# Hodiny kreslí u tří až pěti zpráv tři řádky, u šesti dva; do dvou řádků se
# vejde kolem sta znaků, delší titulek utne LVGL třemi tečkami.
MAX_TITLE_CHARS = 90
MODEL = os.environ.get("NEWS_MODEL", "gemini-flash-latest")
# Když je hlavní model přetížený (503), zkusí se po řadě další. Aliasy „latest“
# se nezastarají, pinovaný 3.6 je záloha.
FALLBACK_MODELS = [
    m.strip()
    for m in os.environ.get(
        "NEWS_FALLBACK_MODELS",
        "gemini-flash-lite-latest,gemini-3.6-flash",
    ).split(",")
    if m.strip()
]
OUTPUT = Path(os.environ.get("NEWS_OUTPUT", "/opt/news/www/top.xml"))
# Jen do <link> kanálu. Hodiny ho nečtou, ale konkrétní adresa serveru nepatří
# do veřejného repozitáře, takže se bere z prostředí (news.env).
CHANNEL_LINK = os.environ.get("NEWS_LINK", "http://localhost:8088/top.xml")


class Pick(BaseModel):
    index: int = Field(description="Index vybrané zprávy ve vstupním seznamu")
    headline: str = Field(description=f"Úderný český titulek, max {MAX_TITLE_CHARS} znaků")


def published_at(entry) -> datetime:
    parsed = entry.get("published_parsed") or entry.get("updated_parsed")
    if not parsed:
        return datetime.now(timezone.utc)
    return datetime(*parsed[:6], tzinfo=timezone.utc)


def collect() -> list[dict]:
    items: list[dict] = []
    seen: set[str] = set()
    now = datetime.now(timezone.utc)
    for url in FEEDS:
        for entry in feedparser.parse(url).entries[:CANDIDATES_PER_FEED]:
            title = (entry.get("title") or "").strip()
            key = title.lower()
            if not title or key in seen:
                continue
            published = published_at(entry)
            # Položka bez data dostane od published_at současný čas, takže
            # projde. Zamrzlý kanál se pozná právě podle dat a nedatovaná
            # položka je vzácná; zahazovat ji naslepo by ubralo víc než přidalo.
            if (now - published).total_seconds() > MAX_AGE_HOURS * 3600:
                continue
            # Až tady, ne dřív: kdyby se stejný titulek objevil ve dvou
            # kanálech a ten první byl přes okno, přišlo by se i o ten čerstvý.
            seen.add(key)
            items.append({
                "title": title,
                "summary": (entry.get("summary") or "").strip()[:300],
                "published": published,
            })
    return items


def describe_age(published: datetime, now: datetime) -> str:
    hours = (now - published).total_seconds() / 3600
    return "před <1 h" if hours < 1 else f"před {hours:.0f} h"


def choose(items: list[dict]) -> list[Pick]:
    # Stáří u každé položky, aby model poznal, co je z dneška a co doběhlo ze
    # včerejška. Pořadí zůstává po zdrojích, ne podle času: seřazeno od
    # nejnovějšího by nahoru vyplavaly zdroje, které publikují nejčastěji,
    # a model by kvůli pozici sklouzl k jednomu médiu.
    now = datetime.now(timezone.utc)
    listing = "\n".join(
        f"[{i}] ({describe_age(it['published'], now)}) {it['title']} — {it['summary']}"
        for i, it in enumerate(items)
    )
    api_key = os.environ.get("GEMINI_API_KEY") or os.environ.get("GOOGLE_API_KEY")
    client = genai.Client(api_key=api_key)
    config = types.GenerateContentConfig(
        system_instruction=(
            "Jsi editor zpravodajství pro malý displej pro čtenáře v Česku. "
            "Vybíráš nejdůležitější zprávy dne a mícháš klíčové domácí české "
            "zprávy s hlavními světovými událostmi; ať je ve výběru obojí. "
            "Vyhýbáš se bulváru, sportovním výsledkům a PR článkům. Některé "
            "titulky přicházejí anglicky (BBC); vždy piš výsledný titulek česky."
        ),
        response_mime_type="application/json",
        response_schema=list[Pick],
        temperature=0.3,
        max_output_tokens=8192,
    )
    prompt = (
        f"Vyber {REQUESTED} nejdůležitějších zpráv dne a ke každé napiš vlastní "
        f"úderný titulek v češtině, nejvýše {MAX_TITLE_CHARS} znaků, bez uvozovek "
        f"a bez názvu média. V závorce je u každé položky stáří; při srovnatelné "
        f"důležitosti dej přednost čerstvější zprávě. Vrať je seřazené od "
        f"nejdůležitější, hodiny ukazují jen prvních několik. U každé vrať index "
        f"zprávy ze seznamu.\n\n{listing}"
    )
    # Model bývá občas přetížený (503). Zkusí se hlavní model dvakrát, pak
    # postupně záložní modely, takže výpadek jednoho fondu výběr nezastaví.
    last_error: Exception | None = None
    for model_name in [MODEL, *[m for m in FALLBACK_MODELS if m != MODEL]]:
        for attempt in range(2):
            try:
                response = client.models.generate_content(
                    model=model_name, contents=prompt, config=config
                )
                picks = response.parsed
                if not picks:
                    raise RuntimeError("prazdna strukturovana odpoved")
                return picks
            except genai_errors.ServerError as error:  # 5xx včetně 503 UNAVAILABLE
                last_error = error
                time.sleep(4)
    raise RuntimeError(f"Model nedostupny po nekolika pokusech: {last_error}")


def render(picks: list[Pick], items: list[dict]) -> str:
    parts = [
        '<?xml version="1.0" encoding="UTF-8"?>',
        '<rss version="2.0"><channel>',
        "<title>Zpravy dne</title>",
        f"<link>{html.escape(CHANNEL_LINK)}</link>",
        "<description>Vyber nejdulezitejsich zprav dne</description>",
        f"<lastBuildDate>{format_datetime(datetime.now(timezone.utc))}</lastBuildDate>",
    ]
    for pick in picks:
        source = items[pick.index]
        title = (pick.headline.strip() or source["title"])[:MAX_TITLE_CHARS]
        # guid drží identitu položky. Hodiny ho nečtou, ale bez něj považuje
        # každá jiná čtečka celý kanál při každém stažení za nový.
        guid = html.escape(f"{source['published'].isoformat()}#{pick.index}")
        parts.append(
            "<item>"
            f"<title>{html.escape(title)}</title>"
            f'<guid isPermaLink="false">{guid}</guid>'
            f"<pubDate>{format_datetime(source['published'])}</pubDate>"
            "</item>"
        )
    parts.append("</channel></rss>")
    return "\n".join(parts)


def main() -> None:
    items = collect()
    if len(items) < MINIMUM_TO_PUBLISH:
        sys.exit(
            f"Zdroje vratily jen {len(items)} zprav mladsich nez "
            f"{MAX_AGE_HOURS:.0f} h, ponechavam predchozi soubor."
        )

    # Model občas vrátí stejný index dvakrát; bez téhle kontroly by se jedna
    # zpráva objevila na displeji dvakrát pod dvěma titulky.
    picks = []
    seen_indexes: set[int] = set()
    for pick in choose(items):
        if not 0 <= pick.index < len(items) or pick.index in seen_indexes:
            continue
        seen_indexes.add(pick.index)
        picks.append(pick)
        if len(picks) == PUBLISHED_MAX:
            break
    if len(picks) < MINIMUM_TO_PUBLISH:
        sys.exit(f"Model vratil jen {len(picks)} platnych zprav, ponechavam predchozi soubor.")

    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    temporary = OUTPUT.with_suffix(".tmp")
    # Bez fsync může výpadek napájení nechat na disku prázdný top.xml: přejmenování
    # je atomické, ale zápis obsahu ještě nemusí být na plotně.
    with open(temporary, "w", encoding="utf-8") as handle:
        handle.write(render(picks, items))
        handle.flush()
        os.fsync(handle.fileno())
    os.replace(temporary, OUTPUT)
    print(f"Zapsano {len(picks)} zprav do {OUTPUT}")


if __name__ == "__main__":
    main()
