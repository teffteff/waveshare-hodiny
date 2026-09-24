#!/usr/bin/env python3
"""Testy brany bez site: python3 -m unittest infra/llm/test_gateway.py"""
import json
import sys
import tempfile
import unittest
from datetime import datetime
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import gateway  # noqa: E402

CONFIG = json.loads((Path(__file__).resolve().parent / "config.json").read_text())
ENV = {"GEMINI_KEY_HODINY": "g-hodiny", "GEMINI_KEY_RADAR": "g-radar",
       "OPENROUTER_API_KEY": "or-key", "LLM_TOKENS": "news:tok-news,radar:tok-radar"}
NOON = datetime(2026, 9, 24, 12, 0, tzinfo=gateway.PACIFIC).timestamp()
SCHEMA = {"type": "object", "properties": {"picks": {"type": "array",
          "items": {"$ref": "#/$defs/Pick"}}}, "required": ["picks"],
          "$defs": {"Pick": {"type": "object", "properties": {"index": {"type": "integer"}}}}}


def request(tier="smart", schema=True):
    body = {"model": tier, "messages": [{"role": "system", "content": "sys"},
                                        {"role": "user", "content": "ahoj"}]}
    if schema:
        body["response_format"] = {"type": "json_schema",
                                   "json_schema": {"name": "p", "schema": SCHEMA}}
    return body


def gemini_ok(text='{"picks": []}'):
    return 200, {"candidates": [{"content": {"parts": [{"text": text}]}}],
                 "usageMetadata": {"promptTokenCount": 10, "candidatesTokenCount": 2}}


def openrouter_ok(text='{"picks": []}', cost=0.001):
    return 200, {"choices": [{"message": {"content": text}}],
                 "usage": {"prompt_tokens": 10, "completion_tokens": 2, "cost": cost}}


GEMINI_DAY_QUOTA = (429, {"error": {"code": 429, "message": "quota", "details": [
    {"violations": [{"quotaId": "GenerateRequestsPerDayPerProjectPerModel-FreeTier"}]}]}})
OVERLOAD = (503, {"error": {"code": 503, "message": "high demand"}})


class FakeUpstream:
    """Odpovedi podle (poskytovatel, model); posledni odpoved se opakuje."""

    def __init__(self, answers):
        self.answers = {k: list(v) for k, v in answers.items()}
        self.calls = []

    def __call__(self, url, body, headers):
        if "openrouter" in url:
            key = ("openrouter", body["model"])
        else:
            key = ("gemini", url.split("/models/")[1].split(":")[0])
        self.calls.append((key, body, headers))
        queue = self.answers.get(key) or [OVERLOAD]
        return queue.pop(0) if len(queue) > 1 else queue[0]


class GatewayTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.now = NOON

    def tearDown(self):
        self.tmp.cleanup()

    def make(self, answers, env=ENV, config=CONFIG):
        self.upstream = FakeUpstream(answers)
        return gateway.Gateway(config, env, Path(self.tmp.name) / "usage.sqlite",
                               post=self.upstream, clock=lambda: self.now, sleep=lambda s: None)

    def test_first_model_answers(self):
        gw = self.make({("gemini", "gemini-flash-latest"): [gemini_ok()]})
        status, body = gw.complete("news", request())
        self.assertEqual(status, 200)
        self.assertEqual(body["model"], "gemini/gemini-flash-latest")
        self.assertEqual(json.loads(body["choices"][0]["message"]["content"]), {"picks": []})
        sent = self.upstream.calls[0]
        self.assertEqual(sent[2]["x-goog-api-key"], "g-hodiny")
        config = sent[1]["generationConfig"]
        self.assertEqual(config["responseMimeType"], "application/json")
        self.assertNotIn("$defs", json.dumps(config["responseJsonSchema"]))
        self.assertEqual(sent[1]["systemInstruction"]["parts"][0]["text"], "sys")

    def test_radar_uses_its_own_project_key(self):
        gw = self.make({("gemini", "gemini-flash-lite-latest"): [gemini_ok()]})
        gw.complete("radar", request("cheap"))
        self.assertEqual(self.upstream.calls[0][2]["x-goog-api-key"], "g-radar")

    def test_overload_everywhere_at_google_falls_back_to_openrouter(self):
        gw = self.make({("openrouter", "openai/gpt-4.1-mini"): [openrouter_ok()]})
        status, body = gw.complete("news", request())
        self.assertEqual(status, 200)
        self.assertEqual(body["model"], "openrouter/openai/gpt-4.1-mini")
        # dva Gemini modely, kazdy dvakrat, pak OpenRouter
        self.assertEqual([c[0][0] for c in self.upstream.calls],
                         ["gemini"] * 4 + ["openrouter"])
        sent = self.upstream.calls[-1][1]
        self.assertTrue(sent["provider"]["require_parameters"])
        self.assertEqual(sent["response_format"]["json_schema"]["schema"], SCHEMA)
        self.assertEqual(gw.status()["openrouter_usd_today"], 0.001)

    def test_all_fail_is_503(self):
        gw = self.make({})
        status, body = gw.complete("news", request())
        self.assertEqual(status, 503)
        self.assertIn("vsechny modely selhaly", body["error"]["message"])
        self.assertEqual(gw.status()["services"]["news"]["failed"], 1)

    def test_daily_quota_blocks_model_for_whole_project_until_pacific_midnight(self):
        gw = self.make({("gemini", "gemini-flash-latest"): [GEMINI_DAY_QUOTA],
                        ("gemini", "gemini-flash-lite-latest"): [gemini_ok()]})
        self.assertEqual(gw.complete("news", request())[0], 200)
        self.upstream.calls.clear()
        self.now += 3600
        self.assertEqual(gw.complete("news", request())[1]["model"],
                         "gemini/gemini-flash-lite-latest")
        self.assertEqual([c[0][1] for c in self.upstream.calls], ["gemini-flash-lite-latest"])
        # po pulnoci tichomorskeho casu zase Flash
        self.upstream.answers[("gemini", "gemini-flash-latest")] = [gemini_ok()]
        self.now = NOON + 13 * 3600
        self.assertEqual(gw.complete("news", request())[1]["model"],
                         "gemini/gemini-flash-latest")

    def test_quota_block_survives_restart(self):
        gw = self.make({("gemini", "gemini-flash-latest"): [GEMINI_DAY_QUOTA],
                        ("gemini", "gemini-flash-lite-latest"): [gemini_ok()]})
        gw.complete("news", request())
        again = self.make({("gemini", "gemini-flash-lite-latest"): [gemini_ok()]})
        again.complete("news", request())
        self.assertEqual([c[0][1] for c in self.upstream.calls], ["gemini-flash-lite-latest"])

    def test_bad_request_returns_at_once(self):
        gw = self.make({("gemini", "gemini-flash-latest"): [(400, {"error": {"message": "bad"}})]})
        status, _ = gw.complete("news", request())
        self.assertEqual(status, 400)
        self.assertEqual(len(self.upstream.calls), 1)

    def test_invalid_json_goes_to_next_model(self):
        gw = self.make({("gemini", "gemini-flash-latest"): [gemini_ok("nejsem json")],
                        ("gemini", "gemini-flash-lite-latest"): [gemini_ok('```json\n{"a": 1}\n```')]})
        status, body = gw.complete("news", request())
        self.assertEqual(status, 200)
        self.assertEqual(body["choices"][0]["message"]["content"], '{"a": 1}')

    def test_openrouter_daily_spend_cap(self):
        gw = self.make({("openrouter", "openai/gpt-4.1-mini"): [openrouter_ok(cost=0.6)]})
        self.assertEqual(gw.complete("news", request())[0], 200)
        self.assertEqual(gw.complete("news", request())[0], 503)
        self.assertEqual(sum(1 for c in self.upstream.calls if c[0][0] == "openrouter"), 1)

    def test_openrouter_402_blocks_provider_and_reports_problem(self):
        gw = self.make({("openrouter", "openai/gpt-4.1-mini"): [
            (402, {"error": {"code": 402, "message": "limit"}})]})
        self.assertEqual(gw.complete("news", request())[0], 503)
        self.assertIn("OpenRouter", gw.status()["problem"])

    def test_service_daily_cap(self):
        config = json.loads(json.dumps(CONFIG))
        config["services"]["news"]["daily_cap"] = 1
        gw = self.make({("gemini", "gemini-flash-latest"): [gemini_ok()]}, config=config)
        self.assertEqual(gw.complete("news", request())[0], 200)
        self.assertEqual(gw.complete("news", request())[0], 429)

    def test_tokens(self):
        gw = self.make({})
        self.assertEqual(gw.service_for("Bearer tok-news"), "news")
        self.assertIsNone(gw.service_for("Bearer nope"))
        self.assertIsNone(gw.service_for(""))

    def test_missing_openrouter_key_is_a_problem(self):
        env = {k: v for k, v in ENV.items() if k != "OPENROUTER_API_KEY"}
        self.assertIn("OPENROUTER_API_KEY", self.make({}, env=env).status()["problem"])

    def test_parse_request(self):
        tiers = CONFIG["tiers"]
        gateway.parse_request(request(), tiers)
        for bad in ({"model": "gpt-4", "messages": [{"role": "user", "content": "x"}]},
                    {"model": "smart", "messages": []},
                    {"model": "smart", "messages": [{"role": "tool", "content": "x"}]},
                    {"model": "smart", "messages": [{"role": "user", "content": "x"}],
                     "response_format": {"type": "json_schema"}}):
            with self.assertRaises(ValueError):
                gateway.parse_request(bad, tiers)


if __name__ == "__main__":
    unittest.main()
