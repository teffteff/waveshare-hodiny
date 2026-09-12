#!/usr/bin/env python3
"""Sdílený přístup k webovému rozhraní uloženému v hlavičkách firmwaru.

Stránky jsou v repozitáři čitelné jako surové řetězce C++, aby se daly
upravovat jako obyčejné HTML. Do firmwaru se ale nedostanou v této podobě:
`generate_compressed_pages.py` je zabalí gzipem a vygeneruje z nich
`WaveshareHodiny/CompressedPages.h`. Tenhle modul je jediné místo, které ví,
kde stránky leží a jak se z hlavičky vytáhnou.
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
FIRMWARE = ROOT / "WaveshareHodiny"

PAGE_HEADER = FIRMWARE / "ConfigurationPage.h"
LOCALIZATION_HEADER = FIRMWARE / "ConfigurationLocalization.h"
LOGIN_HEADER = FIRMWARE / "LoginPage.h"
DIAGNOSTIC_HEADER = FIRMWARE / "DiagnosticPage.h"


def extract_raw_string(path: Path, symbol: str, delimiter: str) -> str:
    """Vrátí obsah surového řetězce C++ R"DELIM( ... )DELIM"."""
    source = path.read_text(encoding="utf-8")
    opening = f'{symbol}[] PROGMEM = R"{delimiter}('
    start = source.find(opening)
    if start < 0:
        raise SystemExit(f"V {path.name} se nepodařilo najít {symbol}.")
    start += len(opening)
    end = source.find(f'){delimiter}"', start)
    if end < 0:
        raise SystemExit(f"Řetězec {symbol} v {path.name} není ukončený.")
    return source[start:end]


@dataclass(frozen=True)
class WebAsset:
    """Jedna stránka nebo skript, které firmware posílá prohlížeči."""

    header: Path
    symbol: str
    delimiter: str

    def text(self) -> str:
        return extract_raw_string(self.header, self.symbol, self.delimiter)


# Pořadí určuje pořadí polí ve vygenerované hlavičce.
WEB_ASSETS = (
    WebAsset(PAGE_HEADER, "CONFIGURATION_PAGE", "HTML"),
    WebAsset(LOCALIZATION_HEADER, "CONFIGURATION_LOCALIZATION_JS", "JS"),
    WebAsset(LOGIN_HEADER, "LOGIN_PAGE", "HTML"),
    WebAsset(DIAGNOSTIC_HEADER, "DIAGNOSTIC_PAGE", "HTML"),
)
