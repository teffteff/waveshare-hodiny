#!/usr/bin/env python3
"""Testy prevypravece blesku bez site: python3 -m unittest infra/lightning/test_serve.py"""
import json
import math
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import serve  # noqa: E402

NOW = 1789332500.0


def stroke(stroke_id, lat, lon, age_seconds):
    return {"time": int((NOW - age_seconds) * 1000), "lat": lat, "lon": lon, "id": stroke_id,
            "src": 2, "srv": 1, "del": 1803, "dev": 8100}


class StrokeStoreTest(unittest.TestCase):
    def setUp(self):
        self.store = serve.StrokeStore()
        self.box = serve.bounding_box(49.9, 14.8, 300)

    def test_filters_outside_duplicates_and_old(self):
        added, outside = self.store.add([
            stroke(1, 50.0, 14.5, 60),
            stroke(1, 50.0, 14.5, 60),          # opakovani po znovupripojeni
            stroke(2, 42.0, 14.5, 60),          # Italie, mimo vyrez
            stroke(3, 50.1, 14.6, 40 * 60),     # starsi nez uchovavaci doba
            {"time": "x", "lat": 50, "lon": 14, "id": 4},
        ], self.box, NOW)
        self.assertEqual((added, outside), (1, 1))
        self.assertEqual(len(self.store), 1)

    def test_query_radius_since_and_order(self):
        self.store.add([
            stroke(1, 49.95, 14.80, 300),   # 5 km od stredu
            stroke(2, 49.90, 15.60, 120),   # zhruba 57 km
            stroke(3, 49.91, 14.79, 30),
        ], self.box, NOW)
        near = self.store.query(49.9, 14.8, 10, 0, NOW)
        self.assertEqual([item["id"] for item in near], [1, 3])
        # Kurzor je cas prijeti na serveru, ne cas uderu.
        self.assertEqual(self.store.query(49.9, 14.8, 100, NOW - 1, NOW + 5),
                         self.store.query(49.9, 14.8, 100, 0, NOW + 5))
        self.assertEqual(self.store.query(49.9, 14.8, 100, NOW, NOW + 5), [])
        late = [stroke(9, 49.9, 14.8, 200)]   # doruceny pozde, stary uder
        self.store.add(late, self.box, NOW + 10)
        newer = self.store.query(49.9, 14.8, 100, NOW, NOW + 10)
        self.assertEqual([item["id"] for item in newer], [9])
        self.assertEqual(set(near[0]), {"time", "lat", "lon", "id"})

    def test_limit_keeps_newest(self):
        strokes = [stroke(index, 49.9, 14.8, 1000 - index * 0.5) for index in range(serve.MAX_STROKES + 50)]
        self.store.add(strokes, self.box, NOW)
        result = self.store.query(49.9, 14.8, 50, 0, NOW)
        self.assertEqual(len(result), serve.MAX_STROKES)
        self.assertEqual(result[-1]["id"], serve.MAX_STROKES + 49)

    def test_prune(self):
        self.store.add([stroke(1, 49.9, 14.8, 60)], self.box, NOW)
        self.store.prune(NOW + serve.RETENTION_SECONDS)
        self.assertEqual(len(self.store), 0)


class ProtocolTest(unittest.TestCase):
    def test_subscription_rounds_outward(self):
        message = json.loads(serve.subscription_message((51.24, 18.91, 48.36, 11.79)))
        self.assertEqual(message["p"], [51.3, 19.0, 48.3, 11.7])
        self.assertTrue(message["from_lightningmaps_org"])

    def test_challenge_matches_web_client(self):
        reply = json.loads(serve.challenge_reply(5338620.54180223, 1789332454286))
        expected = math.fmod(5338620.54180223 * 3604, 7081) * 1789332454286 / 100
        self.assertAlmostEqual(reply["k"], expected, places=2)

    def test_boxes(self):
        north, east, south, west = serve.bounding_box(50.0, 15.0, 111.32)
        self.assertAlmostEqual(north, 51.0, places=3)
        self.assertAlmostEqual(south, 49.0, places=3)
        union = serve.union_box([(51, 16, 49, 14), (50, 18, 48, 15)])
        self.assertEqual(union, (51, 18, 48, 14))
        self.assertTrue(serve.box_contains(union, (50, 17, 49, 15)))
        self.assertFalse(serve.box_contains(union, (52, 17, 49, 15)))
        self.assertFalse(serve.box_contains(None, (50, 17, 49, 15)))

    def test_distance(self):
        self.assertAlmostEqual(serve.distance_km(50.0755, 14.4378, 49.1951, 16.6068), 185, delta=5)

    def test_parse_query(self):
        self.assertEqual(serve.parse_query("lat=49.9&lon=14.8&r=120&since=1789332500.25"),
                         (49.9, 14.8, 120.0, 1789332500.25))
        self.assertEqual(serve.parse_query("lat=49.9&lon=14.8&r=10")[3], 0)
        for bad in ("lat=49.9&lon=14.8", "lat=91&lon=14&r=10", "lat=49&lon=14&r=0",
                    "lat=49&lon=14&r=500", "lat=nan&lon=14&r=10", "lat=49&lon=14&r=10&since=-1"):
            with self.assertRaises((KeyError, ValueError)):
                serve.parse_query(bad)


class AreaTest(unittest.TestCase):
    def test_wanted_box_covers_exact_clock_circle(self):
        # Hodiny posilaji souradnice na ctyri desetinna mista; vyrez slozeny
        # ze zaokrouhleneho stredu je nepokryl a server nikdy nehlasil "live".
        upstream = serve.Upstream(serve.StrokeStore())
        upstream.request_area(49.9046, 14.7842, 150, NOW)
        upstream._subscribed_box = upstream._wanted_box(NOW)
        upstream._socket = object()
        upstream.last_message = NOW
        self.assertTrue(upstream.request_area(49.9046, 14.7842, 150, NOW))
        self.assertTrue(upstream.request_area(49.9, 14.8, 150, NOW))

    def test_area_coverage_needs_live_subscription(self):
        upstream = serve.Upstream(serve.StrokeStore())
        self.assertFalse(upstream.request_area(49.9, 14.8, 50, NOW))
        upstream._subscribed_box = serve.bounding_box(49.9, 14.8, 300)
        upstream._socket = object()
        upstream.last_message = NOW
        self.assertTrue(upstream.request_area(49.9, 14.8, 50, NOW))
        self.assertFalse(upstream.request_area(40.0, 14.8, 50, NOW))
        self.assertFalse(upstream.request_area(49.9, 14.8, 50, NOW + serve.STALE_SECONDS + 1))


if __name__ == "__main__":
    unittest.main()
