#!/usr/bin/env python3
"""Zabalí vestavěné webové stránky gzipem do WaveshareHodiny/CompressedPages.h.

Konfigurační stránka vyrostla na 190 kB a firmware ji posílal znak po znaku
tak, jak je zapsaná ve zdroji. Prohlížeč umí gzip vždycky, takže se do
firmwaru dostane jen zabalená podoba: ušetří to zhruba 180 kB flash a stránka
odchází třikrát rychleji, což je rozdíl mezi načtením a zaseknutou smyčkou na
telefonu se slabým signálem.

Generátor je součástí `build.sh` i `build-release.sh`; výsledná hlavička je
ignorovaná Gitem stejně jako ostatní generované hlavičky.
"""

from __future__ import annotations

import gzip
from pathlib import Path

from web_ui_sources import FIRMWARE, WEB_ASSETS

OUTPUT = FIRMWARE / "CompressedPages.h"
BYTES_PER_LINE = 16


def compress(text: str) -> bytes:
    # mtime=0 drží výstup shodný mezi sestaveními, aby se release binárka
    # nelišila jen razítkem uvnitř gzip hlavičky.
    return gzip.compress(text.encode("utf-8"), compresslevel=9, mtime=0)


def render_array(symbol: str, payload: bytes) -> str:
    lines = [f"const uint8_t {symbol}[] PROGMEM = {{"]
    for start in range(0, len(payload), BYTES_PER_LINE):
        chunk = payload[start:start + BYTES_PER_LINE]
        lines.append("    " + " ".join(f"0x{byte:02x}," for byte in chunk))
    lines.append("};")
    return "\n".join(lines)


def render_header() -> str:
    blocks = [
        "// Generováno tools/generate_compressed_pages.py - needituj ručně.",
        "// Zdrojem jsou surové řetězce v ConfigurationPage.h a spol.; ty se do",
        "// firmwaru už nepřekládají, protože se posílá jen zabalená podoba.",
        "#pragma once",
        "",
        "#include <pgmspace.h>",
        "#include <stdint.h>",
        "",
    ]
    for asset in WEB_ASSETS:
        text = asset.text()
        payload = compress(text)
        raw_bytes = len(text.encode("utf-8"))
        blocks.append(
            f"// {asset.header.name}: {raw_bytes} B -> {len(payload)} B")
        blocks.append(render_array(f"{asset.symbol}_GZIP", payload))
        blocks.append("")
    return "\n".join(blocks)


def main() -> None:
    rendered = render_header()
    # Přepisovat soubor beze změny obsahu by zbytečně shodilo inkrementální
    # sestavení celého ConfigurationWeb.cpp.
    if OUTPUT.exists() and OUTPUT.read_text(encoding="utf-8") == rendered:
        print(f"{OUTPUT.name} je aktuální.")
        return
    OUTPUT.write_text(rendered, encoding="utf-8")
    total_raw = sum(len(asset.text().encode("utf-8")) for asset in WEB_ASSETS)
    total_packed = sum(len(compress(asset.text())) for asset in WEB_ASSETS)
    print(f"Webové rozhraní zabaleno: {total_raw} B -> {total_packed} B.")


if __name__ == "__main__":
    main()
