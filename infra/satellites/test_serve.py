#!/usr/bin/env python3
"""Testy druzic bez site.

Potrebuji sgp4 a numpy, stejne jako sluzba:
    /opt/satellites/.venv/bin/python -m unittest infra/satellites/test_serve.py
"""
import json
import math
import sys
import tempfile
import unittest
from datetime import datetime, timezone
from pathlib import Path

import numpy as np
from sgp4.api import Satrec

sys.path.insert(0, str(Path(__file__).resolve().parent))

import serve  # noqa: E402

# Vzorovy prvek ISS z dokumentace sgp4, jednou jako TLE a jednou jako OMM.
ISS_TLE = (
    "1 25544U 98067A   19343.69339541  .00001764  00000-0  38792-4 0  9991",
    "2 25544  51.6439 211.2001 0007417  17.6667  85.6398 15.50103472202482",
)
ISS_OMM = {
    "OBJECT_NAME": "ISS (ZARYA)",
    "OBJECT_ID": "1998-067A",
    "EPOCH": "2019-12-09T16:38:29.363424",
    "MEAN_MOTION": 15.50103472,
    "ECCENTRICITY": 0.0007417,
    "INCLINATION": 51.6439,
    "RA_OF_ASC_NODE": 211.2001,
    "ARG_OF_PERICENTER": 17.6667,
    "MEAN_ANOMALY": 85.6398,
    "EPHEMERIS_TYPE": 0,
    "CLASSIFICATION_TYPE": "U",
    "NORAD_CAT_ID": 25544,
    "ELEMENT_SET_NO": 999,
    "REV_AT_EPOCH": 20248,
    "BSTAR": 3.8792e-05,
    "MEAN_MOTION_DOT": 1.764e-05,
    "MEAN_MOTION_DDOT": 0,
}
EPOCH = datetime(2019, 12, 9, 16, 38, 29, tzinfo=timezone.utc).timestamp()
ONDREJOV = (49.90, 14.78)


def element(norad_id, name, mean_anomaly=85.6398, node=211.2001, mean_motion=15.50103472):
    fields = dict(ISS_OMM)
    fields.update({"NORAD_CAT_ID": norad_id, "OBJECT_NAME": name,
                   "MEAN_ANOMALY": mean_anomaly, "RA_OF_ASC_NODE": node,
                   "MEAN_MOTION": mean_motion})
    return fields


def catalog_with(groups, now=EPOCH):
    catalog = serve.Catalog(cache_dir=tempfile.mkdtemp(), fetcher=lambda name: (500, b""),
                            clock=lambda: now)
    for name, records in groups.items():
        catalog.groups[name] = serve.Group(name, records, now)
    return catalog


class GeometryTest(unittest.TestCase):
    def test_gmst_at_j2000(self):
        # 1. 1. 2000 ve 12:00 UT je GMST 280,46061837 stupne.
        epoch = datetime(2000, 1, 1, 12, tzinfo=timezone.utc).timestamp()
        self.assertAlmostEqual(math.degrees(serve.gmst_radians(np.array([epoch]))[0]),
                               280.46061837, places=4)

    def test_satellite_straight_up_and_on_horizon(self):
        latitude, longitude = ONDREJOV
        ground = serve.observer_ecef(latitude, longitude)
        up = ground / np.linalg.norm(ground)
        _, elevation, distance = serve.look_angles(ground + up * 400.0, latitude, longitude)
        self.assertGreater(float(elevation), 89.5)
        self.assertAlmostEqual(float(distance), 400.0, delta=1.0)
        # Na sever po povrchu: vyska kolem nuly, azimut sever.
        north = np.array([-math.sin(math.radians(latitude)) * math.cos(math.radians(longitude)),
                          -math.sin(math.radians(latitude)) * math.sin(math.radians(longitude)),
                          math.cos(math.radians(latitude))])
        azimuth, elevation, _ = serve.look_angles(ground + north * 100.0, latitude, longitude)
        self.assertAlmostEqual(float(elevation), 0.0, delta=1.0)
        wrapped = (float(azimuth) + 180.0) % 360.0 - 180.0
        self.assertAlmostEqual(wrapped, 0.0, delta=1.0)

    def test_sun_elevation_at_prague_noon(self):
        # Letni slunovrat, prave poledne v Praze: Slunce kolem 63,5 stupne.
        epoch = datetime(2026, 6, 21, 11, 2, tzinfo=timezone.utc).timestamp()
        self.assertAlmostEqual(serve.sun_elevation(epoch, 50.08, 14.42), 63.4, delta=0.5)
        midnight = datetime(2026, 6, 21, 23, 2, tzinfo=timezone.utc).timestamp()
        self.assertLess(serve.sun_elevation(midnight, 50.08, 14.42), -15.0)

    def test_shadow(self):
        sun = np.array([1.0, 0.0, 0.0])
        self.assertTrue(serve.sunlit(np.array([7000.0, 0.0, 0.0]), sun))
        self.assertFalse(serve.sunlit(np.array([-7000.0, 0.0, 0.0]), sun))
        self.assertTrue(serve.sunlit(np.array([-7000.0, 7000.0, 0.0]), sun))


class CatalogTest(unittest.TestCase):
    def test_omm_matches_tle(self):
        from_omm = serve.satrec_from_omm(ISS_OMM)
        from_tle = Satrec.twoline2rv(*ISS_TLE)
        jd, fr = serve.julian_dates(np.array([EPOCH + 3600.0]))
        _, a, _ = from_omm.sgp4(jd[0], fr[0])
        _, b, _ = from_tle.sgp4(jd[0], fr[0])
        self.assertLess(np.linalg.norm(np.array(a) - np.array(b)), 0.5)

    def test_bad_records_are_skipped(self):
        broken = dict(ISS_OMM, EPOCH="yesterday")
        missing = {key: value for key, value in ISS_OMM.items() if key != "BSTAR"}
        group = serve.Group("stations", [broken, missing, "x", ISS_OMM], EPOCH)
        self.assertEqual(group.names, ["ISS (ZARYA)"])
        with self.assertRaises(ValueError):
            serve.Group("stations", [broken], EPOCH)

    def test_epoch_without_fraction(self):
        self.assertIsNotNone(serve.satrec_from_omm(dict(ISS_OMM, EPOCH="2019-12-09T16:38:29")))

    def test_names_are_sanitized(self):
        self.assertEqual(serve.clean_name('iss <script>"x"'), "ISS SCRIPTX")
        self.assertEqual(len(serve.clean_name("A" * 80)), serve.MAX_NAME_LENGTH)

    def test_refresh_stores_and_backs_off(self):
        now = [EPOCH]
        answers = []
        catalog = serve.Catalog(cache_dir=tempfile.mkdtemp(),
                                fetcher=lambda name: answers.pop(0), clock=lambda: now[0])
        catalog.request(["stations"])
        self.assertEqual(catalog.due_groups(), ["stations"])
        answers.append((500, b""))
        catalog.refresh("stations")
        self.assertEqual(catalog.next_attempt["stations"], now[0] + serve.RETRY_MIN_SECONDS)
        answers.append((200, b"<html>not json</html>"))
        catalog.refresh("stations")
        self.assertEqual(catalog.next_attempt["stations"], now[0] + 2 * serve.RETRY_MIN_SECONDS)
        self.assertNotIn("stations", catalog.groups)
        answers.append((200, json.dumps([ISS_OMM]).encode()))
        catalog.refresh("stations")
        self.assertIn("stations", catalog.groups)
        self.assertEqual(catalog.next_attempt["stations"], now[0] + serve.REFRESH_SECONDS)
        # Po restartu se skupina nacte z disku bez stahovani.
        restarted = serve.Catalog(cache_dir=catalog.cache_dir, fetcher=None, clock=lambda: now[0])
        restarted.load_cached()
        self.assertEqual(restarted.groups["stations"].names, ["ISS (ZARYA)"])
        # Blokovani a "stejna data" nemazou nactenou skupinu.
        now[0] += 1000
        answers.append((403, b"GP data has not updated since your last successful download"))
        catalog.refresh("stations")
        self.assertIn("stations", catalog.groups)
        self.assertEqual(catalog.next_attempt["stations"], now[0] + serve.REFRESH_SECONDS)
        answers.append((429, b"slow down"))
        catalog.refresh("stations")
        self.assertEqual(catalog.next_attempt["stations"], now[0] + serve.RETRY_BLOCKED_SECONDS)
        self.assertIn("stations", catalog.groups)


class ResponseTest(unittest.TestCase):
    def setUp(self):
        serve.pass_cache = serve.PassCache()

    def find_overhead_epoch(self):
        satellite = serve.satrec_from_omm(ISS_OMM)
        epochs = EPOCH + 30.0 * np.arange(2 * 24 * 120)
        jd, fr = serve.julian_dates(epochs)
        _, positions, _ = satellite.sgp4_array(jd, fr)
        ecef = serve.teme_to_ecef(positions, serve.gmst_radians(epochs))
        _, elevation, _ = serve.look_angles(ecef, *ONDREJOV)
        return float(epochs[int(np.argmax(elevation))]), float(elevation.max())

    def test_iss_track_shape(self):
        epoch, peak = self.find_overhead_epoch()
        self.assertGreater(peak, 20.0)
        catalog = catalog_with({"stations": [ISS_OMM], "visual": [ISS_OMM, element(99001, "OTHER", 10.0)]}, epoch)
        status, body = serve.build_response(catalog, *ONDREJOV, ["stations", "visual"], 10, epoch)
        self.assertEqual(status, 200)
        self.assertEqual(body["time"] % serve.STEP_SECONDS, 0)
        self.assertLessEqual(body["time"], epoch)
        iss = [item for item in body["sats"] if item["id"] == 25544]
        # ISS je ve stanicich i jasnych druzicich, poslat se ma jednou.
        self.assertEqual(len(iss), 1)
        self.assertEqual(iss[0]["g"], serve.GROUP_INDEX["stations"])
        self.assertEqual(len(iss[0]["p"]), 2 * serve.SAMPLE_COUNT)
        self.assertTrue(300 <= iss[0]["h"] <= 450)
        self.assertTrue(all(0 <= az < 3600 for az in iss[0]["p"][0::2]))
        self.assertTrue(all(-900 <= el <= 900 for el in iss[0]["p"][1::2]))
        self.assertIn(iss[0]["l"], (0, 1))
        self.assertIn("pass", body)
        self.assertLessEqual(body["pass"]["rise"], body["pass"]["maxTime"])
        self.assertLess(body["pass"]["maxTime"], body["pass"]["set"])
        self.assertGreater(body["pass"]["set"], epoch)
        json.dumps(body)

    def test_min_elevation_filters(self):
        epoch, peak = self.find_overhead_epoch()
        catalog = catalog_with({"stations": [ISS_OMM]}, epoch)
        _, body = serve.build_response(catalog, *ONDREJOV, ["stations"], 60, epoch - 3600)
        self.assertEqual(body["sats"], [])
        self.assertEqual(body["total"], 0)

    def test_cap_prefers_priority_groups(self):
        catalog = catalog_with({
            "starlink": [element(70000 + index, f"STARLINK-{index}", index * 2.0, index * 3.0)
                         for index in range(180)],
            "stations": [ISS_OMM],
        })
        original = serve.MAX_SATELLITES
        serve.MAX_SATELLITES = 5
        try:
            _, body = serve.build_response(catalog, *ONDREJOV, ["starlink", "stations"], 0, EPOCH)
        finally:
            serve.MAX_SATELLITES = original
        self.assertLessEqual(len(body["sats"]), 5)
        groups = [item["g"] for item in body["sats"]]
        self.assertEqual(groups, sorted(groups, key=lambda g: serve.PRIORITY.index(serve.GROUPS[g][0])))

    def test_passes_prefer_the_home_satellite(self):
        epoch, _ = self.find_overhead_epoch()
        catalog = catalog_with({"stations": [ISS_OMM],
                                "satgus": [element(62713, "SATGUS", 120.0, 300.0)]}, epoch)
        _, body = serve.build_response(
            catalog, *ONDREJOV, ["stations", "satgus"], 10, epoch)
        names = [entry["n"] for entry in body["passes"]]
        self.assertEqual(names, ["SATGUS", "ISS"])
        satgus, iss = body["passes"]
        self.assertEqual(satgus["pref"], 1)
        self.assertNotIn("pref", iss)
        for entry in body["passes"]:
            self.assertLessEqual(entry["rise"], entry["maxTime"])
            self.assertLess(entry["maxTime"], entry["set"])
            self.assertGreater(entry["set"], epoch)
        # Starsi firmware cte jen "pass" a popisek ma napevno ISS.
        self.assertEqual(body["pass"], iss)
        json.dumps(body)

    def test_pass_only_for_requested_groups(self):
        epoch, _ = self.find_overhead_epoch()
        catalog = catalog_with({"stations": [ISS_OMM],
                                "satgus": [element(62713, "SATGUS", 120.0, 300.0)]}, epoch)
        _, body = serve.build_response(catalog, *ONDREJOV, ["satgus"], 10, epoch)
        self.assertEqual([entry["n"] for entry in body["passes"]], ["SATGUS"])
        self.assertNotIn("pass", body)

    def test_single_satellite_is_downloaded_by_catalogue_number(self):
        seen = []

        class Answer:
            status = 200

            def read(self, limit):
                return json.dumps([element(62713, "SATGUS")]).encode()

            def __enter__(self):
                return self

            def __exit__(self, *args):
                return False

        def fake_urlopen(request, timeout):
            seen.append(request.full_url)
            return Answer()

        original = serve.urllib.request.urlopen
        serve.urllib.request.urlopen = fake_urlopen
        try:
            status, payload = serve.download_group("satgus")
            serve.download_group("stations")
        finally:
            serve.urllib.request.urlopen = original
        self.assertEqual(status, 200)
        self.assertEqual(serve.Group("satgus", serve.parse_catalog(payload), EPOCH).names,
                         ["SATGUS"])
        self.assertIn("CATNR=62713", seen[0])
        self.assertIn("GROUP=stations", seen[1])
        self.assertTrue(all("FORMAT=json" in url for url in seen))

    def test_pending_and_stale(self):
        catalog = catalog_with({})
        status, body = serve.build_response(catalog, *ONDREJOV, ["stations"], 10, EPOCH)
        self.assertEqual((status, body["error"]), (503, "loading"))
        catalog = catalog_with({"stations": [ISS_OMM]}, EPOCH)
        status, body = serve.build_response(
            catalog, *ONDREJOV, ["stations"], 10, EPOCH + serve.MAX_DATA_AGE_SECONDS + 10)
        self.assertEqual((status, body["error"]), (503, "unavailable"))
        catalog = catalog_with({"stations": [ISS_OMM]}, EPOCH)
        status, body = serve.build_response(catalog, *ONDREJOV, ["stations", "weather"], 10, EPOCH)
        self.assertEqual(status, 200)
        self.assertEqual(body["pending"], ["weather"])


class QueryTest(unittest.TestCase):
    def test_valid(self):
        self.assertEqual(serve.parse_query("lat=49.90461&lon=14.7842&groups=weather,stations&minel=15"),
                         (49.9, 14.78, ["stations", "weather"], 15))
        self.assertEqual(serve.parse_query("lat=0&lon=0")[2], ["stations", "visual", "weather"])

    def test_invalid(self):
        for query in ("lat=91&lon=0", "lat=nan&lon=0", "lat=1", "lat=1&lon=1&groups=all",
                      "lat=1&lon=1&groups=", "lat=1&lon=1&minel=-5", "lat=1&lon=1&minel=61",
                      "lat=1&lon=1&minel=1e3", "lat=1&lon=1&groups=" + "stations," * 50):
            self.assertIsInstance(serve.parse_query(query), str, query)


if __name__ == "__main__":
    unittest.main()
