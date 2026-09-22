"""Nocni obloha pro hodiny: planety, Mesic, jasne hvezdy, udalosti a Kp.

Pripojuje se k serveru druzic: hodiny se ptaji tehoz /satellites.json se
stejnym heslem, jen s parametrem view=sky. Druha sluzba ani druha adresa
v hodinach tak nejsou potreba.

Polohy tel se neposilaji jako azimut a vyska, ale jako rektascenze
a deklinace. Hodiny si z nich vysku a azimut dopocitaji kazdou vterinu
z hvezdneho casu, takze se staci ptat jednou za ctvrt hodiny a planety se
presto po obloze hybou plynule. Mesic se posila topocentricky: jeho paralaxa
je az stupen a bez ni by na obloze hodin stal vedle.

Udalosti (roje, konjunkce, opozice, zatmeni) se pocitaji pro polohu hodin,
protoze na ni zalezi: zatmeni Slunce se hleda podle vzdalenosti kotoucu videne
primo z mista hodin, ne z tabulky pasu totality. Pocitaji se jednou za
EVENTS_SECONDS pro kazdou polohu zaokrouhlenou na desetinu stupne.

K poloham patri i draha Mesice na pristich 14 hodin (hodiny z ni kresli,
kudy pujde), astronomicka tma, ktera prave je nebo prijde, a par dni kolem
maxima roje jeho radiant. Obrazce souhvezdi maji jmeno v jejich stredu
a hvezdy sva jmena a souhvezdi pro detail po klepnuti.

Index Kp je z NOAA SWPC (odhad po minute a predpoved po tri hodiny) a obnovuje
se nejvys jednou za KP_SECONDS. Poplach "polarni zare" vyhlasuji hodiny samy
podle vlastniho prahu.

Efemeridy DE421 (17 MB, platne do roku 2053) si skyfield stahne pri prvnim
startu do adresare cache sluzby; do te doby se na oblohu odpovida 503, stejne
jako kdyz se stahuje skupina druzic.
"""
from __future__ import annotations

import calendar
import json
import math
import threading
import time
import urllib.error
import urllib.request
from datetime import datetime, timezone

import numpy as np
from skyfield import almanac, eclipselib
from skyfield.api import Loader, Star, load_constellation_map, load_constellation_names, wgs84
from skyfield.positionlib import position_of_radec
from skyfield.framelib import ecliptic_J2000_frame
from skyfield.magnitudelib import planetary_magnitude

EPHEMERIS = "de421.bsp"
KP_NOW_URL = "https://services.swpc.noaa.gov/json/planetary_k_index_1m.json"
KP_FORECAST_URL = "https://services.swpc.noaa.gov/products/noaa-planetary-k-index-forecast.json"
KP_SECONDS = 600
KP_TIMEOUT_SECONDS = 20
# Odhad Kp po minute zastara, kdyz NOAA hodinu nic nepridalo; pak se neposila.
KP_MAX_AGE_SECONDS = 3600
MAX_KP_BYTES = 2 * 1024 * 1024
# Polohy tel se prepocitavaji nejvys jednou za minutu pro kazdou polohu hodin.
POSITIONS_SECONDS = 60
EVENTS_SECONDS = 6 * 3600
CACHE_ENTRIES = 32
# Tolik radku maji hodiny pod oblohou.
MAX_EVENTS = 3

# Na kolik dni dopredu se ktere udalosti hledaji. Zatmeni jsou vzacna, proto
# az dva roky; bezne konjunkce jen dva tydny, jinak by zaplnily cely seznam.
MOON_CONJUNCTION_DAYS = 14
PLANET_EVENT_DAYS = 60
SHOWER_DAYS = 60
# Zatmeni se hlasi mesic dopredu, velke (uplne, prstencove nebo aspon z poloviny)
# tri mesice. Driv by radek jen rok strasil datem, ke kteremu se nic nedeje.
ECLIPSE_DAYS = 30
MAJOR_ECLIPSE_DAYS = 90
MAJOR_ECLIPSE_PERCENT = 50
# Radiant roje se kresli na obloze tolik dni pred maximem a po nem.
RADIANT_DAYS_BEFORE = 3.0
RADIANT_DAYS_AFTER = 2.0
# Draha Mesice: vzorky po pul hodine na tolik hodin dopredu. Hodiny kresli
# cely nejblizsi prechod oblohou, pokud Mesic vyjde do 12 hodin; prechod
# trva nejvys kolem 16 hodin.
MOON_TRACK_STEP_SECONDS = 1800
MOON_TRACK_HOURS = 28
# A tolik hodin zpet ("moonPast"), aby hodiny kreslily i cast prechodu, kterou
# uz Mesic urazil. Zvlast, protoze starsi hodiny cekaji "moonTrack" od ted.
MOON_PAST_HOURS = 16
# Astronomicka tma: Slunce aspon 18° pod obzorem.
DARK_SUN_DEGREES = -18.0
# Tesne prilozeni: Mesic k planete nebo hvezde, planeta k planete.
MOON_CONJUNCTION_DEGREES = 4.0
MOON_STAR_DEGREES = 2.0
PLANET_CONJUNCTION_DEGREES = 3.0

SUN_RADIUS_KM = 696000.0
MOON_RADIUS_KM = 1737.4
AU_KM = 149597870.7

# (klic, efemeridy, cesky, anglicky, druh). Druh: 0 Slunce, 1 Mesic, 2 planeta.
BODIES = (
    ("sun", "sun", "Slunce", "Sun", 0),
    ("moon", "moon", "Měsíc", "Moon", 1),
    ("mercury", "mercury", "Merkur", "Mercury", 2),
    ("venus", "venus", "Venuše", "Venus", 2),
    ("mars", "mars", "Mars", "Mars", 2),
    ("jupiter", "jupiter barycenter", "Jupiter", "Jupiter", 2),
    ("saturn", "saturn barycenter", "Saturn", "Saturn", 2),
    ("uranus", "uranus barycenter", "Uran", "Uranus", 2),
    ("neptune", "neptune barycenter", "Neptun", "Neptune", 2),
)
# Jen tyhle jde videt okem, takze jen ony se hodi do konjunkci.
NAKED_EYE = ("mercury", "venus", "mars", "jupiter", "saturn")
# Opozice se hlasi jen u planet, ktere jde videt okem.
OUTER = ("mars", "jupiter", "saturn")
INNER = ("mercury", "venus")
# Cesky 7. pad pro "Mesic 2° od Jupiteru" - jmena se sklonuji.
CZECH_FROM = {
    "mercury": "Merkuru", "venus": "Venuše", "mars": "Marsu", "jupiter": "Jupiteru",
    "saturn": "Saturnu", "uranus": "Uranu", "neptune": "Neptunu",
}

# Nejjasnejsi hvezdy: jmeno, rektascenze J2000 (h, m, s), deklinace (st, ', "),
# hvezdna velikost. Stari souradnic (precese asi 0,4° od roku 2000) je na
# kruhu o polomeru 222 px pod jeden pixel.
STARS = (
    ("Sirius", (6, 45, 8.9), (-16, 42, 58), -1.46),
    ("Arcturus", (14, 15, 39.7), (19, 10, 57), -0.05),
    ("Vega", (18, 36, 56.3), (38, 47, 1), 0.03),
    ("Capella", (5, 16, 41.4), (45, 59, 53), 0.08),
    ("Rigel", (5, 14, 32.3), (-8, 12, 6), 0.13),
    ("Procyon", (7, 39, 18.1), (5, 13, 30), 0.34),
    ("Betelgeuse", (5, 55, 10.3), (7, 24, 25), 0.42),
    ("Altair", (19, 50, 47.0), (8, 52, 6), 0.76),
    ("Aldebaran", (4, 35, 55.2), (16, 30, 33), 0.86),
    ("Antares", (16, 29, 24.4), (-26, 25, 55), 0.96),
    ("Spica", (13, 25, 11.6), (-11, 9, 41), 0.97),
    ("Pollux", (7, 45, 18.9), (28, 1, 34), 1.14),
    ("Fomalhaut", (22, 57, 39.0), (-29, 37, 20), 1.16),
    ("Deneb", (20, 41, 25.9), (45, 16, 49), 1.25),
    ("Regulus", (10, 8, 22.3), (11, 58, 2), 1.35),
    ("Adhara", (6, 58, 37.5), (-28, 58, 20), 1.50),
    ("Castor", (7, 34, 36.0), (31, 53, 18), 1.58),
    ("Shaula", (17, 33, 36.5), (-37, 6, 14), 1.62),
    ("Bellatrix", (5, 25, 7.9), (6, 20, 59), 1.64),
    ("Elnath", (5, 26, 17.5), (28, 36, 27), 1.65),
    ("Alnilam", (5, 36, 12.8), (-1, -12, -7), 1.69),
    ("Alnitak", (5, 40, 45.5), (-1, -56, -34), 1.77),
    ("Mintaka", (5, 32, 0.4), (0, -17, -57), 2.23),
    ("Saiph", (5, 47, 45.4), (-9, 40, 11), 2.09),
    ("Alioth", (12, 54, 1.7), (55, 57, 35), 1.77),
    ("Dubhe", (11, 3, 43.7), (61, 45, 3), 1.79),
    ("Merak", (11, 1, 50.5), (56, 22, 57), 2.37),
    ("Phecda", (11, 53, 49.8), (53, 41, 41), 2.44),
    ("Megrez", (12, 15, 25.6), (57, 1, 57), 3.31),
    ("Mizar", (13, 23, 55.5), (54, 55, 31), 2.23),
    ("Alkaid", (13, 47, 32.4), (49, 18, 48), 1.86),
    ("Mirfak", (3, 24, 19.4), (49, 51, 40), 1.80),
    ("Algol", (3, 8, 10.1), (40, 57, 20), 2.10),
    ("Wezen", (7, 8, 23.5), (-26, 23, 36), 1.84),
    ("Aludra", (7, 24, 5.7), (-29, 18, 11), 2.45),
    ("Mirzam", (6, 22, 42.0), (-17, 57, 21), 1.98),
    ("Kaus Australis", (18, 24, 10.3), (-34, 23, 5), 1.85),
    ("Nunki", (18, 55, 15.9), (-26, 17, 48), 2.05),
    ("Menkalinan", (5, 59, 31.7), (44, 56, 51), 1.90),
    ("Hassaleh", (4, 56, 59.6), (33, 9, 58), 2.69),
    ("Alhena", (6, 37, 42.7), (16, 23, 57), 1.92),
    ("Mebsuta", (6, 43, 55.9), (25, 7, 52), 3.06),
    ("Tejat", (6, 22, 57.6), (22, 30, 49), 2.88),
    ("Polaris", (2, 31, 49.1), (89, 15, 51), 1.98),
    ("Kochab", (14, 50, 42.3), (74, 9, 20), 2.08),
    ("Pherkad", (15, 20, 43.7), (71, 50, 2), 3.00),
    ("Alphard", (9, 27, 35.2), (-8, 39, 31), 1.98),
    ("Hamal", (2, 7, 10.4), (23, 27, 45), 2.00),
    ("Diphda", (0, 43, 35.4), (-17, 59, 12), 2.04),
    ("Menkar", (3, 2, 16.8), (4, 5, 23), 2.54),
    ("Alpheratz", (0, 8, 23.3), (29, 5, 26), 2.06),
    ("Mirach", (1, 9, 43.9), (35, 37, 14), 2.05),
    ("Almach", (2, 3, 54.0), (42, 19, 47), 2.10),
    ("Scheat", (23, 3, 46.5), (28, 4, 58), 2.42),
    ("Markab", (23, 4, 45.7), (15, 12, 19), 2.49),
    ("Algenib", (0, 13, 14.2), (15, 11, 1), 2.83),
    ("Enif", (21, 44, 11.2), (9, 52, 30), 2.39),
    ("Schedar", (0, 40, 30.4), (56, 32, 14), 2.24),
    ("Caph", (0, 9, 10.7), (59, 8, 59), 2.28),
    ("Navi", (0, 56, 42.5), (60, 43, 0), 2.47),
    ("Ruchbah", (1, 25, 49.0), (60, 14, 7), 2.68),
    ("Segin", (1, 54, 23.7), (63, 40, 12), 3.37),
    ("Alderamin", (21, 18, 34.8), (62, 35, 8), 2.45),
    ("Rasalhague", (17, 34, 56.1), (12, 33, 36), 2.07),
    ("Eltanin", (17, 56, 36.4), (51, 29, 20), 2.24),
    ("Rastaban", (17, 30, 26.0), (52, 18, 5), 2.79),
    ("Sadr", (20, 22, 13.7), (40, 15, 24), 2.23),
    ("Aljanah", (20, 46, 12.7), (33, 58, 13), 2.48),
    ("Fawaris", (19, 44, 58.5), (45, 7, 51), 2.87),
    ("Albireo", (19, 30, 43.3), (27, 57, 35), 3.05),
    ("Sheliak", (18, 50, 4.8), (33, 21, 46), 3.52),
    ("Sulafat", (18, 58, 56.6), (32, 41, 22), 3.24),
    ("Tarazed", (19, 46, 15.6), (10, 36, 48), 2.72),
    ("Alcyone", (3, 47, 29.1), (24, 6, 18), 2.87),
    ("Denebola", (11, 49, 3.6), (14, 34, 19), 2.14),
    ("Algieba", (10, 19, 58.4), (19, 50, 29), 2.08),
    ("Zosma", (11, 14, 6.5), (20, 31, 25), 2.56),
    ("Izar", (14, 44, 59.2), (27, 4, 27), 2.37),
    ("Muphrid", (13, 54, 41.1), (18, 23, 52), 2.68),
    ("Alphecca", (15, 34, 41.3), (26, 42, 53), 2.23),
    ("Unukalhai", (15, 44, 16.1), (6, 25, 32), 2.63),
    ("Dschubba", (16, 0, 20.0), (-22, 37, 18), 2.29),
    ("Sabik", (17, 10, 22.7), (-15, 43, 29), 2.43),
    ("Kornephoros", (16, 30, 13.2), (21, 29, 22), 2.78),
    ("Porrima", (12, 41, 39.6), (-1, -26, -58), 2.74),
    ("Vindemiatrix", (13, 2, 10.6), (10, 57, 33), 2.83),
    ("Gienah", (12, 15, 48.4), (-17, 32, 31), 2.59),
    ("Kraz", (12, 34, 23.2), (-23, 23, 48), 2.65),
    ("Zubeneschamali", (15, 17, 0.4), (-9, 22, 59), 2.61),
    ("Zubenelgenubi", (14, 50, 52.7), (-16, 2, 30), 2.75),
    ("Sadalsuud", (21, 31, 33.5), (-5, 34, 16), 2.90),
    ("Sadalmelik", (22, 5, 47.0), (0, -19, -11), 2.95),
    ("Deneb Algedi", (21, 47, 2.4), (-16, 7, 38), 2.87),
    ("Arneb", (5, 32, 43.8), (-17, 49, 20), 2.58),
    ("Nihal", (5, 28, 14.7), (-20, 45, 34), 2.84),
    ("Gomeisa", (7, 27, 9.0), (8, 17, 21), 2.89),
)
STAR_INDEX = {star[0]: index for index, star in enumerate(STARS)}
# Obrazce souhvezdi jako dvojice hvezd, po souhvezdich: kazdy obrazec dostane
# na obloze sve jmeno. Jen ty nejznamejsi: plna mapa by se na kruh o polomeru
# 222 px nevesla. Jmeno obrazce (cesky, anglicky) je tam, kde se obrazec rika
# jinak nez souhvezdi: Velky vuz je jen cast Velke medvedice.
FIGURES = (
    ("UMa", ("Velký vůz", "Big Dipper"), (
        ("Dubhe", "Merak"), ("Merak", "Phecda"), ("Phecda", "Megrez"), ("Megrez", "Dubhe"),
        ("Megrez", "Alioth"), ("Alioth", "Mizar"), ("Mizar", "Alkaid"))),
    ("Cas", None, (
        ("Caph", "Schedar"), ("Schedar", "Navi"), ("Navi", "Ruchbah"), ("Ruchbah", "Segin"))),
    ("Ori", None, (
        ("Betelgeuse", "Bellatrix"), ("Bellatrix", "Mintaka"), ("Mintaka", "Alnilam"),
        ("Alnilam", "Alnitak"), ("Alnitak", "Saiph"), ("Saiph", "Rigel"), ("Rigel", "Mintaka"),
        ("Betelgeuse", "Alnitak"))),
    ("Cyg", None, (
        ("Deneb", "Sadr"), ("Sadr", "Albireo"), ("Aljanah", "Sadr"), ("Sadr", "Fawaris"))),
    ("Lyr", None, (("Vega", "Sheliak"), ("Sheliak", "Sulafat"), ("Sulafat", "Vega"))),
    ("Leo", None, (
        ("Regulus", "Algieba"), ("Algieba", "Zosma"), ("Zosma", "Denebola"),
        ("Denebola", "Regulus"))),
    ("Sco", None, (("Dschubba", "Antares"), ("Antares", "Shaula"))),
    # Alpheratz patri Andromede, ale ctverec bez ni neni ctverec.
    ("Peg", None, (
        ("Alpheratz", "Scheat"), ("Scheat", "Markab"), ("Markab", "Algenib"),
        ("Algenib", "Alpheratz"))),
    ("And", None, (("Alpheratz", "Mirach"), ("Mirach", "Almach"))),
    ("Gem", None, (
        ("Castor", "Pollux"), ("Pollux", "Alhena"), ("Castor", "Mebsuta"), ("Mebsuta", "Tejat"))),
    ("Tau", None, (("Aldebaran", "Elnath"),)),
    ("Aur", None, (
        ("Capella", "Menkalinan"), ("Menkalinan", "Elnath"), ("Elnath", "Hassaleh"),
        ("Hassaleh", "Capella"))),
    ("Boo", None, (("Arcturus", "Izar"), ("Arcturus", "Muphrid"))),
    ("Aql", None, (("Altair", "Tarazed"),)),
    ("Per", None, (("Mirfak", "Algol"),)),
    ("CMa", None, (
        ("Sirius", "Mirzam"), ("Sirius", "Wezen"), ("Wezen", "Adhara"), ("Wezen", "Aludra"))),
)
LINES = tuple(pair for _, _, pairs in FIGURES for pair in pairs)
# Hvezdy, ke kterym se hlida prilozeni Mesice. Plejady zastupuje Alcyone.
MOON_STARS = {
    "Alcyone": ("Plejád", "the Pleiades"),
    "Aldebaran": ("Aldebaranu", "Aldebaran"),
    "Regulus": ("Regula", "Regulus"),
    "Spica": ("Spiky", "Spica"),
    "Antares": ("Antara", "Antares"),
}

# Meteoricke roje IMO: jmeno, delka Slunce v maximu (J2000), ZHR a radiant
# v maximu (rektascenze a deklinace ve stupnich). Datum maxima se z delky
# Slunce dopocita pro kazdy rok zvlast. Eta Akvaridy
# a jizni delta Akvaridy tu nejsou: z padesati stupnu severni sirky maji
# radiant tak nizko, ze z ohlasovaneho ZHR zbyde malokdy nekolik meteoru.
SHOWERS = (
    ("Kvadrantidy", "Quadrantids", 283.15, 80, 230.0, 49.0),
    ("Lyridy", "Lyrids", 32.32, 18, 271.0, 34.0),
    ("Perseidy", "Perseids", 140.0, 100, 48.0, 58.0),
    ("Drakonidy", "Draconids", 195.4, 10, 262.0, 54.0),
    ("Orionidy", "Orionids", 208.0, 20, 95.0, 16.0),
    ("Leonidy", "Leonids", 235.27, 15, 152.0, 22.0),
    ("Geminidy", "Geminids", 262.2, 150, 112.0, 33.0),
    ("Ursidy", "Ursids", 270.7, 10, 217.0, 76.0),
)

CONSTELLATIONS_CS = {
    "And": "Andromeda", "Ant": "Vývěva", "Aps": "Rajka", "Aqr": "Vodnář", "Aql": "Orel",
    "Ara": "Oltář", "Ari": "Beran", "Aur": "Vozka", "Boo": "Pastýř", "Cae": "Rydlo",
    "Cam": "Žirafa", "Cnc": "Rak", "CVn": "Honicí psi", "CMa": "Velký pes",
    "CMi": "Malý pes", "Cap": "Kozoroh", "Car": "Lodní kýl", "Cas": "Kasiopeja",
    "Cen": "Kentaur", "Cep": "Cefeus", "Cet": "Velryba", "Cha": "Chameleon",
    "Cir": "Kružítko", "Col": "Holubice", "Com": "Vlasy Bereniky", "CrA": "Jižní koruna",
    "CrB": "Severní koruna", "Crv": "Havran", "Crt": "Pohár", "Cru": "Jižní kříž",
    "Cyg": "Labuť", "Del": "Delfín", "Dor": "Mečoun", "Dra": "Drak", "Equ": "Koníček",
    "Eri": "Eridanus", "For": "Pec", "Gem": "Blíženci", "Gru": "Jeřáb", "Her": "Herkules",
    "Hor": "Hodiny", "Hya": "Hydra", "Hyi": "Malý vodní had", "Ind": "Indián",
    "Lac": "Ještěrka", "Leo": "Lev", "LMi": "Malý lev", "Lep": "Zajíc", "Lib": "Váhy",
    "Lup": "Vlk", "Lyn": "Rys", "Lyr": "Lyra", "Men": "Tabulová hora", "Mic": "Mikroskop",
    "Mon": "Jednorožec", "Mus": "Moucha", "Nor": "Pravítko", "Oct": "Oktant",
    "Oph": "Hadonoš", "Ori": "Orion", "Pav": "Páv", "Peg": "Pegas", "Per": "Perseus",
    "Phe": "Fénix", "Pic": "Malíř", "Psc": "Ryby", "PsA": "Jižní ryba", "Pup": "Lodní záď",
    "Pyx": "Kompas", "Ret": "Síťka", "Sge": "Šíp", "Sgr": "Střelec", "Sco": "Štír",
    "Scl": "Sochař", "Sct": "Štít", "Ser": "Had", "Sex": "Sextant", "Tau": "Býk",
    "Tel": "Dalekohled", "Tri": "Trojúhelník", "TrA": "Jižní trojúhelník", "Tuc": "Tukan",
    "UMa": "Velká medvědice", "UMi": "Malá medvědice", "Vel": "Plachty", "Vir": "Panna",
    "Vol": "Létající ryba", "Vul": "Lištička",
}


def _star_object(entry) -> Star:
    _, (hours, minutes, seconds), (degrees, arcminutes, arcseconds), _ = entry
    sign = -1.0 if degrees < 0 or arcminutes < 0 or arcseconds < 0 else 1.0
    declination = sign * (abs(degrees) + abs(arcminutes) / 60.0 + abs(arcseconds) / 3600.0)
    return Star(ra_hours=hours + minutes / 60.0 + seconds / 3600.0,
                dec_degrees=declination)


def _decimal_comma(value: float, digits: int, english: bool) -> str:
    text = f"{value:.{digits}f}"
    return text if english else text.replace(".", ",")


class Kp:
    """Index Kp z NOAA SWPC, sdileny vsemi hodinami."""

    def __init__(self, user_agent: str) -> None:
        self._user_agent = user_agent
        self._lock = threading.Lock()
        self._fetched = 0.0
        self._running = False
        self._now: float | None = None
        self._now_at = 0
        self._max: float | None = None
        self._max_at = 0

    def _get(self, url: str):
        request = urllib.request.Request(url, headers={"User-Agent": self._user_agent})
        with urllib.request.urlopen(request, timeout=KP_TIMEOUT_SECONDS) as response:
            blob = response.read(MAX_KP_BYTES + 1)
        if len(blob) > MAX_KP_BYTES:
            raise ValueError("odpoved NOAA je prilis velka")
        return json.loads(blob)

    @staticmethod
    def _epoch(tag: str) -> int:
        return calendar.timegm(time.strptime(tag[:19], "%Y-%m-%dT%H:%M:%S"))

    def refresh_soon(self, now: float) -> None:
        """Obnovi Kp na pozadi, kdyz je na rade. NOAA odpovida i nekolik
        sekund a hodiny na oblohu cekaji jen osm; odpoved proto dostanou
        s posledni znamou hodnotou a nova prijde s dalsim dotazem."""
        with self._lock:
            if self._running or now - self._fetched < KP_SECONDS:
                return
            self._fetched = now
            self._running = True
        threading.Thread(target=self._refresh, args=(now,), name="kp", daemon=True).start()

    def _refresh(self, now: float) -> None:
        try:
            self._fetch(now)
        finally:
            with self._lock:
                self._running = False

    def _fetch(self, now: float) -> None:
        try:
            minutes = self._get(KP_NOW_URL)
            last = minutes[-1]
            current = float(last["estimated_kp"])
            current_at = self._epoch(last["time_tag"])
        except (urllib.error.URLError, OSError, ValueError, KeyError, IndexError, TypeError):
            current, current_at = None, 0
        try:
            rows = self._get(KP_FORECAST_URL)
            best, best_at = None, 0
            for row in rows:
                if not isinstance(row, dict) or row.get("observed") != "predicted":
                    continue
                at = self._epoch(row["time_tag"])
                # Tri hodiny od zacatku intervalu, na ktery predpoved plati.
                if at + 3 * 3600 < now or at > now + 24 * 3600:
                    continue
                value = float(row["kp"])
                if best is None or value > best:
                    best, best_at = value, at
        except (urllib.error.URLError, OSError, ValueError, KeyError, TypeError):
            best, best_at = None, 0
        with self._lock:
            if current is not None:
                self._now, self._now_at = current, current_at
            if best is not None:
                self._max, self._max_at = best, best_at

    def values(self, now: float) -> dict:
        with self._lock:
            out = {}
            if self._now is not None and now - self._now_at < KP_MAX_AGE_SECONDS:
                out["kp"] = round(self._now, 2)
            if self._max is not None:
                out["kpMax"] = round(self._max, 2)
                out["kpMaxAt"] = self._max_at
            return out


class Sky:
    def __init__(self, cache_dir: str, user_agent: str) -> None:
        self._cache_dir = cache_dir
        self._user_agent = user_agent
        self._ready = threading.Event()
        self._error = ""
        self._lock = threading.Lock()
        self._positions: dict = {}
        self._events: dict = {}
        self.kp = Kp(user_agent)
        self.eph = None
        self.ts = None

    # --- Start -------------------------------------------------------------
    def start(self) -> None:
        threading.Thread(target=self._load, name="ephemeris", daemon=True).start()

    def _load(self) -> None:
        try:
            loader = Loader(self._cache_dir, verbose=False)
            self.ts = loader.timescale()
            self.eph = loader(EPHEMERIS)
            self.earth = self.eph["earth"]
            self.sun = self.eph["sun"]
            self.moon = self.eph["moon"]
            self.targets = {key: self.eph[name] for key, name, *_ in BODIES}
            self.stars = [_star_object(entry) for entry in STARS]
            self.moon_stars = {name: self.stars[STAR_INDEX[name]] for name in MOON_STARS}
            self.constellation_at = load_constellation_map()
            self.constellation_en = dict(load_constellation_names())
            self.star_constellations = [
                self.constellation_at(position_of_radec(star.ra.hours, star.dec.degrees))
                for star in self.stars]
            self.figures = [(abbreviation, names, _figure_center(
                [self.stars[STAR_INDEX[name]] for pair in pairs for name in pair]))
                for abbreviation, names, pairs in FIGURES]
            self._ready.set()
        except Exception as error:  # noqa: BLE001 - chyba se hlasi v odpovedi
            self._error = f"{type(error).__name__}: {error}"[:120]

    def ready(self) -> bool:
        return self._ready.is_set()

    def _constellation_name(self, abbreviation: str, english: bool) -> str:
        return (self.constellation_en.get(abbreviation, abbreviation) if english
                else CONSTELLATIONS_CS.get(abbreviation, abbreviation))

    def status(self) -> dict:
        return {"ready": self.ready(), "error": self._error,
                "positions": len(self._positions), "events": len(self._events)}

    # --- Odpoved -----------------------------------------------------------
    def answer(self, latitude: float, longitude: float, english: bool, now: float) -> dict:
        self.kp.refresh_soon(now)
        key = (round(latitude, 1), round(longitude, 1), english)
        with self._lock:
            cached = self._positions.get(key)
            if cached is None or not 0 <= now - cached[0] < POSITIONS_SECONDS:
                cached = (now, {
                    "bodies": self._bodies(latitude, longitude, english, now),
                    **self._moon_track(latitude, longitude, now),
                    **self._dark_window(latitude, longitude, now),
                    **self._day_window(latitude, longitude, now),
                })
                radiant = self._active_radiant(now, english)
                if radiant is not None:
                    cached[1]["radiant"] = radiant
                self._store(self._positions, key, cached)
            events = self._events.get(key)
            if events is None or not 0 <= now - events[0] < EVENTS_SECONDS:
                events = (now, self._compute_events(latitude, longitude, english, now))
                self._store(self._events, key, events)
        answer = {
            "v": 1,
            "time": round(now),
            **cached[1],
            "stars": self._star_payload(),
            "starNames": [entry[0] for entry in STARS],
            "starCons": [self._constellation_name(abbreviation, english)
                         for abbreviation in self.star_constellations],
            "lines": [STAR_INDEX[name] for pair in LINES for name in pair],
            "cons": self._figure_payload(english),
            # Udalosti, ktere uz probehly, se z mezipameti neposilaji.
            "events": [event for event in events[1] if event["t"] + 6 * 3600 > now][:MAX_EVENTS],
        }
        answer.update(self.kp.values(now))
        return answer

    def _figure_payload(self, english: bool) -> list[dict]:
        out = []
        for abbreviation, names, (ra_hours, dec_degrees) in self.figures:
            if names is not None:
                name = names[1] if english else names[0]
            else:
                name = self._constellation_name(abbreviation, english)
            out.append({"n": name, "ra": round(ra_hours, 3), "dec": round(dec_degrees, 2)})
        return out

    def _moon_track(self, latitude: float, longitude: float, now: float) -> dict:
        """Poloha Mesice po pul hodine kolem zacatku tehle pulhodiny.

        "moonTrack" jde od zacatku pulhodiny dopredu, "moonPast" tesne pred
        nej zpet; dohromady je to jedna rada. Hodiny z ni kresli cely prechod
        Mesice oblohou; vysku a azimut kazdeho vzorku si dopocitaji pro jeho
        cas. Topocentricky, stejne jako Mesic sam.
        """
        step = MOON_TRACK_STEP_SECONDS
        start = int(now // step) * step
        past = MOON_PAST_HOURS * 3600 // step
        count = past + MOON_TRACK_HOURS * 3600 // step + 1
        first = start - past * step
        seconds = first + np.arange(count) * step
        times = self.ts.tt_jd(self.ts.from_datetime(_utc(first)).tt + (seconds - first) / 86400.0)
        ra, dec, _ = self._observer(latitude, longitude).at(times).observe(
            self.moon).apparent().radec(epoch="date")
        points: list[int] = []
        for hours, degrees in zip(ra.hours, dec.degrees):
            points.extend((round(float(hours) * 1000) % 24000, round(float(degrees) * 100)))
        return {"moonTrack": {"t": start, "s": step, "p": points[2 * past:]},
                "moonPast": {"t": first, "s": step, "p": points[:2 * past]}}

    def _dark_window(self, latitude: float, longitude: float, now: float) -> dict:
        """Astronomicka tma, ktera prave je nebo prijde jako prvni.

        V lete ji na padesate rovnobezce Slunce vubec nepusti; pak se nic
        neposila a hodiny radek vynechaji.
        """
        state_at = almanac.dark_twilight_day(self.eph, wgs84.latlon(latitude, longitude))
        window = self._window(state_at, 0, now)
        return {"dark": window} if window else {}

    def _day_window(self, latitude: float, longitude: float, now: float) -> dict:
        """Den od vychodu do zapadu Slunce, ktery prave je nebo prijde."""
        state_at = almanac.sunrise_sunset(self.eph, wgs84.latlon(latitude, longitude))
        window = self._window(state_at, 1, now)
        return {"day": window} if window else {}

    def _window(self, state_at, wanted: int, now: float) -> list[int] | None:
        """Prvni usek stavu wanted, ktery neskoncil, jako [od, do]."""
        t0 = self.ts.from_datetime(_utc(now - 16 * 3600))
        t1 = self.ts.from_datetime(_utc(now + 32 * 3600))
        times, states = almanac.find_discrete(t0, t1, state_at)
        # Okraje hledani se berou jako hranice, useky na nich ale nejsou cele,
        # tak se nepouziji.
        edges = [(_unix(moment), int(state)) for moment, state in zip(times, states)]
        for index, (start, state) in enumerate(edges):
            if state != wanted or index + 1 >= len(edges):
                continue
            end = edges[index + 1][0]
            if end > now:
                return [start, end]
        return None

    def _active_radiant(self, now: float, english: bool) -> dict | None:
        """Radiant roje, ktery je pobliz maxima; jinak nic."""
        t0 = self.ts.from_datetime(_utc(now - RADIANT_DAYS_AFTER * 86400))
        best = self._shower_peak(t0, RADIANT_DAYS_BEFORE + RADIANT_DAYS_AFTER)
        if best is None:
            return None
        czech, english_name, _, _, ra_degrees, dec_degrees = best[1]
        return {"n": english_name if english else czech,
                "ra": round(ra_degrees / 15.0, 3), "dec": dec_degrees}

    @staticmethod
    def _store(table: dict, key, value) -> None:
        if key not in table and len(table) >= CACHE_ENTRIES:
            oldest = min(table, key=lambda existing: table[existing][0])
            del table[oldest]
        table[key] = value

    def _star_payload(self) -> list[int]:
        # Plochy seznam celych cisel: rektascenze v tisicinach hodiny,
        # deklinace v setinach stupne, velikost v desetinach.
        payload: list[int] = []
        for entry, star in zip(STARS, self.stars):
            payload.extend((round(star.ra.hours * 1000), round(star.dec.degrees * 100),
                            round(entry[3] * 10)))
        return payload

    def _observer(self, latitude: float, longitude: float):
        return self.earth + wgs84.latlon(latitude, longitude)

    def _bodies(self, latitude: float, longitude: float, english: bool, now: float) -> list[dict]:
        t = self.ts.from_datetime(_utc(now))
        observer = self._observer(latitude, longitude)
        here = observer.at(t)
        out = []
        for key, _, czech, english_name, kind in BODIES:
            target = self.targets[key]
            apparent = here.observe(target).apparent()
            ra, dec, distance = apparent.radec(epoch="date")
            body = {
                "id": key,
                "n": english_name if english else czech,
                "k": kind,
                "ra": round(ra.hours, 4),
                "dec": round(dec.degrees, 3),
            }
            if kind == 1:
                body["dist"] = round(distance.km)
                body["ill"] = round(float(almanac.fraction_illuminated(self.eph, "moon", t)), 3)
            else:
                body["au"] = round(distance.au, 3)
            if kind == 2:
                try:
                    magnitude = float(planetary_magnitude(self.earth.at(t).observe(target)))
                    if math.isfinite(magnitude):
                        body["mag"] = round(magnitude, 1)
                except (ValueError, TypeError):
                    pass
            body["con"] = self._constellation_name(self.constellation_at(apparent), english)
            rise, set_ = self._rise_set(observer, target, t, key)
            if rise:
                body["rise"] = rise
            if set_:
                body["set"] = set_
            out.append(body)
        return out

    def _rise_set(self, observer, target, t, key) -> tuple[int, int]:
        """Nejblizsi vychod a zapad v pristich 24 hodinach, unixove sekundy."""
        t1 = self.ts.tt_jd(t.tt + 1.0)
        # Slunce s refrakci a kotoucem, Mesic s paralaxou resi skyfield sam
        # podle polomeru a vzdalenosti; planety jsou body.
        horizon = -0.8333 if key in ("sun", "moon") else -0.5667
        rise = set_ = 0
        try:
            times, _ = almanac.find_risings(observer, target, t, t1, horizon_degrees=horizon)
            if len(times):
                rise = _unix(times[0])
            times, _ = almanac.find_settings(observer, target, t, t1, horizon_degrees=horizon)
            if len(times):
                set_ = _unix(times[0])
        except (ValueError, AttributeError):
            pass
        return rise, set_

    # --- Udalosti ------------------------------------------------------------
    def _compute_events(self, latitude: float, longitude: float, english: bool,
                        now: float) -> list[dict]:
        observer = self._observer(latitude, longitude)
        t0 = self.ts.from_datetime(_utc(now))
        eclipse = self._next_eclipse(observer, t0, english)
        common = [event for event in (
            self._next_moon_conjunction(observer, t0, english),
            self._next_shower(t0, english),
            self._next_planet_event(observer, t0, english),
        ) if event is not None]
        common.sort(key=lambda event: event["t"])
        # Zatmeni je vzacne, takze ma radek jisty: jinak by ho vytlacily tri
        # bezne udalosti, ktere jsou na rade driv. Jen ne rok dopredu: male
        # zatmeni se hlasi mesic predem, velke tri mesice.
        if eclipse is not None:
            major = eclipse.pop("major")
            days = (eclipse["t"] - now) / 86400.0
            if days <= (MAJOR_ECLIPSE_DAYS if major else ECLIPSE_DAYS):
                common = common[:MAX_EVENTS - 1] + [eclipse]
        return sorted(common, key=lambda event: event["t"])[:MAX_EVENTS]

    def _sample(self, t0, days: float, step_hours: float):
        count = int(days * 24 / step_hours) + 1
        return self.ts.tt_jd(t0.tt + np.arange(count) * step_hours / 24.0)

    @staticmethod
    def _separation(a, b) -> np.ndarray:
        return a.separation_from(b).degrees

    def _minima(self, times, separation: np.ndarray, limit: float, refine) -> list[tuple]:
        """Mistni minima pod limitem, zpresnena funkci refine(index)."""
        out = []
        for index in range(1, len(separation) - 1):
            if separation[index] <= separation[index - 1] and separation[index] < separation[index + 1]:
                if separation[index] < limit:
                    out.append(refine(index))
        return out

    def _refine_minimum(self, function, t_center, half_width_days: float, steps: int = 121):
        times = self.ts.tt_jd(t_center.tt + np.linspace(-half_width_days, half_width_days, steps))
        values = function(times)
        best = int(np.argmin(values))
        return times[best], float(values[best])

    def _next_moon_conjunction(self, observer, t0, english: bool) -> dict | None:
        times = self._sample(t0, MOON_CONJUNCTION_DAYS, 1.0)
        here = observer.at(times)
        moon = here.observe(self.moon).apparent()
        candidates = []
        for key in NAKED_EYE:
            target = self.targets[key]

            def separation(sample_times, target=target):
                position = observer.at(sample_times)
                return self._separation(position.observe(self.moon).apparent(),
                                        position.observe(target).apparent())

            values = self._separation(moon, here.observe(target).apparent())
            for index in range(1, len(values) - 1):
                if values[index] <= values[index - 1] and values[index] < values[index + 1] \
                        and values[index] < MOON_CONJUNCTION_DEGREES + 1.0:
                    moment, degrees = self._refine_minimum(separation, times[index], 1.0 / 24.0)
                    if degrees < MOON_CONJUNCTION_DEGREES:
                        candidates.append((moment, degrees, key, False))
        for name, star in self.moon_stars.items():
            values = self._separation(moon, here.observe(star).apparent())
            for index in range(1, len(values) - 1):
                if values[index] <= values[index - 1] and values[index] < values[index + 1] \
                        and values[index] < MOON_STAR_DEGREES + 1.0:

                    def separation(sample_times, star=star):
                        position = observer.at(sample_times)
                        return self._separation(position.observe(self.moon).apparent(),
                                                position.observe(star).apparent())

                    moment, degrees = self._refine_minimum(separation, times[index], 1.0 / 24.0)
                    if degrees < MOON_STAR_DEGREES:
                        candidates.append((moment, degrees, name, True))
        if not candidates:
            return None
        moment, degrees, key, star = min(candidates, key=lambda item: item[0].tt)
        amount = _decimal_comma(degrees, 1, english)
        if star:
            czech, english_name = MOON_STARS[key]
            text = f"Moon {amount}° from {english_name}" if english else f"Měsíc {amount}° od {czech}"
        else:
            text = (f"Moon {amount}° from {self._name(key, True)}" if english
                    else f"Měsíc {amount}° od {CZECH_FROM[key]}")
        return {"t": _unix(moment), "tm": 1, "k": "conj", "x": text}

    def _name(self, key: str, english: bool) -> str:
        for body_key, _, czech, english_name, _ in BODIES:
            if body_key == key:
                return english_name if english else czech
        return key

    def _next_planet_event(self, observer, t0, english: bool) -> dict | None:
        times = self._sample(t0, PLANET_EVENT_DAYS, 6.0)
        earth = self.earth.at(times)
        sun = earth.observe(self.sun).apparent()
        positions = {key: earth.observe(self.targets[key]).apparent() for key in OUTER + INNER}
        candidates = []
        # Planeta k planete.
        visible = [key for key in NAKED_EYE]
        for first in range(len(visible)):
            for second in range(first + 1, len(visible)):
                a, b = visible[first], visible[second]
                values = self._separation(positions[a], positions[b])
                for index in range(1, len(values) - 1):
                    if values[index] <= values[index - 1] and values[index] < values[index + 1] \
                            and values[index] < PLANET_CONJUNCTION_DEGREES:
                        # Dvojici tesne u Slunce stejne nikdo neuvidi.
                        if self._separation(sun[index], positions[a][index]) < 15.0:
                            continue
                        candidates.append((times[index], "pair", (a, b), float(values[index])))
        # Opozice vnejsich planet a nejvetsi elongace vnitrnich.
        for key in OUTER + INNER:
            elongation = self._separation(sun, positions[key])
            for index in range(1, len(elongation) - 1):
                if elongation[index] >= elongation[index - 1] and elongation[index] > elongation[index + 1]:
                    if key in OUTER and elongation[index] > 170.0:
                        candidates.append((times[index], "opposition", key, float(elongation[index])))
                    elif key in INNER:
                        east = self._east_of_sun(sun[index], positions[key][index])
                        candidates.append((times[index], "elongation", (key, east),
                                           float(elongation[index])))
        if not candidates:
            return None
        moment, kind, subject, degrees = min(candidates, key=lambda item: item[0].tt)
        if kind == "pair":
            a, b = subject
            amount = _decimal_comma(degrees, 1, english)
            text = (f"{self._name(a, True)} {amount}° from {self._name(b, True)}" if english
                    else f"{self._name(a, False)} {amount}° od {CZECH_FROM[b]}")
        elif kind == "opposition":
            text = (f"{self._name(subject, True)} at opposition" if english
                    else f"{self._name(subject, False)} v opozici")
        else:
            key, east = subject
            when = ("evening" if east else "morning") if english else ("večer" if east else "ráno")
            text = (f"{self._name(key, True)} greatest elongation {round(degrees)}° ({when})"
                    if english else
                    f"{self._name(key, False)} nejdál od Slunce, {round(degrees)}° ({when})")
        return {"t": _unix(moment), "tm": 0, "k": kind, "x": text}

    @staticmethod
    def _east_of_sun(sun, planet) -> bool:
        """Planeta vychodne od Slunce je videt vecer."""
        _, sun_lon, _ = sun.frame_latlon(ecliptic_J2000_frame)
        _, planet_lon, _ = planet.frame_latlon(ecliptic_J2000_frame)
        difference = (planet_lon.degrees - sun_lon.degrees) % 360.0
        return difference < 180.0

    def _next_shower(self, t0, english: bool) -> dict | None:
        best = self._shower_peak(t0, SHOWER_DAYS)
        if best is None:
            return None
        moment, (czech, english_name, _, zhr, _, _) = best
        moon = round(float(almanac.fraction_illuminated(self.eph, "moon", moment)) * 100)
        text = (f"{english_name}, up to {zhr}/h, Moon {moon} %" if english
                else f"{czech} až {zhr}/h, Měsíc {moon} %")
        return {"t": _unix(moment), "tm": 0, "k": "meteor", "x": text}

    def _shower_peak(self, t0, days: float):
        """Nejblizsi maximum roje od t0 do t0 + days: (okamzik, radek SHOWERS)."""
        times = self._sample(t0, days + 1, 24.0)
        sun = self.earth.at(times).observe(self.sun).apparent()
        _, longitude, _ = sun.frame_latlon(ecliptic_J2000_frame)
        degrees = longitude.degrees
        best = None
        for shower in SHOWERS:
            offset = (degrees - shower[2] + 180.0) % 360.0 - 180.0
            for index in range(len(offset) - 1):
                if offset[index] < 0.0 <= offset[index + 1]:
                    fraction = -offset[index] / (offset[index + 1] - offset[index])
                    moment = self.ts.tt_jd(times[index].tt + fraction)
                    if moment.tt <= t0.tt + days and (best is None or moment.tt < best[0].tt):
                        best = (moment, shower)
                    break
        return best

    def _next_eclipse(self, observer, t0, english: bool) -> dict | None:
        t1 = self.ts.tt_jd(t0.tt + MAJOR_ECLIPSE_DAYS)
        found = []
        lunar = self._next_lunar_eclipse(observer, t0, t1, english)
        if lunar is not None:
            found.append(lunar)
        solar = self._next_solar_eclipse(observer, t0, t1, english)
        if solar is not None:
            found.append(solar)
        return min(found, key=lambda event: event["t"]) if found else None

    def _next_lunar_eclipse(self, observer, t0, t1, english: bool) -> dict | None:
        times, kinds, details = eclipselib.lunar_eclipses(t0, t1, self.eph)
        for index, moment in enumerate(times):
            kind = int(kinds[index])
            # Polostinove zatmeni okem skoro nejde poznat.
            if kind == 0:
                continue
            altitude, _, _ = observer.at(moment).observe(self.moon).apparent().altaz()
            if altitude.degrees <= 0.0:
                continue
            if kind == 2:
                text = "Total lunar eclipse" if english else "Úplné zatmění Měsíce"
                major = True
            else:
                magnitude = round(float(details["umbral_magnitude"][index]) * 100)
                text = (f"Partial lunar eclipse {magnitude} %" if english
                        else f"Částečné zatmění Měsíce {magnitude} %")
                major = magnitude >= MAJOR_ECLIPSE_PERCENT
            return {"t": _unix(moment), "tm": 1, "k": "eclipse", "x": text, "major": major}
        return None

    def _next_solar_eclipse(self, observer, t0, t1, english: bool) -> dict | None:
        """Zatmeni Slunce videne primo z mista hodin.

        Kolem kazdeho novu se po minute porovna vzdalenost stredu kotoucu se
        souctem jejich polomeru, jak je vidi pozorovatel. Pocita se jen chvile,
        kdy je Slunce nad obzorem, a z ni nejvetsi zakryti.
        """
        new_moons, phases = almanac.find_discrete(t0, t1, almanac.moon_phases(self.eph))
        for moment, phase in zip(new_moons, phases):
            if int(phase) != 0:
                continue
            times = self.ts.tt_jd(moment.tt + np.arange(-360, 361) / 1440.0)
            here = observer.at(times)
            sun = here.observe(self.sun).apparent()
            moon = here.observe(self.moon).apparent()
            separation = np.radians(sun.separation_from(moon).degrees)
            sun_radius = np.arcsin(SUN_RADIUS_KM / sun.distance().km)
            moon_radius = np.arcsin(MOON_RADIUS_KM / moon.distance().km)
            altitude = sun.altaz()[0].degrees
            overlapping = (separation < sun_radius + moon_radius) & (altitude > 0.0)
            if not overlapping.any():
                continue
            obscuration = _obscuration(separation, sun_radius, moon_radius)
            obscuration[~overlapping] = 0.0
            best = int(np.argmax(obscuration))
            if separation[best] <= abs(moon_radius[best] - sun_radius[best]):
                total = moon_radius[best] >= sun_radius[best]
                text = ((("Total" if total else "Annular") + " solar eclipse") if english
                        else ("Úplné" if total else "Prstencové") + " zatmění Slunce")
                major = True
            else:
                percent = max(1, round(float(obscuration[best]) * 100))
                text = (f"Partial solar eclipse {percent} %" if english
                        else f"Částečné zatmění Slunce {percent} %")
                major = percent >= MAJOR_ECLIPSE_PERCENT
            return {"t": _unix(times[best]), "tm": 1, "k": "eclipse", "x": text, "major": major}
        return None


def _figure_center(stars) -> tuple[float, float]:
    """Stred obrazce: prumer jednotkovych vektoru jeho hvezd, jako (h, °)."""
    vectors = []
    for star in stars:
        ra = math.radians(star.ra.hours * 15.0)
        dec = math.radians(star.dec.degrees)
        vectors.append((math.cos(dec) * math.cos(ra), math.cos(dec) * math.sin(ra), math.sin(dec)))
    x, y, z = (sum(axis) / len(vectors) for axis in zip(*vectors))
    ra_hours = (math.degrees(math.atan2(y, x)) / 15.0) % 24.0
    return ra_hours, math.degrees(math.atan2(z, math.hypot(x, y)))


def _obscuration(distance: np.ndarray, sun: np.ndarray, moon: np.ndarray) -> np.ndarray:
    """Zakryta cast plochy slunecniho kotouce (prunik dvou kruhu)."""
    out = np.zeros_like(distance)
    inside = distance <= np.abs(moon - sun)
    out[inside] = np.minimum(1.0, (moon[inside] / sun[inside]) ** 2)
    partial = (distance < sun + moon) & ~inside
    d, r1, r2 = distance[partial], sun[partial], moon[partial]
    alpha = np.arccos(np.clip((d * d + r1 * r1 - r2 * r2) / (2 * d * r1), -1.0, 1.0))
    beta = np.arccos(np.clip((d * d + r2 * r2 - r1 * r1) / (2 * d * r2), -1.0, 1.0))
    area = (r1 * r1 * alpha + r2 * r2 * beta
            - 0.5 * np.sqrt(np.clip((-d + r1 + r2) * (d + r1 - r2) * (d - r1 + r2) * (d + r1 + r2),
                                    0.0, None)))
    out[partial] = area / (math.pi * r1 * r1)
    return out


def _utc(epoch: float) -> datetime:
    return datetime.fromtimestamp(epoch, tz=timezone.utc)


def _unix(moment) -> int:
    return round(moment.utc_datetime().timestamp())
