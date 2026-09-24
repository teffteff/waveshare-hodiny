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
ENV = {"GEMINI_KEY_HODINY": "g-hodiny", "GEMINI_KEY_RADAR": "g-radar", "GEMINI_KEY_WATCH": "g-watch",
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

    def test_safety_filters_off_only_for_watch(self):
        gw = self.make({("gemini", "gemini-flash-lite-latest"): [gemini_ok()]})
        gw.tokens["tok-watch"] = "watch"
        gw.complete("watch", request("cheap"))
        gw.complete("radar", request("cheap"))
        watch, radar = (c[1] for c in self.upstream.calls)
        self.assertEqual({s["threshold"] for s in watch["safetySettings"]}, {"BLOCK_NONE"})
        self.assertEqual(self.upstream.calls[0][2]["x-goog-api-key"], "g-watch")
        self.assertNotIn("safetySettings", radar)

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

    def test_overloaded_model_is_skipped_for_a_while(self):
        gw = self.make({("openrouter", "openai/gpt-4.1-mini"): [openrouter_ok()]})
        gw.complete("news", request())
        self.upstream.calls.clear()
        self.now += 60
        gw.complete("news", request())
        self.assertEqual([c[0][0] for c in self.upstream.calls], ["openrouter"])
        self.upstream.calls.clear()
        self.now += gateway.OVERLOAD_COOLDOWN_S
        gw.complete("news", request())
        self.assertEqual(self.upstream.calls[0][0], ("gemini", "gemini-flash-latest"))

    def test_slow_overload_is_not_retried(self):
        gw = self.make({("gemini", "gemini-flash-lite-latest"): [gemini_ok()]})

        def slow(url, body, headers):
            self.now += 40
            return FakeUpstream.__call__(self.upstream, url, body, headers)
        gw.post = slow
        status, body = gw.complete("news", request())
        self.assertEqual(body["model"], "gemini/gemini-flash-lite-latest")
        self.assertEqual([c[0][1] for c in self.upstream.calls],
                         ["gemini-flash-latest", "gemini-flash-lite-latest"])

    def test_call_budget_skips_to_the_last_model(self):
        gw = self.make({("openrouter", "openai/gpt-4.1-mini"): [openrouter_ok()]})

        def very_slow(url, body, headers):
            self.now += gateway.CALL_BUDGET_S + 1
            return FakeUpstream.__call__(self.upstream, url, body, headers)
        gw.post = very_slow
        status, body = gw.complete("news", request())
        self.assertEqual(status, 200)
        self.assertEqual([c[0][1] for c in self.upstream.calls],
                         ["gemini-flash-latest", "openai/gpt-4.1-mini"])

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

    def test_truncated_answer_goes_to_next_model(self):
        cut = (200, {"candidates": [{"content": {"parts": [{"text": "Za tyden napr"}]},
                                     "finishReason": "MAX_TOKENS"}]})
        gw = self.make({("gemini", "gemini-flash-latest"): [cut],
                        ("gemini", "gemini-flash-lite-latest"): [gemini_ok("Hotovo.")]})
        status, body = gw.complete("news", request(schema=False))
        self.assertEqual(body["model"], "gemini/gemini-flash-lite-latest")
        long = (200, {"choices": [{"message": {"content": "Za"}, "finish_reason": "length"}]})
        gw = self.make({("openrouter", "openai/gpt-4.1-mini"): [long]})
        self.assertEqual(gw.complete("news", request(schema=False))[0], 503)

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

    def search_request(self):
        return {"model": "search", "messages": [{"role": "user", "content": "najdi"}]}

    def test_search_tier_sends_the_web_plugin_and_returns_citations(self):
        cites = [{"type": "url_citation", "url_citation": {"url": "https://a", "title": "A"}}]
        gw = self.make({("openrouter", "openai/gpt-4.1-nano"): [(200, {
            "choices": [{"message": {"content": "Nalezeno.", "annotations": cites}}],
            "usage": {"cost": 0.0075}})]})
        status, body = gw.complete("radar", self.search_request())
        self.assertEqual(status, 200)
        self.assertEqual(body["choices"][0]["message"]["annotations"], cites)
        sent = self.upstream.calls[0][1]
        self.assertEqual(sent["plugins"][0]["id"], "web")
        self.assertNotIn("response_format", sent)
        self.assertEqual([c[0][0] for c in self.upstream.calls], ["openrouter"])

    def test_search_tier_only_for_listed_services_and_capped(self):
        gw = self.make({("openrouter", "openai/gpt-4.1-nano"): [openrouter_ok("x")]})
        self.assertEqual(gw.complete("news", self.search_request())[0], 403)
        for _ in range(3):
            self.assertEqual(gw.complete("radar", self.search_request())[0], 200)
        self.assertEqual(gw.complete("radar", self.search_request())[0], 429)
        # the cap is per tier: radar's other tiers still work
        self.assertEqual(gw.complete("radar", request("cheap", schema=False))[0], 200)

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
