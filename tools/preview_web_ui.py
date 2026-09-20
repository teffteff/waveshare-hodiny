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
from urllib.parse import parse_qs, urlparse

from web_ui_sources import (
    LOCALIZATION_HEADER,
    PAGE_HEADER,
    extract_raw_string,
)


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


def value_slots() -> list[dict]:
    """Dvě stránky po devíti slotech HODNOTY; první čtyři jsou zapnuté jako po migraci."""
    seeded = [
        ("VENKU", "sensor.venkovni_teplota", "°C", 1, "#4CCBEC"),
        ("OBÝVÁK", "sensor.obyvak_teplota", "°C", 1, "#FFB843"),
        ("VOC", "sensor.obyvak_voc", "ppb", 0, "#65C744"),
        ("CO₂", "sensor.obyvak_co2", "ppm", 0, "#FFB843"),
    ]
    slots = []
    for index in range(18):
        if index < len(seeded):
            name, entity, suffix, decimals, color = seeded[index]
            enabled = True
        else:
            name, entity, suffix, decimals, color = "", "", "°C", 1, "#FFFFFF"
            enabled = False
        slots.append(
            {
                "enabled": enabled,
                "custom": bool(name),
                "preset": "custom" if name else "temperature",
                "name": name,
                "entityId": entity,
                "suffix": suffix,
                "decimals": decimals,
                "colorScale": [{"value": 0.0, "color": color}],
            }
        )
    return slots


def stub_config() -> dict:
    """Odpovídá tvaru configJson() v ConfigurationWeb.cpp."""
    return {
        "dataSource": "home-assistant",
        "language": "cs",
        "openMeteoCity": "Ondřejov · Praha-východ · Středočeský kraj · Česko",
        "openMeteoLatitude": 49.90461,
        "openMeteoLongitude": 14.7842,
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
        "valueSlots": value_slots(),
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
        "deviceName": "waveshare-hodiny",
        "defaultDeviceName": "waveshare-hodiny",
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
        "rssEnabled": True,
        "rssUrl": "https://www.irozhlas.cz/rss/irozhlas",
        "rssItemCount": 5,
        "rssRefreshMinutes": 10,
        "rssDisplaySeconds": 20,
        "rssAutomaticRotation": False,
        "agendaEnabled": True,
        "agendaUrl": "https://hodiny:heslo@server.example/agenda.json",
        "agendaRefreshMinutes": 15,
        "agendaDisplaySeconds": 20,
        "agendaAutomaticRotation": False,
        "agendaHiddenCalendars": 0,
        "agendaPrivateKeyConfigured": False,
        "agendaCalendars": PREVIEW_AGENDA_CALENDARS,
        "schoolEnabled": True,
        "schoolUrl": "https://hodiny:heslo@server.example/school.json",
        "schoolShowHomework": True,
        "schoolMealNextDayHour": 16,
        "schoolRefreshMinutes": 20,
        "schoolDisplaySeconds": 20,
        "schoolAutomaticRotation": False,
        "satellitesEnabled": True,
        "satellitesUrl": "https://hodiny:heslo@server.example/satellites.json",
        "satellitesGroups": "stations,visual,weather",
        "satellitesMinElevation": 10,
        "satellitesRefreshSeconds": 60,
        "satellitesTopBearing": 0,
        "satellitesShowTracks": True,
        "satellitesDisplaySeconds": 20,
        "satellitesAutomaticRotation": False,
        "forecastEnabled": True,
        "forecastAirQuality": True,
        "forecastDayCount": 3,
        "forecastRefreshMinutes": 30,
        "forecastDisplaySeconds": 20,
        "forecastAutomaticRotation": False,
        # Stejné pořadí jako ve firmwaru: (kvalita ovzduší ? 5 : 0) + dny.
        "forecastHourCounts": [12, 11, 10, 9, 8, 10, 8, 7, 6, 5],
        "screenOrder": ["clock", "radar", "rss", "forecast", "planes"],
        "startupScreen": "clock",
        # Družice od soumraku do svítání, letadla přes den, zbytek vypnutý.
        "screenSchedule": [
            {"screen": "satellites", "startEvent": "civil-dusk", "startValue": 0,
             "endEvent": "civil-dawn", "endValue": 0},
            {"screen": "planes", "startEvent": "civil-dawn", "startValue": 15,
             "endEvent": "time", "endValue": 17 * 60},
            {"screen": "", "startEvent": "time", "startValue": 0,
             "endEvent": "time", "endValue": 0},
            {"screen": "", "startEvent": "time", "startValue": 0,
             "endEvent": "time", "endValue": 0},
        ],
        # Upozornění na déšť: zapnuté, aby byla v náhledu vidět celá sekce.
        "rainAlertEnabled": True,
        "rainAlertUrl": "https://hodiny:heslo@tvuj-server.example.net/rain.json",
        "rainAlertHorizonMinutes": 30,
        "rainAlertMinimumDbz": 28,
        "rainAlertRadiusKm": 5,
        "rainAlertHoldMinutes": 10,
        "rainAlertCooldownMinutes": 30,
        "rainAlertRefreshMinutes": 5,
        "rainAlertQuietAtNight": False,
        "currentScreen": preview_current_screen,
        "controlSecret": "nahled-bez-zarizeni",
        "webPasswordConfigured": preview_password_configured,
        "settingsShareConfigured": False,
        "settingsShareUrl": "",
    }


# Kalendáře agendy tak, jak je firmware převezme z odpovědi serveru.
PREVIEW_AGENDA_CALENDARS = [
    {"name": "Neumannovi", "private": False},
    {"name": "Adámek", "private": False},
    {"name": "Martin", "private": True},
]
# Heslo k soukromým kalendářům, které náhled přijme při zkoušce agendy.
PREVIEW_AGENDA_KEY = "tajne"


# Záloha pro náhled. Data nejsou skutečná záloha; firmware by je odmítl, ale
# stránce stačí tvar obálky.
BACKUP_PART_COUNT = 5


def partial_restore(fields: dict) -> bool:
    include = fields.get("include", "")
    return bool(include) and len(set(include.split(","))) < BACKUP_PART_COUNT


PREVIEW_BACKUP = {
    "format": "waveshare-hodiny-settings",
    "version": 4,
    "firmware": "0.0.0-preview",
    "schema": 41,
    "secrets": True,
    "exportedAt": "2026-09-13T10:00:00Z",
    "cipher": "AES-256-GCM",
    "kdf": "PBKDF2-SHA256",
    "iterations": 20000,
    "salt": "00" * 16,
    "nonce": "00" * 12,
    "data": "V0hTQgEAAAA",
}
# Heslo webu a heslo, které v náhledu otevře každou zálohu.
PREVIEW_PASSWORD = "webove-heslo"
PREVIEW_BACKUP_PASSWORD = "zalohove-heslo"
preview_password_configured = True
# Obrazovka, kterou náhled hlásí jako zobrazenou; mění ji "Zobrazit teď".
preview_current_screen = "clock"


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
        global preview_current_screen
        path = urlparse(self.path).path
        length = int(self.headers.get("Content-Length") or 0)
        body = self.rfile.read(length).decode("utf-8", "replace") if length else ""
        if path == "/api/screen/show":
            screen = (parse_qs(body).get("screen") or [""])[0]
            print(f"POST {path}: {screen}", flush=True)
            # Náhled nemá radar ani zprávy zapnuté, jako typické hodiny bez nich.
            if screen in ("radar", "rss"):
                self._json({"ok": False, "message":
                            "Obrazovka je vypnutá, nebo se teď přepnout nedá."}, 409)
                return
            preview_current_screen = screen
            self._json({"ok": True, "screen": screen})
            return
        if path == "/api/config":
            fields = {k: v[0] for k, v in parse_qs(body, keep_blank_values=True).items()}
            print(f"\n--- POST /api/config ({len(fields)} polí) ---", flush=True)
            for key in sorted(fields):
                print(f"  {key} = {fields[key]}", flush=True)
            self._json({"ok": True,
                        "saveConfirmationId": fields.get("saveConfirmationId", "")})
            return
        if path == "/api/ha/entities":
            # Firmware přeposílá čtveřice [id, název, jednotka, stav] tak, jak
            # je vyrenderoval Home Assistant. Náhled dodá jejich malý vzorek.
            self._json({"ok": True, "entities": [
                ["sensor.venkovni_teplota", "Venkovní teplota", "°C", "12.8"],
                ["sensor.obyvak_teplota", "Obývák teplota", "°C", "21.4"],
                ["sensor.obyvak_vlhkost", "Obývák vlhkost", "%", "47"],
                ["sensor.obyvak_co2", "Obývák CO2", "ppm", "812"],
                ["sensor.obyvak_voc", "Obývák VOC", "ppb", "140"],
                ["number.cil_teploty", "Cíl teploty", "°C", "22"],
                ["weather.domov", "Domov", "", "cloudy"],
                ["sun.sun", "Sun", "", "above_horizon"],
                ["light.loznice", "Ložnice", "", "off"],
                ["binary_sensor.dvere", "Vchodové dveře", "", "off"],
            ]})
            return
        if path == "/api/web-password":
            global preview_password_configured
            fields = {k: v[0] for k, v in parse_qs(body, keep_blank_values=True).items()}
            print(f"POST {path}: {fields}", flush=True)
            if preview_password_configured and \
                    fields.get("currentPassword") != PREVIEW_PASSWORD:
                self._json({"ok": False, "message": "Heslo webu není správné."}, 401)
                return
            if fields.get("action") == "clear":
                preview_password_configured = False
            else:
                preview_password_configured = True
            self._json({"ok": True, "configured": preview_password_configured})
            return
        if path.startswith("/api/backup/"):
            fields = {k: v[0] for k, v in parse_qs(body, keep_blank_values=True).items()}
            print(f"POST {path}: {sorted(fields)}", flush=True)
            restore = path in ("/api/backup/import", "/api/backup/share/download")
            if path in ("/api/backup/export", "/api/backup/share/upload") and \
                    not preview_password_configured:
                self._json({"ok": False, "message":
                            "Zálohy nesou tokeny, proto jdou vytvořit jen s "
                            "nastaveným heslem webu. Nastav ho v záložce Systém."}, 409)
                return
            if restore and fields.get("backupPassword") != PREVIEW_BACKUP_PASSWORD:
                self._json({"ok": False, "message":
                            "Heslo zálohy není správné, nebo je soubor poškozený."}, 401)
                return
            if path == "/api/backup/export":
                self._json({"ok": True, "backup": PREVIEW_BACKUP})
            elif restore:
                self._json({"ok": True, "partial": partial_restore(fields),
                            "webPasswordChanged": "system" in fields.get("include", "")})
            elif path == "/api/backup/share/list":
                self._json({"ok": True, "url": "https://server.example/settings",
                            "backups": [
                                {"name": "obyvak", "modified": "2026-09-13T08:15:00Z",
                                 "firmware": "2.1.0"},
                                {"name": "kuchyn", "modified": "2026-09-12T19:40:00Z",
                                 "firmware": "2.1.0"},
                            ]})
            elif path == "/api/backup/share/upload":
                self._json({"ok": True, "name": fields.get("name", ""),
                            "url": "https://server.example/settings"})
            else:
                self._json({"ok": True})
            return
        if path == "/api/school/test":
            fields = {k: v[0] for k, v in parse_qs(body, keep_blank_values=True).items()}
            print(f"POST {path}: {fields}", flush=True)
            # Tvar odpovědi firmwaru (appendSchoolFeedJson).
            self._json({
                "ok": True, "student": "Adam",
                "days": [
                    {"day": "DNES", "weekday": "ÚTERÝ", "end": "12:20", "lessons": [
                        {"hour": "1.", "subject": "Český jazyk", "note": "", "state": 0},
                        {"hour": "2.", "subject": "Anglický jazyk",
                         "note": "Suplování", "state": 1},
                    ]},
                    {"day": "ZÍTRA", "weekday": "STŘEDA", "end": "11:25", "lessons": [
                        {"hour": "1.", "subject": "Hudební výchova",
                         "note": "Odpadlá hodina", "state": 2},
                    ]},
                ],
                "homework": [
                    {"due": "ZÍTRA", "subject": "Matematika",
                     "title": "Pracovní sešit str. 12"},
                ],
                "messageCount": 1,
                "messages": [{"when": "VČERA", "sender": "Nováková", "title": "Třídní schůzky"}],
                "markCount": 1,
                "marks": [{"when": "DNES", "subject": "Matematika", "abbrev": "M",
                           "mark": "1", "theme": "Násobilka"}],
            })
            return
        if path == "/api/satellites/test":
            fields = {k: v[0] for k, v in parse_qs(body, keep_blank_values=True).items()}
            print(f"POST {path}: {fields}", flush=True)
            # Tvar odpovědi firmwaru (handleSatellitesTest).
            self._json({
                "ok": True, "count": 9, "total": 9, "ageHours": 3, "dark": True,
                "pending": False, "problem": "",
                "pass": {"rise": 1789508390, "set": 1789508779, "max": 48, "visible": True},
                "satellites": [
                    {"name": "ISS (ZARYA)", "group": "stations", "elevation": 54,
                     "azimuth": 212, "sunlit": True},
                    {"name": "NOAA 19", "group": "weather", "elevation": 31,
                     "azimuth": 18, "sunlit": True},
                    {"name": "SL-16 R/B", "group": "visual", "elevation": 12,
                     "azimuth": 97, "sunlit": False},
                ],
            })
            return
        if path == "/api/agenda/test":
            fields = {k: v[0] for k, v in parse_qs(body, keep_blank_values=True).items()}
            print(f"POST {path}: {fields}", flush=True)
            key = fields.get("agendaPrivateKey", "")
            if key and key != PREVIEW_AGENDA_KEY:
                self._json({"ok": False,
                            "message": "Heslo k soukromým kalendářům nesedí."}, 502)
                return
            hidden = int(fields.get("agendaHiddenCalendars") or 0)
            unlocked = key == PREVIEW_AGENDA_KEY
            items = [
                {"day": "DNES", "time": "18:00", "title": "Popelnice", "calendar": 0},
                {"day": "", "time": "19:00", "title": "Plavání", "calendar": 1},
                {"day": "ZÍTRA", "time": "08:00", "title": "Tajná schůzka", "calendar": 2},
            ]
            shown = [
                item for item in items
                if not hidden >> item["calendar"] & 1
                and (unlocked or not PREVIEW_AGENDA_CALENDARS[item["calendar"]]["private"])
            ]
            self._json({"ok": True, "count": len(shown),
                        "calendars": PREVIEW_AGENDA_CALENDARS, "items": shown})
            return
        if path == "/api/rss/test":
            # Náhled nechodí na síť; vrátí ukázku ve tvaru, který posílá firmware.
            self._json({
                "ok": True,
                "channel": "iROZHLAS.cz",
                "items": [
                    {"time": "16:42",
                     "title": "Zidovska obec se soudi o byvaly spolkovy dum v Brne. "
                              "V restituci ho neziskala, podala urcovaci zalobu"},
                    {"time": "16:38",
                     "title": "Mlejnek: Sance Motoristu na uspech ve volbach je miziva. "
                              "Kandidatura ministru je z nouze ctnost"},
                    {"time": "16:30", "title": "Rozpocet silencu a populistu?"},
                ],
            })
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
