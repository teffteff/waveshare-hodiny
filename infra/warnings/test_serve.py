#!/usr/bin/env python3
"""Testy vystrah bez site: python3 -m unittest infra/warnings/test_serve.py"""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import serve  # noqa: E402

NOW = 1782680000.0  # 28. 6. 2026 21:33 UTC, mezi onset a expires nize

CAP = """<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<alert xmlns="urn:oasis:names:tc:emergency:cap:1.2">
  <identifier>2.49.0.0.203.0.CZ.260628205342.XOCZ50_OKPR_000300</identifier>
  <sent>2026-06-28T22:53:42+02:00</sent>
  <msgType>Update</msgType>
  <info>
    <language>cs</language>
    <event>Žádná výstraha před větrem</event>
    <severity>Minor</severity>
    <area><areaDesc>Hlavní město Praha</areaDesc>
      <geocode><valueName>CISORP</valueName><value>1100</value></geocode></area>
  </info>
  <info>
    <language>en-GB</language>
    <event>Minor Wind Warning</event>
    <severity>Minor</severity>
  </info>
  <info>
    <language>cs</language>
    <event>Velmi silné bouřky</event>
    <severity>Severe</severity>
    <eventCode><valueName>SIVS</valueName><value>X.2</value></eventCode>
    <eventCode><valueName>HPPS</valueName><value>X.2</value></eventCode>
    <onset>2026-06-29T11:00:00+02:00</onset>
    <expires>2026-06-30T00:00:00+02:00</expires>
    <parameter><valueName>awareness_level</valueName><value>3; orange; Severe</value></parameter>
    <parameter><valueName>awareness_type</valueName><value>3; Thunderstorm</value></parameter>
    <area><areaDesc>Středočeský kraj (Černošice, Říčany)</areaDesc>
      <geocode><valueName>CISORP</valueName><value>2105</value></geocode>
      <geocode><valueName>EMMA_ID</valueName><value>CZ02105</value></geocode>
      <geocode><valueName>CISORP</valueName><value>2122</value></geocode></area>
  </info>
  <info>
    <language>en-GB</language>
    <event>Severe Thunderstorms</event>
    <severity>Severe</severity>
  </info>
  <info>
    <language>cs</language>
    <event>Extrémně vysoké teploty – odpoledne</event>
    <severity>Extreme</severity>
    <eventCode><valueName>SIVS</valueName><value>I.3</value></eventCode>
    <onset>2026-06-28T22:51:35+02:00</onset>
    <expires>2026-06-29T00:00:00+02:00</expires>
    <parameter><valueName>awareness_level</valueName><value>4; red; Extreme</value></parameter>
    <parameter><valueName>awareness_type</valueName><value>5; high-temperature</value></parameter>
    <area><areaDesc>Středočeský kraj (Černošice)</areaDesc>
      <geocode><valueName>CISORP</valueName><value>2105</value></geocode></area>
  </info>
  <info>
    <language>cs</language>
    <event>Silné bouřky</event>
    <severity>Moderate</severity>
    <eventCode><valueName>SIVS</valueName><value>X.1</value></eventCode>
    <onset>2026-06-28T12:00:00+02:00</onset>
    <expires>2026-06-28T20:00:00+02:00</expires>
    <parameter><valueName>awareness_level</valueName><value>2; yellow; Moderate</value></parameter>
    <area><areaDesc>Středočeský kraj (Černošice)</areaDesc>
      <geocode><valueName>CISORP</valueName><value>2105</value></geocode></area>
  </info>
  <info>
    <language>cs</language>
    <event>Smogová situace - troposférický ozón O3</event>
    <severity>Moderate</severity>
    <eventCode><valueName>SVRS</valueName><value>SMOGSIT.O3</value></eventCode>
    <onset>2026-06-28T22:51:35+02:00</onset>
    <parameter><valueName>awareness_level</valueName><value>2; yellow; Moderate</value></parameter>
    <area><areaDesc>Aglomerace Praha</areaDesc>
      <geocode><valueName>CISORP</valueName><value>2105</value></geocode></area>
  </info>
  <info>
    <language>cs</language>
    <event>Výhled nebezpečných jevů</event>
    <severity>Unknown</severity>
    <eventCode><valueName>SIVS</valueName><value>OUTLOOK</value></eventCode>
    <parameter><valueName>awareness_level</valueName><value>2; yellow; Moderate</value></parameter>
    <area><areaDesc>Středočeský kraj</areaDesc>
      <geocode><valueName>CISORP</valueName><value>2105</value></geocode></area>
  </info>
</alert>
""".encode("utf-8")

LISTING = """<html><body><pre><a href="../">../</a>
<a href="alert_cap_50_310920.xml">alert_cap_50_310920.xml</a>                            31-May-2026 09:20             2241194
<a href="alert_cap_50_210735.xml">alert_cap_50_210735.xml</a>                            21-Sep-2026 07:36             1550502
<a href="alert_cap_50_200940.xml">alert_cap_50_200940.xml</a>                            20-Sep-2026 09:40             1550502
<a href="alert_cap_70_151200.xml">alert_cap_70_151200.xml</a>                            15-Sep-2026 11:57               60024
</pre></body></html>"""


class ParseTest(unittest.TestCase):
    def test_keeps_only_real_warnings(self):
        sent, warnings = serve.parse_cap(CAP)
        self.assertEqual(sent, serve.parse_time("2026-06-28T22:53:42+02:00"))
        ids = [warning["id"] for warning in warnings]
        # Zadna vystraha (Minor) ani vyhled (OUTLOOK) mezi nimi nejsou.
        self.assertEqual(ids, ["SIVS X.2", "SIVS I.3", "SIVS X.1", "SVRS SMOGSIT.O3"])

    def test_reads_level_type_areas_and_english(self):
        _, warnings = serve.parse_cap(CAP)
        storm = warnings[0]
        self.assertEqual(storm["lvl"], 3)
        self.assertEqual(storm["type"], 3)
        self.assertEqual(storm["orps"], {"2105", "2122"})
        self.assertEqual(storm["en"], "Severe Thunderstorms")
        self.assertEqual(storm["on"], serve.parse_time("2026-06-29T11:00:00+02:00"))
        # Blok bez anglickeho dvojcete anglicky text nema.
        self.assertEqual(warnings[1]["en"], "")

    def test_until_revoked_has_zero_expiry(self):
        _, warnings = serve.parse_cap(CAP)
        self.assertEqual(warnings[3]["ex"], 0)

    def test_parse_time(self):
        self.assertEqual(serve.parse_time("2026-06-28T22:51:35+02:00"), 1782679895)
        self.assertEqual(serve.parse_time(""), 0)
        self.assertEqual(serve.parse_time("nesmysl"), 0)


class ListingTest(unittest.TestCase):
    def test_newest_by_modification_time_not_name(self):
        # 310920 je podle jmena "vetsi", ale je z kvetna.
        self.assertEqual(serve.newest_cap_name(LISTING), "alert_cap_50_210735.xml")

    def test_empty_listing(self):
        self.assertIsNone(serve.newest_cap_name("<html></html>"))


class FontTest(unittest.TestCase):
    def test_replaces_dashes_and_drops_unknown(self):
        self.assertEqual(serve.font_safe("Vysoké teploty – odpoledne ☀"),
                         "Vysoké teploty - odpoledne")

    def test_limit(self):
        self.assertEqual(serve.font_safe("a" * 100, 10), "a" * 10)


class AreasTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.areas = serve.Areas(Path(serve.__file__).with_name("orp.json"))

    def test_known_places(self):
        cases = {
            (49.90461, 14.7842): "2122",   # Ondrejov -> Ricany
            (50.0875, 14.4213): "1100",    # Praha
            (49.1951, 16.6068): "6203",    # Brno
            (50.0796, 12.3710): "4102",    # Cheb
            (49.6835, 18.6734): "8121",    # Trinec
        }
        for (latitude, longitude), code in cases.items():
            with self.subTest(code=code):
                area = self.areas.locate(latitude, longitude)
                self.assertIsNotNone(area)
                self.assertEqual(area["code"], code)

    def test_abroad_is_nothing(self):
        self.assertIsNone(self.areas.locate(48.2082, 16.3738))  # Viden
        self.assertIsNone(self.areas.locate(52.52, 13.405))     # Berlin

    def test_all_codes_present(self):
        self.assertEqual(len(self.areas.areas), 206)


class AnswerTest(unittest.TestCase):
    def setUp(self):
        self.original_snapshot = serve.feed.snapshot
        self.original_areas = serve.areas
        serve.areas = serve.Areas(Path(serve.__file__).with_name("orp.json"))
        sent, warnings = serve.parse_cap(CAP)
        serve.feed.snapshot = lambda now: (sent, warnings, True, False)

    def tearDown(self):
        serve.feed.snapshot = self.original_snapshot
        serve.areas = self.original_areas

    def test_filters_by_orp_and_sorts_by_level(self):
        # Cernosice (Dobrichovice).
        answer = serve.build_answer(49.9269, 14.2750, False, NOW)
        self.assertTrue(answer["covered"])
        self.assertEqual(answer["orp"], "2105")
        self.assertEqual(answer["area"], "Černošice")
        # Zluta bourka uz skoncila; cervena prvni, pak oranzova, pak smog.
        self.assertEqual([w["id"] for w in answer["warnings"]],
                         ["SIVS I.3", "SIVS X.2", "SVRS SMOGSIT.O3"])
        self.assertEqual(answer["warnings"][0]["ev"], "Extrémně vysoké teploty - odpoledne")

    def test_other_orp_gets_only_its_warnings(self):
        answer = serve.build_answer(49.90461, 14.7842, False, NOW)
        self.assertEqual(answer["orp"], "2122")
        self.assertEqual([w["id"] for w in answer["warnings"]], ["SIVS X.2"])

    def test_english_text(self):
        answer = serve.build_answer(49.90461, 14.7842, True, NOW)
        self.assertEqual(answer["warnings"][0]["ev"], "Severe Thunderstorms")

    def test_abroad_not_covered(self):
        answer = serve.build_answer(48.2082, 16.3738, False, NOW)
        self.assertFalse(answer["covered"])
        self.assertEqual(answer["warnings"], [])

    def test_no_data(self):
        serve.feed.snapshot = lambda now: (0, [], False, True)
        self.assertIsNone(serve.build_answer(49.9, 14.8, False, NOW))


if __name__ == "__main__":
    unittest.main()
