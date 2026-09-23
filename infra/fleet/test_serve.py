"""Testy bez site: python3 -m unittest infra/fleet/test_serve.py (z korene repa)."""
from __future__ import annotations

import gzip
import hashlib
import http.client
import json
import sys
import tempfile
import threading
import time
import unittest
import urllib.parse
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import serve  # noqa: E402

# RFC 6238, dodatek B: klic "12345678901234567890" v base32.
RFC_SECRET = "GEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQ"
PASSWORD = "spravne-dlouhe-heslo"
PAGE = ('<!doctype html><html><head><meta charset="utf-8"></head><body><header>'
        '<div class="header-actions"></div></header><script src="/ui-language.js">'
        '</script><a href="/diagnostics">d</a><script>fetch("/api/config")</script>'
        '</body></html>')


class TotpTest(unittest.TestCase):
    def test_rfc_vector(self):
        # Osmimistny kod v RFC je 94287082; sestimistny je jeho konec.
        self.assertEqual(serve.totp_code(RFC_SECRET, 59 // 30), "287082")
        self.assertEqual(serve.totp_code(RFC_SECRET, 1111111109 // 30), "081804")

    def test_window(self):
        now = 1_000_000.0
        code = serve.totp_code(RFC_SECRET, int(now // 30) - 1)
        self.assertEqual(serve.totp_match(RFC_SECRET, code, now), int(now // 30) - 1)
        old = serve.totp_code(RFC_SECRET, int(now // 30) - 3)
        self.assertIsNone(serve.totp_match(RFC_SECRET, old, now))
        self.assertIsNone(serve.totp_match(RFC_SECRET, "12345", now))
        self.assertIsNone(serve.totp_match(RFC_SECRET, "abcdef", now))


class PasswordTest(unittest.TestCase):
    def test_round_trip(self):
        stored = serve.hash_password(PASSWORD)
        self.assertTrue(serve.verify_password(PASSWORD, stored))
        self.assertFalse(serve.verify_password(PASSWORD + "x", stored))
        self.assertFalse(serve.verify_password(PASSWORD, "nesmysl"))


class InjectTest(unittest.TestCase):
    def test_prefix_and_shim(self):
        page = serve.inject(PAGE, "kuchyn", "tok</script>", [{"name": "kuchyn"}])
        self.assertIn('src="/fleet/d/kuchyn/ui-language.js"', page)
        self.assertIn('href="/fleet/d/kuchyn/diagnostics"', page)
        # Shim je hned za <head>, pred vlastnimi skripty stranky.
        self.assertLess(page.index("X-Fleet-Csrf"), page.index('fetch("/api/config")'))
        self.assertNotIn("tok</script>", page)


class FakeClock(threading.Thread):
    """Hodiny: ptaji se serveru a odpovidaji jako jejich web."""

    def __init__(self, port: int, token: str):
        super().__init__(daemon=True)
        self.port, self.token = port, token
        self.stop = threading.Event()
        self.seen: list[tuple[str, str, bytes]] = []
        self.tags = True
        self.skipped = 0

    def answer(self, method: str, path: str, body: bytes):
        if path == "/":
            return 200, "text/html; charset=utf-8", "gzip", gzip.compress(PAGE.encode())
        if path == "/api/config" and method == "GET":
            return 200, "application/json", "", b'{"deviceName":"kuchyn"}'
        if path == "/api/config" and method == "POST":
            return 200, "application/json", "", json.dumps({"ok": True, "echo": body.decode()}).encode()
        return 404, "application/json", "", b'{"ok":false}'

    def run(self):
        connection = http.client.HTTPConnection("127.0.0.1", self.port, timeout=10)
        auth = {"Authorization": f"Bearer {self.token}"}
        while not self.stop.is_set():
            connection.request("GET", "/fleet/agent/poll",
                               headers={**auth, "X-Clock-Firmware": "2.3.0",
                                        "X-Clock-Name": "kuchyn"})
            response = connection.getresponse()
            body = response.read()
            if response.status != 200:
                continue
            method = response.getheader("X-Job-Method")
            path = response.getheader("X-Job-Path")
            known = response.getheader("X-Job-If-None-Match") or ""
            self.seen.append((method, path, body))
            status, ctype, encoding, payload = self.answer(method, path, body)
            # Jako firmware: velke odpovedi na GET dostanou otisk; kdyz ho
            # server uz ma, telo se neposila.
            tag = ""
            if self.tags and method == "GET" and status == 200 and len(payload) >= 64:
                tag = hashlib.sha256(payload).hexdigest()[:16]
            unchanged = bool(tag) and tag == known
            if unchanged:
                self.skipped += 1
            connection.request("POST", "/fleet/agent/result",
                               body=b"" if unchanged else payload, headers={
                **auth, "X-Job-Id": response.getheader("X-Job-Id"),
                "X-Job-Status": str(status), "X-Job-Content-Type": ctype,
                "X-Job-Content-Encoding": encoding, "X-Job-Tag": tag,
                "X-Job-Not-Modified": "1" if unchanged else ""})
            response = connection.getresponse()
            response.read()


class ServerTest(unittest.TestCase):
    def setUp(self):
        self.state = tempfile.TemporaryDirectory()
        serve.STATE = Path(self.state.name)
        serve.POLL_WAIT_S = 0.5
        self.token = "t" * 43
        serve.save_devices({"kuchyn": {"token_sha256": serve.token_hash(self.token)},
                            "obyvak": {"token_sha256": serve.token_hash("o" * 43)}})
        self.guard = serve.Guard(serve.hash_password(PASSWORD), RFC_SECRET)
        self.relay = serve.Relay()
        self.server = serve.make_server("127.0.0.1", 0, self.guard, self.relay)
        self.port = self.server.server_address[1]
        threading.Thread(target=self.server.serve_forever, daemon=True).start()
        self.clock = None

    def tearDown(self):
        if self.clock:
            self.clock.stop.set()
            self.clock.join(3)
        self.server.shutdown()
        self.server.server_close()
        self.state.cleanup()

    def request(self, method, path, body=None, headers=None):
        connection = http.client.HTTPConnection("127.0.0.1", self.port, timeout=10)
        connection.request(method, path, body=body, headers=headers or {})
        response = connection.getresponse()
        body = response.read()
        connection.close()
        return response, body

    def login(self, code=None, ip="203.0.113.5"):
        code = code or serve.totp_code(RFC_SECRET, int(time.time() // 30))
        form = urllib.parse.urlencode({"password": PASSWORD, "code": code})
        return self.request("POST", "/fleet/login", form, {
            "Content-Type": "application/x-www-form-urlencoded", "X-Forwarded-For": ip})

    def session(self):
        response, _ = self.login()
        self.assertEqual(response.status, 303)
        cookie = response.getheader("Set-Cookie").split(";")[0]
        return cookie, self.guard.sessions[cookie.split("=", 1)[1]].csrf

    def start_clock(self):
        self.clock = FakeClock(self.port, self.token)
        self.clock.start()
        for _ in range(50):
            if self.relay.device("kuchyn").online(time.monotonic()):
                return
            time.sleep(0.05)
        self.fail("hodiny se nepripojily")

    def test_login_flow_and_code_reuse(self):
        response, body = self.request("GET", "/fleet/")
        self.assertIn("Kód z ověřovací aplikace", body.decode())
        response, _ = self.login()
        self.assertEqual(response.status, 303)
        cookie = response.getheader("Set-Cookie")
        self.assertIn("HttpOnly", cookie)
        self.assertIn("Secure", cookie)
        self.assertIn("SameSite=Strict", cookie)
        # Tentyz kod podruhe neprojde.
        response, _ = self.login()
        self.assertEqual(response.status, 401)

    def test_wrong_code_and_rate_limit(self):
        for _ in range(serve.IP_FAILURES):
            response, _ = self.login(code="000000")
            self.assertEqual(response.status, 401)
        response, _ = self.login()
        self.assertEqual(response.status, 429)
        # Jina IP se prihlasi.
        response, _ = self.login(ip="198.51.100.7")
        self.assertEqual(response.status, 303)

    def test_device_list_needs_login(self):
        response, _ = self.request("GET", "/fleet/d/kuchyn/")
        self.assertEqual(response.status, 303)
        response, _ = self.request("GET", "/fleet/d/kuchyn/api/config")
        self.assertEqual(response.status, 401)

    def test_relay_round_trip(self):
        cookie, csrf = self.session()
        self.start_clock()
        response, body = self.request("GET", "/fleet/d/kuchyn/", headers={"Cookie": cookie})
        self.assertEqual(response.status, 200)
        page = body.decode()
        self.assertIn('src="/fleet/d/kuchyn/ui-language.js"', page)
        self.assertIn(json.dumps(csrf), page)
        self.assertIn("obyvak", page)  # vyber hodin zna i ostatni
        headers = {"Cookie": cookie, "X-Fleet-Csrf": csrf}
        response, body = self.request("GET", "/fleet/d/kuchyn/api/config", headers=headers)
        self.assertEqual(response.status, 200)
        self.assertEqual(json.loads(body), {"deviceName": "kuchyn"})
        response, body = self.request("POST", "/fleet/d/kuchyn/api/config", "a=1&b=2", {
            **headers, "Content-Type": "application/x-www-form-urlencoded"})
        self.assertEqual(json.loads(body)["echo"], "a=1&b=2")
        # Bez tokenu proti CSRF nic neodejde.
        count = len(self.clock.seen)
        response, _ = self.request("POST", "/fleet/d/kuchyn/api/config", "a=1",
                                   {"Cookie": cookie})
        self.assertEqual(response.status, 403)
        response, _ = self.request("POST", "/fleet/d/kuchyn/api/config", "a=1", {
            **headers, "Origin": "https://evil.example"})
        self.assertEqual(response.status, 403)
        self.assertEqual(len(self.clock.seen), count)

    def test_stale_page_reloads_and_expired_session_logs_in(self):
        old_cookie, old_csrf = self.session()
        self.guard.last_counter = 0  # druhe prihlaseni stejnym kodem
        cookie, csrf = self.session()
        self.start_clock()
        # Stranka vykreslena pro starsi relaci, cookie uz je nova.
        response, _ = self.request("GET", "/fleet/d/kuchyn/api/config", headers={
            "Cookie": cookie, "X-Fleet-Csrf": old_csrf})
        self.assertEqual(response.status, 403)
        self.assertEqual(response.getheader("X-Fleet-Action"), "refresh")
        # Stranka si vezme aktualni token relace a pozadavek zopakuje.
        response, body = self.request("GET", "/fleet/api/session", headers={
            "Cookie": cookie, "X-Fleet-Session": "1"})
        self.assertEqual(json.loads(body)["csrf"], csrf)
        response, _ = self.request("GET", "/fleet/api/session", headers={"Cookie": cookie})
        self.assertEqual(response.status, 401)
        response, _ = self.request("GET", "/fleet/api/session", headers={
            "Cookie": cookie, "X-Fleet-Session": "1", "Origin": "https://evil.example"})
        self.assertEqual(response.status, 401)
        # Chybejici token je utok nebo chyba, ne zastarala stranka.
        response, _ = self.request("GET", "/fleet/d/kuchyn/api/config",
                                   headers={"Cookie": cookie})
        self.assertEqual(response.status, 403)
        self.assertIsNone(response.getheader("X-Fleet-Action"))
        response, _ = self.request("GET", "/fleet/d/kuchyn/api/config", headers={
            "Cookie": "__Secure-fleet=neplatna", "X-Fleet-Csrf": csrf})
        self.assertEqual(response.status, 401)
        self.assertEqual(response.getheader("X-Fleet-Action"), "login")
        self.assertEqual(self.clock.seen, [])

    def test_unchanged_page_is_not_uploaded_again(self):
        cookie, csrf = self.session()
        self.start_clock()
        first = self.request("GET", "/fleet/d/kuchyn/", headers={"Cookie": cookie})[1]
        second = self.request("GET", "/fleet/d/kuchyn/", headers={"Cookie": cookie})[1]
        self.assertEqual(first, second)
        self.assertEqual(self.clock.skipped, 1)
        # Kazdy pozadavek pritom do hodin dosel (stav a vedlejsi ucinky plati).
        self.assertEqual([p for _, p, _ in self.clock.seen], ["/", "/"])
        # Male odpovedi se neotiskuji.
        self.request("GET", "/fleet/d/kuchyn/api/config",
                     headers={"Cookie": cookie, "X-Fleet-Csrf": csrf})
        self.request("GET", "/fleet/d/kuchyn/api/config",
                     headers={"Cookie": cookie, "X-Fleet-Csrf": csrf})
        self.assertEqual(self.clock.skipped, 1)

    def test_not_modified_without_cached_copy_fails_cleanly(self):
        device = self.relay.device("kuchyn")
        job = serve.Job("j1", "GET", "/", "", b"")
        device.inflight[job.id] = job
        self.assertTrue(self.relay.complete(device, "j1", 200, "text/html", "", b"",
                                            "0123456789abcdef", True))
        self.assertEqual(job.status, 502)

    def test_blocked_paths_never_reach_clock(self):
        cookie, csrf = self.session()
        self.start_clock()
        headers = {"Cookie": cookie, "X-Fleet-Csrf": csrf}
        for path in ("/api/web-password", "/api/remote-admin", "/api/control/display/off",
                     "/api/auth/login", "/api/../api/config", "/etc/passwd"):
            response, _ = self.request("POST", "/fleet/d/kuchyn" + path, "x=1", headers)
            self.assertEqual(response.status, 403, path)
        self.assertEqual(self.clock.seen, [])

    def test_offline_clock(self):
        cookie, csrf = self.session()
        response, body = self.request("GET", "/fleet/d/obyvak/api/config", headers={
            "Cookie": cookie, "X-Fleet-Csrf": csrf})
        self.assertEqual(response.status, 503)
        self.assertIn("nejsou připojené", json.loads(body)["message"])
        response, body = self.request("GET", "/fleet/d/obyvak/", headers={"Cookie": cookie})
        self.assertEqual(response.status, 503)
        self.assertIn("text/html", response.getheader("Content-Type"))

    def test_agent_needs_token(self):
        response, _ = self.request("GET", "/fleet/agent/poll",
                                   headers={"Authorization": "Bearer spatny"})
        self.assertEqual(response.status, 401)
        # Odpoved na cizi ulohu se nikam nedostane.
        response, _ = self.request("POST", "/fleet/agent/result", b"x", {
            "Authorization": f"Bearer {self.token}", "X-Job-Id": "neexistuje",
            "X-Job-Status": "200"})
        self.assertEqual(response.status, 410)

    def test_removed_device_token_stops_working(self):
        serve.save_devices({"obyvak": {"token_sha256": serve.token_hash("o" * 43)}})
        time.sleep(0.01)
        response, _ = self.request("GET", "/fleet/agent/poll",
                                   headers={"Authorization": f"Bearer {self.token}"})
        self.assertEqual(response.status, 401)


if __name__ == "__main__":
    unittest.main()
