#!/usr/bin/env python3
"""Testy nocni oblohy: python3 -m unittest infra/satellites/test_sky.py

Vypocty nad efemeridami potrebuji de421.bsp (17 MB). Test ho hleda
v adresari z SKY_EPHEMERIS_DIR a bez nej se ty casti preskoci; zbytek bezi
bez site i bez souboru.
"""
import math
import os
import sys
import time
import unittest
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))

import sky  # noqa: E402

EPHEMERIS_DIR = os.environ.get("SKY_EPHEMERIS_DIR", "")
HAVE_EPHEMERIS = bool(EPHEMERIS_DIR) and Path(EPHEMERIS_DIR, sky.EPHEMERIS).is_file()
ONDREJOV = (49.90461, 14.7842)


class ObscurationTest(unittest.TestCase):
    def test_no_overlap(self):
        out = sky._obscuration(np.array([0.02]), np.array([0.0047]), np.array([0.0045]))
        self.assertEqual(out[0], 0.0)

    def test_moon_inside_sun_is_annular_fraction(self):
        out = sky._obscuration(np.array([0.0]), np.array([0.0048]), np.array([0.0044]))
        self.assertAlmostEqual(out[0], (0.0044 / 0.0048) ** 2)

    def test_total(self):
        out = sky._obscuration(np.array([0.0001]), np.array([0.0046]), np.array([0.0049]))
        self.assertEqual(out[0], 1.0)

    def test_half_offset_is_partial(self):
        out = sky._obscuration(np.array([0.0046]), np.array([0.0046]), np.array([0.0046]))
        # Dva stejne kruhy, stred jednoho na obvodu druheho: 39,1 % plochy.
        self.assertAlmostEqual(out[0], (2 * math.pi / 3 - math.sqrt(3) / 2) / math.pi, places=6)


class TableTest(unittest.TestCase):
    def test_lines_use_known_stars(self):
        for first, second in sky.LINES:
            self.assertIn(first, sky.STAR_INDEX)
            self.assertIn(second, sky.STAR_INDEX)

    def test_figures_cover_all_lines(self):
        self.assertEqual(len(sky.LINES), sum(len(pairs) for _, _, pairs in sky.FIGURES))
        for abbreviation, _, _ in sky.FIGURES:
            self.assertIn(abbreviation, sky.CONSTELLATIONS_CS)

    def test_figure_center_wraps_around_zero_hours(self):
        # Hvezdy na 23,9 h a 0,1 h: stred na nule, ne na dvanacti hodinach.
        ra, dec = sky._figure_center([sky.Star(ra_hours=23.9, dec_degrees=10.0),
                                      sky.Star(ra_hours=0.1, dec_degrees=10.0)])
        self.assertAlmostEqual(min(ra, 24.0 - ra), 0.0, places=6)
        self.assertAlmostEqual(dec, 10.0, delta=0.01)

    def test_star_names_unique(self):
        self.assertEqual(len(sky.STAR_INDEX), len(sky.STARS))

    def test_all_constellations_named_in_czech(self):
        self.assertEqual(len(sky.CONSTELLATIONS_CS), 88)

    def test_south_declination_with_zero_degrees(self):
        # Mintaka ma deklinaci -0° 17' 57": znamenko nese minuta, ne stupen.
        star = sky._star_object(sky.STARS[sky.STAR_INDEX["Mintaka"]])
        self.assertAlmostEqual(star.dec.degrees, -(17 / 60 + 57 / 3600))

    def test_decimal_comma(self):
        self.assertEqual(sky._decimal_comma(1.25, 1, False), "1,2")
        self.assertEqual(sky._decimal_comma(1.25, 1, True), "1.2")


class KpTest(unittest.TestCase):
    def test_now_and_forecast_maximum(self):
        kp = sky.Kp("test")
        now = 1790000000
        minutes = [{"time_tag": "2026-09-21T13:52:00", "estimated_kp": 2.33}]
        forecast = [
            {"time_tag": "2026-09-21T12:00:00", "kp": 3.0, "observed": "estimated"},
            {"time_tag": "2026-09-21T15:00:00", "kp": 5.67, "observed": "predicted"},
            {"time_tag": "2026-09-21T18:00:00", "kp": 6.33, "observed": "predicted"},
            # Za vic nez den; do maxima nepatri.
            {"time_tag": "2026-09-23T18:00:00", "kp": 8.0, "observed": "predicted"},
        ]
        kp._get = lambda url: minutes if url == sky.KP_NOW_URL else forecast
        kp._fetch(now)
        values = kp.values(now)
        self.assertEqual(values["kp"], 2.33)
        self.assertEqual(values["kpMax"], 6.33)
        self.assertEqual(values["kpMaxAt"], 1790013600)

    def test_stale_now_is_dropped(self):
        kp = sky.Kp("test")
        kp._get = lambda url: ([{"time_tag": "2026-09-21T10:00:00", "estimated_kp": 4}]
                               if url == sky.KP_NOW_URL else [])
        kp._fetch(1790000000)
        self.assertNotIn("kp", kp.values(1790000000 + 2 * 3600))

    def test_utc_parse_ignores_local_dst(self):
        self.assertEqual(sky.Kp._epoch("2026-09-21T00:00:00"), 1789948800)


@unittest.skipUnless(HAVE_EPHEMERIS, "bez de421.bsp v SKY_EPHEMERIS_DIR")
class EphemerisTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.sky = sky.Sky(EPHEMERIS_DIR, "test")
        cls.sky._load()
        cls.sky.kp.refresh_soon = lambda now: None

    def answer(self, when, english=False):
        return self.sky.answer(*ONDREJOV, english, when)

    def test_equinox_sun(self):
        # 23. 9. 2026 00:05 UTC je podzimni rovnodennost.
        bodies = {body["id"]: body for body in self.answer(1790121900)["bodies"]}
        sun = bodies["sun"]
        self.assertAlmostEqual(sun["ra"], 12.0, delta=0.02)
        self.assertAlmostEqual(sun["dec"], 0.0, delta=0.05)
        self.assertEqual(sun["con"], "Panna")

    def test_every_body_present_with_position(self):
        answer = self.answer(1790000000)
        self.assertEqual([body["id"] for body in answer["bodies"]],
                         [key for key, *_ in sky.BODIES])
        self.assertEqual(len(answer["stars"]), 3 * len(sky.STARS))
        self.assertEqual(len(answer["lines"]), 2 * len(sky.LINES))

    def test_partial_solar_eclipse_2027_from_czechia(self):
        # 2. 8. 2027 je v Cesku castecne zatmeni Slunce kolem 11 h SELC. Rok
        # predem se jeste nehlasi, dva tydny predem uz ano.
        events = self.sky._compute_events(*ONDREJOV, False, 1790000000)
        self.assertEqual([event for event in events if event["k"] == "eclipse"], [])
        events = self.sky._compute_events(*ONDREJOV, False, 1816041600)  # 20. 7. 2027
        eclipse = [event for event in events if event["k"] == "eclipse"]
        self.assertEqual(len(eclipse), 1)
        self.assertNotIn("major", eclipse[0])
        self.assertTrue(eclipse[0]["x"].startswith("Částečné zatmění Slunce"))
        moment = time.gmtime(eclipse[0]["t"])
        self.assertEqual((moment.tm_year, moment.tm_mon, moment.tm_mday), (2027, 8, 2))

    def test_perseids_peak_in_august(self):
        when = 1785600000  # 1. 8. 2026
        observer = self.sky._observer(*ONDREJOV)
        t0 = self.sky.ts.from_datetime(sky._utc(when))
        shower = self.sky._next_shower(t0, False)
        self.assertTrue(shower["x"].startswith("Perseidy"))
        moment = time.gmtime(shower["t"])
        self.assertEqual((moment.tm_mon, moment.tm_mday in (12, 13)), (8, True))
        del observer

    def test_star_names_and_constellations(self):
        answer = self.answer(1790000000)
        self.assertEqual(len(answer["starNames"]), len(sky.STARS))
        self.assertEqual(len(answer["starCons"]), len(sky.STARS))
        index = sky.STAR_INDEX["Vega"]
        self.assertEqual((answer["starNames"][index], answer["starCons"][index]), ("Vega", "Lyra"))
        self.assertEqual(answer["starCons"][sky.STAR_INDEX["Dubhe"]], "Velká medvědice")

    def test_figure_labels(self):
        figures = {entry["n"]: entry for entry in self.answer(1790000000)["cons"]}
        self.assertEqual(len(figures), len(sky.FIGURES))
        orion = figures["Orion"]
        self.assertAlmostEqual(orion["ra"], 5.55, delta=0.2)
        self.assertAlmostEqual(orion["dec"], 0.0, delta=4.0)
        self.assertIn("Velký vůz", figures)
        english = {entry["n"] for entry in self.answer(1790000000, english=True)["cons"]}
        self.assertIn("Big Dipper", english)
        self.assertIn("Swan" if "Swan" in english else "Cygnus", english)

    def test_moon_track_starts_at_current_moon(self):
        answer = self.answer(1790000000)
        track = answer["moonTrack"]
        self.assertEqual(track["t"], 1790000000 // 1800 * 1800)
        self.assertEqual(len(track["p"]), 2 * (sky.MOON_TRACK_HOURS * 2 + 1))
        moon = answer["bodies"][1]
        # Za 800 s od zacatku pulhodiny ujde Mesic zlomek stupne.
        self.assertAlmostEqual(track["p"][0] / 1000.0, moon["ra"], delta=0.02)
        self.assertAlmostEqual(track["p"][1] / 100.0, moon["dec"], delta=0.3)
        # Za 28 hodin se posune na vychod asi o 15 stupnu, tedy o hodinu.
        shift = (track["p"][-2] - track["p"][0]) / 1000.0 % 24.0
        self.assertGreater(shift, 0.7)
        self.assertLess(shift, 1.3)
        # Minulost konci tesne pred zacatkem drahy, se stejnym krokem.
        past = answer["moonPast"]
        self.assertEqual(past["s"], track["s"])
        self.assertEqual(len(past["p"]), 2 * sky.MOON_PAST_HOURS * 2)
        self.assertEqual(past["t"] + len(past["p"]) // 2 * past["s"], track["t"])
        # Za pul hodiny ujde Mesic asi ctvrt stupne, tedy setinu hodiny.
        self.assertAlmostEqual((track["p"][0] - past["p"][-2]) / 1000.0, 0.018, delta=0.012)

    def test_dark_window_in_autumn(self):
        # 21. 9. 2026 14:00 UTC: tma prijde vecer a skonci rano.
        dark = self.answer(1789999200)["dark"]
        start, end = (time.gmtime(value) for value in dark)
        self.assertEqual((start.tm_mday, start.tm_hour), (21, 18))
        self.assertEqual((end.tm_mday, end.tm_hour), (22, 2))

    def test_dark_window_while_dark(self):
        now = 1790040000  # 22. 9. 2026 01:20 UTC
        start, end = self.answer(now)["dark"]
        self.assertLess(start, now)
        self.assertGreater(end, now)

    def test_day_window_before_sunrise(self):
        # 21. 9. 2026 02:00 UTC: den prijde rano a skonci vecer, 12 hodin.
        start, end = self.answer(1789956000)["day"]
        self.assertEqual((time.gmtime(start).tm_mday, time.gmtime(start).tm_hour), (21, 4))
        self.assertEqual((time.gmtime(end).tm_mday, time.gmtime(end).tm_hour), (21, 17))

    def test_day_window_while_day(self):
        now = 1789999200  # 21. 9. 2026 14:00 UTC
        start, end = self.answer(now)["day"]
        self.assertLess(start, now)
        self.assertGreater(end, now)
        self.assertAlmostEqual((end - start) / 3600.0, 12.2, delta=0.3)

    def test_no_astronomical_dark_at_midsummer(self):
        self.assertNotIn("dark", self.answer(1781870400))  # 19. 6. 2026 12:00 UTC

    def test_radiant_only_near_peak(self):
        self.assertNotIn("radiant", self.answer(1790000000))
        # Perseidy vrcholi 12.-13. 8.; 11. 8. 2026 je radiant na obloze.
        radiant = self.answer(1786449600)["radiant"]  # 11. 8. 2026 12:00 UTC
        self.assertEqual(radiant["n"], "Perseidy")
        self.assertAlmostEqual(radiant["ra"], 3.2, delta=0.01)
        self.assertEqual(radiant["dec"], 58.0)

    def test_english(self):
        answer = self.answer(1790000000, english=True)
        self.assertEqual(answer["bodies"][1]["n"], "Moon")


if __name__ == "__main__":
    unittest.main()
