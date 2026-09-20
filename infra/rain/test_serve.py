#!/usr/bin/env python3
"""Testy prevypravece srazek bez site: python3 -m unittest infra/rain/test_serve.py"""
import struct
import sys
import time
import unittest
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import serve  # noqa: E402

WIDTH = 680
HEIGHT = 460
NOW = 1789916000.0


def paeth(left, up, corner):
    estimate = left + up - corner
    da, db, dc = abs(estimate - left), abs(estimate - up), abs(estimate - corner)
    if da <= db and da <= dc:
        return left
    return up if db <= dc else corner


def encode_palette_png(width, height, pixels, filter_type=0):
    """Osmibitove PNG s paletou, zakodovane zvolenym filtrem.

    Snimky CHMU chodi nefiltrovane, ale dekoder ma umet vsech pet metod, takze
    si je test musi umet vyrobit.
    """
    raw = bytearray()
    previous = bytearray(width)
    for row in range(height):
        line = bytearray(pixels[row * width:(row + 1) * width])
        out = bytearray(width)
        for x in range(width):
            left = line[x - 1] if x else 0
            up = previous[x]
            corner = previous[x - 1] if x else 0
            if filter_type == 0:
                out[x] = line[x]
            elif filter_type == 1:
                out[x] = (line[x] - left) & 0xFF
            elif filter_type == 2:
                out[x] = (line[x] - up) & 0xFF
            elif filter_type == 3:
                out[x] = (line[x] - ((left + up) >> 1)) & 0xFF
            else:
                out[x] = (line[x] - paeth(left, up, corner)) & 0xFF
        raw.append(filter_type)
        raw += out
        previous = line

    def chunk(kind, data):
        body = kind + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    header = struct.pack(">IIBBBBB", width, height, 8, 3, 0, 0, 0)
    palette = bytes(256 * 3)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"PLTE", palette)
            + chunk(b"IDAT", zlib.compress(bytes(raw))) + chunk(b"IEND", b""))


def blank(width=WIDTH, height=HEIGHT):
    return bytearray(width * height)


def frame_from(pixels, width=WIDTH, height=HEIGHT):
    return serve.Frame(width, height, bytes(pixels))


class ScaleTest(unittest.TestCase):
    def test_matches_the_six_colours_the_firmware_labels(self):
        # Indexy palety, ktere ChmiRadarService.cpp kresli do legendy, a dBZ
        # podle publikovane stupnice scl-dbz-mmh.png. Spodni hrana pasma.
        self.assertEqual(serve.dbz_from_index(182), 56)
        self.assertEqual(serve.dbz_from_index(183), 52)
        self.assertEqual(serve.dbz_from_index(185), 44)
        self.assertEqual(serve.dbz_from_index(187), 36)
        self.assertEqual(serve.dbz_from_index(190), 24)
        self.assertEqual(serve.dbz_from_index(193), 12)
        self.assertEqual(serve.dbz_from_index(195), 4)

    def test_everything_else_is_no_echo(self):
        for index in (0, 1, 145, 181, 196, 242, 255):
            self.assertEqual(serve.dbz_from_index(index), 0)


class ProjectionTest(unittest.TestCase):
    def test_known_point(self):
        # Poloha hodin. Overeno proti skutecnemu snimku: zpetna projekce pixelu
        # vraci tutez polohu.
        x, y = serve.project(49.90461, 14.7842, WIDTH, HEIGHT)
        self.assertEqual((x, y), (251, 257))

    def test_data_mask_excludes_the_cross_sections(self):
        # Svisle rezy podel horniho a praveho okraje nejsou mapa.
        right, top = serve.data_bounds(WIDTH, HEIGHT)
        self.assertEqual((right, top), (597, 82))

    def test_corners_map_to_the_image_edges(self):
        self.assertEqual(serve.project(serve.LAT_TOP, serve.LON_LEFT, WIDTH, HEIGHT), (0, 0))
        self.assertEqual(serve.project(serve.LAT_BOTTOM, serve.LON_RIGHT, WIDTH, HEIGHT),
                         (WIDTH - 1, HEIGHT - 1))


class DecodeTest(unittest.TestCase):
    def test_every_filter_decodes_to_the_same_pixels(self):
        width, height = 23, 9
        pixels = bytearray((row * 31 + column * 7) & 0xFF
                           for row in range(height) for column in range(width))
        for filter_type in range(5):
            blob = encode_palette_png(width, height, pixels, filter_type)
            frame = serve.decode_palette_png(blob)
            self.assertEqual((frame.width, frame.height), (width, height))
            self.assertEqual(frame.pixels, bytes(pixels), f"filtr {filter_type}")

    def test_rejects_what_is_not_an_eight_bit_palette(self):
        with self.assertRaises(ValueError):
            serve.decode_palette_png(b"nic takoveho")
        truecolour = bytearray(encode_palette_png(4, 4, blank(4, 4)))
        truecolour[25] = 2   # barevny typ 2 = RGB bez palety
        with self.assertRaises(ValueError):
            serve.decode_palette_png(bytes(truecolour))


class PeakTest(unittest.TestCase):
    def test_takes_the_strongest_pixel_in_the_box(self):
        pixels = blank()
        x, y = serve.project(49.90461, 14.7842, WIDTH, HEIGHT)
        pixels[y * WIDTH + x] = 193              # 12 dBZ pod hodinami
        pixels[(y + 4) * WIDTH + x + 4] = 187    # 36 dBZ o ctyri pixely vedle
        frame = frame_from(pixels)
        self.assertEqual(frame.peak_dbz(x, y, 1), 12)
        self.assertEqual(frame.peak_dbz(x, y, 5), 36)

    def test_ignores_pixels_outside_the_data_mask(self):
        right, top = serve.data_bounds(WIDTH, HEIGHT)
        pixels = blank()
        # Silny odraz v rezu nad mapou a v rezu vpravo. Ani jeden neni pocasi.
        pixels[(top - 3) * WIDTH + 300] = 182
        pixels[200 * WIDTH + right + 5] = 182
        frame = frame_from(pixels)
        self.assertEqual(frame.peak_dbz(300, top, 6), 0)
        self.assertEqual(frame.peak_dbz(right, 200, 8), 0)

    def test_no_echo_is_zero(self):
        self.assertEqual(frame_from(blank()).peak_dbz(251, 257, 5), 0)


class RangeTest(unittest.TestCase):
    def test_czech_places_are_in_range_and_distant_ones_are_not(self):
        self.assertTrue(serve.in_radar_range(50.0755, 14.4378))   # Praha
        self.assertTrue(serve.in_radar_range(49.1951, 16.6068))   # Brno
        self.assertFalse(serve.in_radar_range(40.4168, -3.7038))  # Madrid
        self.assertFalse(serve.in_radar_range(52.52, 13.405))     # Berlin


class AnswerTest(unittest.TestCase):
    def setUp(self):
        self.x, self.y = serve.project(49.90461, 14.7842, WIDTH, HEIGHT)
        current = blank()
        current[self.y * WIDTH + self.x] = 190   # 24 dBZ prave ted
        steps = {}
        for lead, index in zip(serve.FORECAST_LEADS, (193, 190, 187, 185, 183, 182)):
            pixels = blank()
            pixels[self.y * WIDTH + self.x] = index
            steps[lead] = frame_from(pixels)
        self.current = frame_from(current)
        self.steps = steps
        self._install(self.current, self.steps)

    def _install(self, current, steps):
        serve.radar._now = current
        serve.radar._steps = steps
        serve.radar._slot = 1789915800
        serve.radar._fetched = NOW

    def tearDown(self):
        serve.radar._now = None
        serve.radar._steps = {}
        serve.radar._fetched = 0.0

    def test_reports_the_ladder(self):
        answer, source = serve.build_answer(49.90461, 14.7842, 1, NOW + 1)
        self.assertEqual(source, "cache")
        self.assertEqual(answer["now"], 24)
        self.assertEqual(answer["steps"], [12, 24, 36, 44, 52, 56])
        self.assertEqual(answer["step"], serve.FORECAST_STEP_MINUTES)
        self.assertEqual(answer["slot"], 1789915800)
        self.assertTrue(answer["covered"])

    def test_missing_lead_is_minus_one(self):
        partial = dict(self.steps)
        del partial[30]
        self._install(self.current, partial)
        answer, _ = serve.build_answer(49.90461, 14.7842, 1, NOW + 1)
        self.assertEqual(answer["steps"][2], -1)

    def test_outside_the_image_says_so_instead_of_reporting_dry(self):
        answer, _ = serve.build_answer(40.4168, -3.7038, 5, NOW + 1)
        self.assertFalse(answer["covered"])
        self.assertEqual(answer["now"], -1)
        self.assertEqual(answer["steps"], [-1] * len(serve.FORECAST_LEADS))

    def test_in_the_image_but_out_of_radar_range_keeps_the_numbers(self):
        # Severovychodni roh kompozice lezi v obraze, ale 290 km od nejblizsiho
        # radaru. Cisla jsou skutecna, jen se z nich nesmi delat zaver
        # "neprsi" - proto covered=false.
        answer, _ = serve.build_answer(51.4, 19.6, 5, NOW + 1)
        self.assertFalse(answer["covered"])
        self.assertEqual(answer["now"], 0)
        self.assertEqual(answer["steps"], [0] * len(serve.FORECAST_LEADS))

    def test_answer_stays_small(self):
        import json
        answer, _ = serve.build_answer(49.90461, 14.7842, 5, NOW + 1)
        self.assertLess(len(json.dumps(answer, separators=(",", ":"))), 200)


class CacheTest(unittest.TestCase):
    def setUp(self):
        self.radar = serve.Radar()
        self.calls = []

    def _loader(self, result):
        def load(now):
            self.calls.append(now)
            if isinstance(result, Exception):
                raise result
            return result
        return load

    def test_serves_cache_then_refetches_then_falls_back_to_stale(self):
        frame = frame_from(blank())
        self.radar._load = self._loader((1789915800, frame, {10: frame}))
        slot, current, _, source = self.radar.snapshot(NOW)
        self.assertEqual((slot, source), (1789915800, "fresh"))
        self.assertIs(current, frame)
        self.assertEqual(len(self.calls), 1)

        # Uvnitr okna se nestahuje znovu.
        self.assertEqual(self.radar.snapshot(NOW + serve.CACHE_SECONDS - 1)[3], "cache")
        self.assertEqual(len(self.calls), 1)

        # Po vyprseni okna se to zkusi znovu; kdyz to selze, plati stara data.
        self.radar._load = self._loader(OSError("upstream"))
        self.assertEqual(self.radar.snapshot(NOW + serve.CACHE_SECONDS + 1)[3], "stale")
        self.assertEqual(len(self.calls), 2)

        # Az prilis stara uz ne: prazdno je lepsi nez hodinu stary dest.
        slot, current, _, source = self.radar.snapshot(NOW + serve.STALE_SECONDS + 1)
        self.assertEqual(source, "error")
        self.assertIsNone(current)


if __name__ == "__main__":
    unittest.main()
