"""Testy hlaseni poruch bez site a bez systemd: python3 -m unittest infra/health/test_check.py"""
from __future__ import annotations

import importlib.util
import os
import pathlib
import tempfile
import unittest
from unittest import mock

HERE = pathlib.Path(__file__).resolve().parent


def load_module(state_dir: str):
    os.environ["HEALTH_STATE_DIR"] = state_dir
    os.environ["NTFY_TOPIC"] = "test-topic"
    spec = importlib.util.spec_from_file_location("health_check", HERE / "check.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class HourlyTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.check = load_module(self.tmp.name)
        self.pushes = []
        self.push_ok = True

        def fake_push(title, message, tags, priority=3):
            self.pushes.append((title, message))
            return self.push_ok

        self.check.push = fake_push

    def tearDown(self):
        self.tmp.cleanup()

    def run_with(self, problems):
        with mock.patch.object(self.check, "run_checks", return_value=dict(problems)):
            self.check.hourly()

    def test_problem_is_reported_after_two_runs(self):
        self.run_with({"zpravy": "kanal je stary 20 h"})
        self.assertEqual(self.pushes, [])
        self.run_with({"zpravy": "kanal je stary 21 h"})
        self.assertEqual(len(self.pushes), 1)
        self.assertIn("kanal je stary 21 h", self.pushes[0][1])

    def test_one_off_blip_is_silent(self):
        self.run_with({"disk": "disk je plny z 90 %"})
        self.run_with({})
        self.assertEqual(self.pushes, [])

    def test_reported_problem_is_not_repeated_and_resolves_once(self):
        for _ in range(4):
            self.run_with({"zpravy": "kanal je stary"})
        self.assertEqual(len(self.pushes), 1)
        self.run_with({})
        self.run_with({})
        self.assertEqual(len(self.pushes), 2)
        self.assertIn("vyreseno", self.pushes[1][0])

    def test_failed_push_is_retried_next_run(self):
        self.push_ok = False
        self.run_with({"zpravy": "x"})
        self.run_with({"zpravy": "x"})
        self.push_ok = True
        self.run_with({"zpravy": "x"})
        self.assertEqual(len(self.pushes), 2)
        self.run_with({"zpravy": "x"})
        self.assertEqual(len(self.pushes), 2)

    def test_unit_failure_is_rate_limited(self):
        with mock.patch.object(self.check.subprocess, "run") as run:
            run.return_value.stdout = "Main process exited, code=killed"
            self.check.unit_failed("news.service")
            self.check.unit_failed("news.service")
            self.check.unit_failed("agenda.service")
        self.assertEqual([title for title, _ in self.pushes],
                         ["Selhalo: news.service", "Selhalo: agenda.service"])


if __name__ == "__main__":
    unittest.main()
