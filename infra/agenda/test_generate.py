"""Testy opakovani dotazu na Google Kalendar bez site a bez knihoven Googlu:
python3 -m unittest infra/agenda/test_generate.py"""
from __future__ import annotations

import importlib.util
import pathlib
import sys
import types
import unittest
from unittest import mock

import requests

HERE = pathlib.Path(__file__).resolve().parent


def load_generate():
    # google-auth v repu neni potreba: get() ho nepouziva, jen ho modul importuje.
    for name in ("google", "google.auth", "google.auth.transport", "google.oauth2"):
        sys.modules.setdefault(name, types.ModuleType(name))
    transport = types.ModuleType("google.auth.transport.requests")
    transport.AuthorizedSession = object
    sys.modules["google.auth.transport.requests"] = transport
    oauth2 = types.ModuleType("google.oauth2.service_account")
    oauth2.Credentials = object
    sys.modules["google.oauth2.service_account"] = oauth2
    sys.modules["google.oauth2"].service_account = oauth2
    sys.path.insert(0, str(HERE))
    spec = importlib.util.spec_from_file_location("agenda_generate", HERE / "generate.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


generate = load_generate()


def response(status: int, retry_after: str = "") -> requests.Response:
    result = requests.Response()
    result.status_code = status
    if retry_after:
        result.headers["Retry-After"] = retry_after
    return result


class FakeSession:
    def __init__(self, outcomes):
        self.outcomes = list(outcomes)
        self.calls = 0

    def get(self, url, timeout, **kwargs):
        self.calls += 1
        outcome = self.outcomes.pop(0)
        if isinstance(outcome, Exception):
            raise outcome
        return outcome


class ModuleTest(unittest.TestCase):
    def test_everything_main_calls_is_defined(self):
        # 22. 9. 2026 upravou vypadla session() a beh spadl az na serveru.
        for name in ("session", "calendar_name", "fetch", "collect", "boundary", "main"):
            self.assertTrue(callable(getattr(generate, name, None)), name)


class RetryTest(unittest.TestCase):
    def setUp(self):
        self.sleeps = []
        patcher = mock.patch.object(generate.time, "sleep", self.sleeps.append)
        patcher.start()
        self.addCleanup(patcher.stop)
        generate._started = generate.time.monotonic()

    def test_single_503_is_retried(self):
        http = FakeSession([response(503), response(200)])
        self.assertEqual(generate.get(http, "https://x/calendars/a").status_code, 200)
        self.assertEqual(self.sleeps, [5])

    def test_404_is_not_retried(self):
        http = FakeSession([response(404)])
        self.assertEqual(generate.get(http, "https://x/calendars/a").status_code, 404)
        self.assertEqual(http.calls, 1)

    def test_persistent_503_returns_last_response(self):
        http = FakeSession([response(503)] * 4)
        self.assertEqual(generate.get(http, "https://x/calendars/a").status_code, 503)
        self.assertEqual(self.sleeps, [5, 15, 30])

    def test_budget_stops_retrying(self):
        generate._started = generate.time.monotonic() - generate.RETRY_BUDGET + 10
        http = FakeSession([response(503), response(503)])
        self.assertEqual(generate.get(http, "https://x/calendars/a").status_code, 503)
        self.assertEqual(self.sleeps, [5])

    def test_connection_error_is_retried_then_raised(self):
        error = requests.ConnectionError("reset")
        http = FakeSession([error, response(200)])
        self.assertEqual(generate.get(http, "https://x/calendars/a").status_code, 200)
        http = FakeSession([error] * 4)
        with self.assertRaises(requests.ConnectionError):
            generate.get(http, "https://x/calendars/a")

    def test_retry_after_is_honoured_up_to_30_s(self):
        http = FakeSession([response(429, "20"), response(200)])
        generate.get(http, "https://x/calendars/a")
        self.assertEqual(self.sleeps, [20])


if __name__ == "__main__":
    unittest.main()
