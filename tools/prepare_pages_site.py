#!/usr/bin/env python3
"""Složí statickou část webu pro GitHub Pages (bez firmwaru).

Zkopíruje docs/, snímky obrazovky a video do ADRESARE a v index.html nastaví
obsahové verze styles.css a app.js (?v=<prvních 12 znaků SHA-256>), aby
prohlížeče po nasazení nevzaly starý soubor z cache. Používá ho nasazení
Pages, release i CI, takže tools/validate_pages_bundle.py kontroluje stejný
web, jaký se nasadí.
"""

from __future__ import annotations

import hashlib
import re
import shutil
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
MEDIA = "waveshare-hodiny-minute-transition.mp4"


def short_sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()[:12]


def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit("Použití: prepare_pages_site.py ADRESAR_WEBU")
    site = Path(sys.argv[1]).resolve()
    for name in ("screenshots", "media", "firmware"):
        (site / name).mkdir(parents=True, exist_ok=True)
    shutil.copytree(ROOT / "docs", site, dirs_exist_ok=True)
    for screenshot in sorted((ROOT / "screenshots").glob("*.png")):
        shutil.copy2(screenshot, site / "screenshots" / screenshot.name)
    shutil.copy2(ROOT / "media" / MEDIA, site / "media" / MEDIA)

    index = site / "index.html"
    html = index.read_text(encoding="utf-8")
    for attribute, name in (("href", "styles.css"), ("src", "app.js")):
        version = short_sha256(site / name)
        pattern = re.compile(
            rf'({attribute}="{re.escape(name)})(\?v=[^"]+)?"'
        )
        html, count = pattern.subn(rf'\g<1>?v={version}"', html)
        if count == 0:
            raise SystemExit(f"index.html neodkazuje na {name}.")
    index.write_text(html, encoding="utf-8")


if __name__ == "__main__":
    main()
