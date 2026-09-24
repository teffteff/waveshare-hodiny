#!/usr/bin/env python3
"""Testy upozorneni bez site: python3 -m unittest infra/alerts/test_serve.py"""
import sys
import tempfile
import unittest
from datetime import datetime
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent))

import serve  # noqa: E402

HOME_LAT = 49.9046
HOME_LON = 14.7842
# 12:00 mistniho casu, mimo nocni klid.
NOON = datetime(2026, 9, 22, 12, 0, tzinfo=serve.TIMEZONE).timestamp()


def clock_payload(**overrides):
    payload = {
        "name": "pracovna", "enabled": True, "lat": HOME_LAT, "lon": HOME_LON,
        "rain": {"enabled": True, "lead": 20, "dbz": 28, "radius": 5},
        "planes": {"military": True, "rare": True, "low": True, "radius": 30,
                   "lowRadius": 1500, "lowHeight": 500},
        "quiet": {"from": 22, "to": 7},
    }
    payload.update(overrides)
    return payload


def forecast(now, steps, slot=NOON - 300, covered=True):
    return {"covered": covered, "step": 10, "now": now, "steps": steps, "slot": slot}


def plane_at(north_km, east_km, **fields):
    """Letadlo posunute od domu o dany pocet km."""
    lat = HOME_LAT + north_km / 110.574
    lon = HOME_LON + east_km / (111.320 * 0.6441)
    plane = {"hex": "abc123", "lat": lat, "lon": lon}
    plane.update(fields)
    return plane


class ParseConfigTest(unittest.TestCase):
    def test_accepts_clock_payload(self):
        config = serve.parse_config(clock_payload())
        self.assertEqual(config["rain"]["lead"], 20)
        self.assertEqual(config["planes"]["lowHeight"], 500)

    def test_rejects_out_of_range_and_wrong_types(self):
        for payload in (
            clock_payload(lat=91),
            clock_payload(enabled="yes"),
            clock_payload(rain={"enabled": True, "lead": 5, "dbz": 28, "radius": 5}),
            clock_payload(quiet={"from": 24, "to": 7}),
            clock_payload(planes=None),
            [],
        ):
            with self.assertRaises(ValueError):
                serve.parse_config(payload)

    def test_quiet_hours_wrap_midnight(self):
        config = serve.parse_config(clock_payload())
        at = lambda hour: datetime(2026, 9, 22, hour, tzinfo=serve.TIMEZONE)
        self.assertTrue(serve.is_quiet(config, at(23)))
        self.assertTrue(serve.is_quiet(config, at(6)))
        self.assertFalse(serve.is_quiet(config, at(7)))
        self.assertFalse(serve.is_quiet(config, at(21)))
        config["quiet"] = {"from": 0, "to": 0}
        self.assertFalse(serve.is_quiet(config, at(3)))


class RainStateTest(unittest.TestCase):
    def test_coming_within_lead(self):
        self.assertEqual(serve.rain_state(forecast(0, [0, 32, 40, 0, 0, 0]), 20, 28),
                         ("coming", 20, 32))

    def test_beyond_lead_is_dry(self):
        self.assertEqual(serve.rain_state(forecast(0, [0, 0, 40, 0, 0, 0]), 20, 28)[0], "dry")

    def test_already_raining(self):
        self.assertEqual(serve.rain_state(forecast(36, [36] * 6), 20, 28)[0], "raining")

    def test_missing_frame_is_not_dry(self):
        self.assertEqual(serve.rain_state(forecast(0, [-1, 0, 0, 0, 0, 0]), 20, 28)[0], "unknown")

    def test_outside_radar_range(self):
        self.assertEqual(serve.rain_state(forecast(0, [40] * 6, covered=False), 20, 28)[0],
                         "unknown")


class RainWatchTest(unittest.TestCase):
    def test_one_push_per_front_then_rearm_after_dry_spell(self):
        watch = serve.RainWatch()
        self.assertTrue(watch.update("coming", 0))
        self.assertFalse(watch.update("coming", 150))
        self.assertFalse(watch.update("raining", 600))
        self.assertFalse(watch.update("dry", 900))
        # Pul hodiny od posledniho deste jeste neubehlo.
        self.assertFalse(watch.update("coming", 1000))
        self.assertFalse(watch.update("dry", 1000 + 29 * 60))
        self.assertFalse(watch.armed)
        watch.update("dry", 1000 + 30 * 60)
        self.assertTrue(watch.armed)
        self.assertTrue(watch.update("coming", 1000 + 31 * 60))


class PredictPassTest(unittest.TestCase):
    def test_straight_overflight(self):
        # 3 km jizne, leti na sever 120 kt (61.7 m/s): nad domem za ~48 s.
        plane = plane_at(-3, 0, gs=120, track=0, alt_geom=(300 + 45 + 400) / 0.3048)
        predicted = serve.predict_pass(plane, HOME_LAT, HOME_LON, 400)
        self.assertAlmostEqual(predicted["seconds"], 48.6, delta=1.5)
        self.assertLess(predicted["distance_m"], 50)
        self.assertAlmostEqual(predicted["height_m"], 300, delta=1)

    def test_flying_away_uses_current_distance(self):
        plane = plane_at(-3, 0, gs=120, track=180, alt_geom=3000)
        predicted = serve.predict_pass(plane, HOME_LAT, HOME_LON, 400)
        self.assertEqual(predicted["seconds"], 0)
        self.assertAlmostEqual(predicted["distance_m"], 3000, delta=30)

    def test_descent_is_projected(self):
        # Klesa 1000 ft/min, nad domem za ~48 s: o ~250 m niz nez ted.
        plane = plane_at(-3, 0, gs=120, track=0, baro_rate=-1000,
                         alt_geom=(600 + 45 + 400) / 0.3048)
        predicted = serve.predict_pass(plane, HOME_LAT, HOME_LON, 400)
        self.assertAlmostEqual(predicted["height_m"], 600 - 0.3048 * 1000 * 48.6 / 60, delta=10)

    def test_horizon_limits_far_aircraft(self):
        # 20 km daleko a 120 kt: za tri minuty uleti jen 11 km.
        plane = plane_at(-20, 0, gs=120, track=0, alt_geom=2000)
        predicted = serve.predict_pass(plane, HOME_LAT, HOME_LON, 400)
        self.assertEqual(predicted["seconds"], serve.LOW_LOOKAHEAD_SECONDS)
        self.assertGreater(predicted["distance_m"], 8000)

    def test_hovering_helicopter_uses_current_distance(self):
        plane = plane_at(0.5, 0, gs=5, track=90, alt_baro=(200 + 400) / 0.3048)
        predicted = serve.predict_pass(plane, HOME_LAT, HOME_LON, 400)
        self.assertEqual(predicted["seconds"], 0)
        self.assertAlmostEqual(predicted["distance_m"], 500, delta=10)
        self.assertAlmostEqual(predicted["height_m"], 200, delta=1)

    def test_without_altitude(self):
        self.assertIsNone(serve.predict_pass(plane_at(0, 0, gs=100, track=0),
                                             HOME_LAT, HOME_LON, 400))


class TypeHistoryTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.path = Path(self.directory.name) / "types.json"

    def tearDown(self):
        self.directory.cleanup()

    def test_warmup_then_rare_until_seen_on_three_days(self):
        history = serve.TypeHistory(self.path)
        history.started = "2026-09-01"
        self.assertFalse(history.is_rare("A400", "2026-09-10"))
        self.assertTrue(history.is_rare("A400", "2026-09-20"))
        for day in ("2026-09-15", "2026-09-17", "2026-09-19"):
            history.record("A400", day)
        self.assertFalse(history.is_rare("A400", "2026-09-20"))
        # Dnesni vyskyt se nepocita, ty mimo okno taky ne.
        self.assertTrue(history.is_rare("A400", "2026-10-18"))

    def test_saves_and_loads(self):
        history = serve.TypeHistory(self.path)
        history.started = "2026-09-01"
        history.record("C172", "2026-09-20")
        history.save("2026-09-20")
        loaded = serve.TypeHistory(self.path)
        self.assertEqual(loaded.started, "2026-09-01")
        self.assertEqual(loaded.days, {"C172": {"2026-09-20"}})


class WatcherTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.watcher = serve.Watcher(Path(self.directory.name))
        self.watcher.store_config("aabbccddeeff", serve.parse_config(clock_payload()))
        self.watcher.elevations[(round(HOME_LAT, 2), round(HOME_LON, 2))] = 400.0
        self.pushes = []
        patcher = mock.patch.object(serve, "send_push",
                                    side_effect=lambda *a, **k: self.pushes.append(a) or True)
        patcher.start()
        self.addCleanup(patcher.stop)

    def tearDown(self):
        self.directory.cleanup()

    def run_rain(self, payload, now):
        with mock.patch.object(serve, "get_json", return_value=payload):
            self.watcher.check_rain(now)

    def run_planes(self, aircraft, now):
        with mock.patch.object(serve, "get_json", return_value={"ac": aircraft}):
            self.watcher.check_planes(now)

    def test_rain_push_once(self):
        self.run_rain(forecast(0, [0, 32, 0, 0, 0, 0], slot=NOON - 300), NOON)
        self.run_rain(forecast(0, [32, 32, 0, 0, 0, 0], slot=NOON), NOON + 150)
        self.assertEqual(len(self.pushes), 1)
        self.assertEqual(self.pushes[0][0], "Déšť za 20 min")

    def test_rain_waits_through_quiet_hours(self):
        night = datetime(2026, 9, 22, 6, 58, tzinfo=serve.TIMEZONE).timestamp()
        self.run_rain(forecast(0, [0, 32, 0, 0, 0, 0], slot=night - 300), night)
        self.assertEqual(self.pushes, [])
        self.run_rain(forecast(0, [32, 0, 0, 0, 0, 0], slot=night), night + 180)
        self.assertEqual(self.pushes[0][0], "Déšť za 10 min")

    def test_stale_forecast_is_ignored(self):
        self.run_rain(forecast(0, [40] * 6, slot=NOON - 3600), NOON)
        self.assertEqual(self.pushes, [])

    def test_disabled_clock_is_not_watched(self):
        self.watcher.store_config("aabbccddeeff",
                                  serve.parse_config(clock_payload(enabled=False)))
        self.run_rain(forecast(0, [40] * 6, slot=NOON), NOON)
        self.assertEqual(self.pushes, [])

    def test_military_once_per_aircraft(self):
        plane = plane_at(10, 0, dbFlags=1, t="C130", desc="LOCKHEED C-130 Hercules",
                         flight="CEF123", alt_geom=20000, gs=250, track=90)
        self.run_planes([plane], NOON)
        self.run_planes([plane], NOON + 10)
        self.assertEqual(len(self.pushes), 1)
        title, message = self.pushes[0][:2]
        self.assertEqual(title, "Vojenské letadlo")
        self.assertIn("CEF123", message)
        self.assertIn("10 km S", message)

    def test_outside_radius_is_ignored(self):
        self.run_planes([plane_at(40, 0, dbFlags=1, alt_geom=20000)], NOON)
        self.assertEqual(self.pushes, [])

    def test_low_military_pass_is_one_push_titled_low(self):
        plane = plane_at(-3, 0, dbFlags=1, gs=120, track=0,
                         alt_geom=(300 + 45 + 400) / 0.3048)
        self.run_planes([plane], NOON)
        self.assertEqual(len(self.pushes), 1)
        self.assertEqual(self.pushes[0][0], "Nízký přelet")
        self.assertIn("Vojenské letadlo", self.pushes[0][1])
        self.assertEqual(self.pushes[0][3], 4)

    def test_high_overflight_is_not_low(self):
        self.run_planes([plane_at(-3, 0, gs=120, track=0, alt_geom=30000)], NOON)
        self.assertEqual(self.pushes, [])

    def test_no_low_pass_without_elevation(self):
        self.watcher.elevations.clear()
        self.watcher.elevation_retry[(round(HOME_LAT, 2), round(HOME_LON, 2))] = NOON + 3600
        self.run_planes([plane_at(-3, 0, gs=120, track=0, alt_geom=1500)], NOON)
        self.assertEqual(self.pushes, [])

    def test_rare_type_after_warmup(self):
        self.watcher.history.started = "2026-08-01"
        self.run_planes([plane_at(5, 5, t="A400", alt_geom=15000)], NOON)
        self.assertEqual(self.pushes[0][0], "Vzácné letadlo")
        # Typ se zapise az po vyhodnoceni; druhe letadlo tehoz typu je dnes
        # porad vzacne, protoze dnesek se nepocita.
        self.run_planes([plane_at(5, 5, hex="def456", t="A400", alt_geom=15000)], NOON + 10)
        self.assertEqual(len(self.pushes), 2)

    def test_quiet_hours_silence_planes(self):
        night = datetime(2026, 9, 22, 23, 0, tzinfo=serve.TIMEZONE).timestamp()
        self.run_planes([plane_at(10, 0, dbFlags=1, alt_geom=20000)], night)
        self.assertEqual(self.pushes, [])

    def test_elevation_for_known_and_unknown_place(self):
        config = serve.parse_config(clock_payload())
        self.assertEqual(self.watcher.elevation_for(config), 400.0)
        elsewhere = serve.parse_config(clock_payload(lat=50.5, lon=15.0))
        with mock.patch.object(serve, "get_json", return_value={"elevation": [312.0]}):
            self.assertEqual(self.watcher.elevation_for(elsewhere), 312.0)
        failing = serve.parse_config(clock_payload(lat=51.0, lon=15.0))
        with mock.patch.object(serve, "get_json", side_effect=OSError("offline")):
            self.assertIsNone(self.watcher.elevation_for(failing))

    def test_low_pass_is_recorded_with_prediction(self):
        plane = plane_at(-3, 0, gs=120, track=0, flight="OKABC",
                         alt_geom=(300 + 45 + 400) / 0.3048)
        self.run_planes([plane], NOON)
        self.run_planes([plane], NOON + 10)
        events = self.watcher.events(0)
        self.assertEqual(len(events), 1)
        event = events[0]
        self.assertEqual(event["kinds"], ["low"])
        self.assertTrue(event["sent"])
        self.assertEqual(event["aircraft"]["flight"], "OKABC")
        self.assertEqual(event["low"]["radius"], 1500)
        self.assertEqual(event["low"]["height"], 500)
        self.assertLess(event["low"]["distance_m"], 100)
        self.assertAlmostEqual(event["low"]["height_m"], 300, delta=2)
        self.assertAlmostEqual(event["low"]["seconds"], 3000 / (120 * serve.KNOT_TO_MS), delta=1)
        self.assertEqual(self.watcher.events(NOON), [])

    def test_military_event_has_no_prediction(self):
        self.run_planes([plane_at(10, 0, dbFlags=1, alt_geom=20000)], NOON)
        event = self.watcher.events(0)[0]
        self.assertEqual(event["kinds"], ["military"])
        self.assertNotIn("low", event)

    def test_events_skip_torn_line(self):
        self.run_planes([plane_at(10, 0, dbFlags=1, alt_geom=20000)], NOON)
        with (Path(self.directory.name) / "events.jsonl").open("a") as handle:
            handle.write('{"ts": 17')
        self.assertEqual(len(self.watcher.events(0)), 1)

    def test_config_survives_restart(self):
        reloaded = serve.Watcher(Path(self.directory.name))
        self.assertIn("aabbccddeeff", reloaded.configs)


if __name__ == "__main__":
    unittest.main()
