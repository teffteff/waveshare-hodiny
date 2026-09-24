#!/usr/bin/env python3
"""Testy prepravce letadel bez site: python3 -m unittest infra/planes/test_serve.py"""
import json
import sys
import threading
import unittest
import urllib.request
from http.server import ThreadingHTTPServer
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent))

import serve  # noqa: E402

UPSTREAM = {
    "ac": [
        {"hex": "far001", "dst": 40.0, "alt_baro": 35000, "lat": 50.2, "lon": 14.2,
         "t": "A320", "track_rate": 0.1, "category": "A3", "ws": 40, "wd": 270},
        {"hex": "near01", "dst": 2.0, "alt_baro": 3000, "lat": 49.91, "lon": 14.79,
         "t": "C172", "roll": 12.5, "nav_altitude_mcp": 2500},
        {"hex": "gnd001", "dst": 20.0, "alt_baro": "ground", "lat": 50.1, "lon": 14.26},
    ],
    "msg": "No error",
    "now": 1790000000.0,
}


class PlanesTest(unittest.TestCase):
    def setUp(self):
        serve._cache.clear()
        self.fetches = 0

        def fetch(*_args):
            self.fetches += 1
            return json.loads(json.dumps(UPSTREAM))

        patcher = mock.patch.object(serve, "_fetch", side_effect=fetch)
        patcher.start()
        self.addCleanup(patcher.stop)
        self.server = ThreadingHTTPServer(("127.0.0.1", 0), serve.Handler)
        threading.Thread(target=self.server.serve_forever, daemon=True).start()
        self.addCleanup(self.server.server_close)
        self.addCleanup(self.server.shutdown)

    def get(self, query):
        url = f"http://127.0.0.1:{self.server.server_port}/planes.json?{query}"
        with urllib.request.urlopen(url, timeout=5) as response:
            return json.loads(response.read())

    def test_trimmed_for_clocks(self):
        body = self.get("lat=49.9&lon=14.78&dist=54")
        self.assertEqual([a["hex"] for a in body["ac"]], ["near01", "far001"])
        self.assertEqual(body["total"], 2)
        self.assertNotIn("track_rate", body["ac"][1])
        self.assertNotIn("dst", body["ac"][0])

    def test_full_keeps_everything(self):
        body = self.get("lat=49.9&lon=14.78&dist=54&full=1")
        self.assertEqual(body, UPSTREAM)

    def test_full_and_trimmed_share_one_download(self):
        self.get("lat=49.9&lon=14.78&dist=54&full=1")
        self.get("lat=49.9&lon=14.78&dist=54")
        self.assertEqual(self.fetches, 1)


if __name__ == "__main__":
    unittest.main()
