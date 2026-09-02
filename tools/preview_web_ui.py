#!/usr/bin/env python3
"""Zobrazí vestavěné webové rozhraní v prohlížeči bez hodin.

Vytáhne CONFIGURATION_PAGE a CONFIGURATION_LOCALIZATION_JS přímo z hlaviček
firmwaru a doplní je stub API, takže se stránka chová jako na zařízení.
Slouží k rychlé úpravě HTML/JS: stačí uložit hlavičku a obnovit stránku,
žádné sestavení ani flashnutí.

    python3 tools/preview_web_ui.py            # http://127.0.0.1:8080
    python3 tools/preview_web_ui.py --port 9000

Odeslání formuláře se neukládá, ale vypíše se do konzole, takže je vidět,
jaká pole by se poslala do firmwaru.
"""

from __future__ import annotations

import argparse
import json
import re
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse

ROOT = Path(__file__).resolve().parent.parent
FIRMWARE = ROOT / "WaveshareHodiny"
PAGE_HEADER = FIRMWARE / "ConfigurationPage.h"
LOCALIZATION_HEADER = FIRMWARE / "ConfigurationLocalization.h"


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


def side(name: str, entity: str, icon: str, color: str) -> dict:
    return {
        "name": name,
        "entityId": entity,
        "temperatureEntityId": entity,
        "icon": icon,
        "color": color,
        "custom": True,
        "preset": "custom",
        "suffix": "°C",
        "decimals": 1,
    }


def metric(name: str, entity: str, suffix: str, decimals: int) -> dict:
    return {
        "custom": False,
        "preset": "co2",
        "name": name,
        "entityId": entity,
        "suffix": suffix,
        "decimals": decimals,
    }


def stub_config() -> dict:
    """Odpovídá tvaru configJson() v ConfigurationWeb.cpp."""
    return {
        "dataSource": "home-assistant",
        "language": "cs",
        "openMeteoCity": "Brno",
        "openMeteoLatitude": 49.1951,
        "openMeteoLongitude": 16.6068,
        "openMeteoCountry": "CZ",
        "openMeteoSlots": [
            {"value": "temperature_2m", "name": "TEPLOTA", "color": "#4CCBEC"},
            {"value": "apparent_temperature", "name": "POCITOVÁ", "color": "#FFB843"},
            {"value": "relative_humidity_2m", "name": "VLHKOST", "color": "#65C744"},
            {"value": "pressure_msl", "name": "TLAK", "color": "#FFB843"},
        ],
        "tmepSlots": [{"enabled": False, "sensorId": "", "field": "", "unit": "", "decimals": 1}] * 4,
        "tmepConfigured": False,
        "homeAssistantUrl": "http://homeassistant.local:8123",
        "tokenConfigured": True,
        "weatherEntityId": "weather.home",
        "sunEntityId": "sun.sun",
        "dayNightLightEntityId": "",
        "sunriseOffsetMinutes": 0,
        "sunsetOffsetMinutes": 0,
        "nextSunriseTimestamp": 0,
        "nextSunsetTimestamp": 0,
        "leftSide": side("VENKU", "sensor.venkovni_teplota", "weather", "#4CCBEC"),
        "rightSide": side("OBÝVÁK", "sensor.obyvak_teplota", "sofa", "#FFB843"),
        "metricA": metric("VOC", "sensor.obyvak_voc", "ppb", 0),
        "metricB": metric("CO₂", "sensor.obyvak_co2", "ppm", 0),
        "leftValueColorScale": [{"value": 0.0, "color": "#4CCBEC"}],
        "rightValueColorScale": [{"value": 0.0, "color": "#FFB843"}],
        "metricAColorScale": [{"value": 0.0, "color": "#65C744"}],
        "metricBColorScale": [{"value": 0.0, "color": "#FFB843"}],
        "animatedWeatherIcons": True,
        "weatherIconStyle": "monochrome",
        "monochromeWeatherIconColor": "#FFFFFF",
        "leftWeatherIconColor": "#FFFFFF",
        "rightWeatherIconColor": "#FFFFFF",
        "dayBrightness": 35,
        "nightBrightness": 10,
        "automaticDayNight": False,
        "nightVisualMode": "red",
        "automaticFirmwareUpdate": False,
        "webMode": "always",
        "clockStyle": "digital",
        "analogToneColor": "#00D6FF",
        "analogHandToneColor": "#00D6FF",
        "analogCardinalAccentColor": "#FFAB00",
        "analogCardinalAccentsEnabled": True,
        "analogOutlineHandsEnabled": False,
        "analogMonochromeValuesEnabled": False,
        "analogValuesAboveHandsEnabled": False,
        "analogDateFormat": "weekday-day-month",
        "analogDateColor": "#B5B5B5",
        "timeColor": "#F6F6F6",
        "timeColonEffect": "steady",
        "showLeadingHourZero": True,
        "timeFont": "barlow",
        "dateFormat": "weekday-day-month",
        "dateColor": "#B5B5B5",
        "secondRingEnabled": True,
        "secondEffect": "dots",
        "secondRingBackgroundColor": "#FFFFFF",
        "secondRingBackgroundBrightness": 0,
        "secondRingBackgroundDotSize": 3,
        "secondDotSize": 3,
        "secondDotColor": "#FFFFFF",
        "secondDotBrightness": 175,
        "radarAvailable": True,
        "radarRadiusKm": 0,
        "radarFrameCount": 6,
        "radarMapOpacity": 100,
        "radarPauseSeconds": 5,
        "automaticRadarRotation": False,
        "clockDisplaySeconds": 120,
        "radarDisplaySeconds": 20,
        "controlSecret": "nahled-bez-zarizeni",
    }


STUB_RESPONSES = {
    "/api/firmware": {"version": "0.0.0-preview", "updateAvailable": False},
    "/api/update-status": {"state": "idle", "message": "Náhled bez zařízení"},
    "/api/radar/state": {"radiusKm": 0, "available": True},
    "/api/diagnostics": {"entries": [], "message": "Náhled bez zařízení"},
}


class PreviewHandler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):  # tišší výpis
        pass

    def _send(self, status: int, content_type: str, body: bytes) -> None:
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def _json(self, payload: dict, status: int = 200) -> None:
        self._send(status, "application/json; charset=utf-8",
                   json.dumps(payload, ensure_ascii=False).encode("utf-8"))

    def do_GET(self) -> None:
        path = urlparse(self.path).path
        if path in ("/", "/index.html"):
            # Hlavičky se čtou při každém požadavku, takže stačí obnovit stránku.
            page = extract_raw_string(PAGE_HEADER, "CONFIGURATION_PAGE", "HTML")
            self._send(200, "text/html; charset=utf-8", page.encode("utf-8"))
        elif path == "/ui-language.js":
            script = extract_raw_string(
                LOCALIZATION_HEADER, "CONFIGURATION_LOCALIZATION_JS", "JS")
            self._send(200, "text/javascript; charset=utf-8",
                       script.encode("utf-8"))
        elif path == "/api/config":
            self._json(stub_config())
        elif path in STUB_RESPONSES:
            self._json(STUB_RESPONSES[path])
        else:
            self._json({"error": "Náhled tento endpoint nezná", "path": path}, 404)

    def do_POST(self) -> None:
        path = urlparse(self.path).path
        length = int(self.headers.get("Content-Length") or 0)
        body = self.rfile.read(length).decode("utf-8", "replace") if length else ""
        if path == "/api/config":
            fields = {k: v[0] for k, v in parse_qs(body, keep_blank_values=True).items()}
            print(f"\n--- POST /api/config ({len(fields)} polí) ---", flush=True)
            for key in sorted(fields):
                print(f"  {key} = {fields[key]}", flush=True)
            self._json({"ok": True,
                        "saveConfirmationId": fields.get("saveConfirmationId", "")})
            return
        print(f"POST {path}: {body[:200]}", flush=True)
        self._json({"ok": True})


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=8080)
    parser.add_argument("--host", default="127.0.0.1")
    args = parser.parse_args()

    # Ověří, že se obě hlavičky dají přečíst, dřív než se otevře port.
    extract_raw_string(PAGE_HEADER, "CONFIGURATION_PAGE", "HTML")
    extract_raw_string(LOCALIZATION_HEADER, "CONFIGURATION_LOCALIZATION_JS", "JS")

    server = ThreadingHTTPServer((args.host, args.port), PreviewHandler)
    print(f"Náhled webového rozhraní běží na http://{args.host}:{args.port}")
    print("Změny v ConfigurationPage.h se projeví po obnovení stránky. Ukončení: Ctrl+C")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nNáhled ukončen.")


if __name__ == "__main__":
    main()
