#!/usr/bin/env python3
"""Spolecna brana k jazykovym modelum pro sluzby na tomto stroji.

Sluzby (generator zprav, hlidac obchodu, radar, statistiky hodin) se neptaji
Googlu primo, ale tady, a to ve formatu OpenAI chat/completions:

    POST /v1/chat/completions
    Authorization: Bearer <token sluzby>
    {"model": "smart" | "cheap",            uroven, ne jmeno modelu
     "messages": [{"role": "system" | "user" | "assistant", "content": "..."}],
     "response_format": {"type": "json_schema",
                         "json_schema": {"name": "...", "schema": {...}}},
     "temperature": 0.3, "max_tokens": 8192}           vse krome model a messages volitelne

Odpoved je chat.completion jako od OpenAI; "model" v ni rika, kdo opravdu
odpovedel (gemini/gemini-flash-latest, openrouter/openai/gpt-4.1-mini).

Proc brana: 24. 9. 2026 vratil Google 503 ("high demand") vsem modelum ve dvou
behech zprav po sobe a hodiny ukazovaly ctyri hodiny stare zpravy. Kazda
sluzba mela vlastni opakovani, vlastni denni limit a vlastni pamet vycerpane
kvoty. Tady je to na jednom miste:

- Retez modelu podle urovne (config.json): nejdriv Gemini, kdyz je pretizeny
  (5xx, timeout) nebo mu dosla kvota (429), dalsi model a nakonec OpenRouter
  mimo Google, ktery pri vypadku Googlu nepadne s nim.
- Kvota Gemini plati na projekt Google Cloudu, ne na klic. Kazda sluzba ma
  v config.json svuj projekt; vycerpany model (429 s denni kvotou) se v tom
  projektu preskakuje do pulnoci tichomorskeho casu pro vsechny jeho sluzby
  a pamet preziva restart (tabulka blocks).
- Denni strop dotazu na sluzbu (pojistka proti zacyklene sluzbe) a strop
  utraty OpenRouteru za den, navic k mesicnimu limitu na klici.
- Kazdy dotaz a kazdy pokus se zapise do state/usage.sqlite. GET /status
  vraci dnesni prehled pro health a check-stack.

Odpoved s JSON formatem se pred vracenim zkontroluje (json.loads); nevalidni
JSON je selhani modelu a jde se na dalsi. Chybny dotaz (400) se vraci hned:
jiny model by ho nespravil a chyba ve sluzbe se ma ukazat.

Klice a tokeny sluzeb jsou v llm.env (vzor llm.env.example). Ven pres Caddy
brana nevede.
"""
from __future__ import annotations

import argparse
import hmac
import json
import os
import sqlite3
import sys
import threading
import time
import urllib.error
import urllib.request
from dataclasses import dataclass
from datetime import datetime, timedelta
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from zoneinfo import ZoneInfo

# Google nuluje denni kvotu free tieru o pulnoci tichomorskeho casu; dny
# v tabulkach a denni stropy se pocitaji stejne, aby sedely s AI Studiem.
PACIFIC = ZoneInfo("America/Los_Angeles")
GEMINI_URL = "https://generativelanguage.googleapis.com/v1beta/models/{model}:generateContent"
OPENROUTER_URL = "https://openrouter.ai/api/v1/chat/completions"
UPSTREAM_TIMEOUT_S = float(os.environ.get("LLM_UPSTREAM_TIMEOUT", "90"))
# Pretizeni (5xx) se u jednoho modelu zkusi jeste jednou po kratke pauze,
# pak se jde na dalsi. Dlouhe cekani je vec sluzby (zpravy maji RETRY_PAUSES_S).
OVERLOAD_RETRY_PAUSE_S = 3.0
# 429 bez denni kvoty je minutovy limit.
MINUTE_BLOCK_S = 65.0
KEEP_DAYS = 90
MAX_BODY_BYTES = 2_000_000
ROLES = {"system", "user", "assistant"}


def pacific_day(ts: float) -> str:
    return datetime.fromtimestamp(ts, PACIFIC).date().isoformat()


def next_pacific_midnight(ts: float) -> float:
    tomorrow = datetime.fromtimestamp(ts, PACIFIC).date() + timedelta(days=1)
    return datetime(tomorrow.year, tomorrow.month, tomorrow.day, tzinfo=PACIFIC).timestamp()


# --- volani poskytovatelu --------------------------------------------------------

@dataclass
class Answer:
    text: str
    prompt_tokens: int = 0
    completion_tokens: int = 0
    cost: float = 0.0


class Failure(Exception):
    """Jeden pokus selhal. kind: overload, quota, bad_request, other."""

    def __init__(self, kind: str, message: str, block_until: float | None = None,
                 whole_provider: bool = False):
        super().__init__(message)
        self.kind = kind
        self.block_until = block_until
        self.whole_provider = whole_provider


def _parse(raw: bytes) -> dict:
    try:
        value = json.loads(raw)
        return value if isinstance(value, dict) else {"value": value}
    except ValueError:
        return {"error": {"message": raw[:300].decode("utf-8", "replace")}}


def http_post(url: str, body: dict, headers: dict) -> tuple[int, dict]:
    """(status, JSON). Status 0 = spojeni ani nedoslo k odpovedi."""
    request = urllib.request.Request(
        url, data=json.dumps(body).encode("utf-8"), method="POST",
        headers={"Content-Type": "application/json", **headers})
    try:
        with urllib.request.urlopen(request, timeout=UPSTREAM_TIMEOUT_S) as response:
            return response.status, _parse(response.read())
    except urllib.error.HTTPError as error:
        return error.code, _parse(error.read())
    except (urllib.error.URLError, TimeoutError, OSError) as error:
        return 0, {"error": {"message": str(error)}}


def error_message(status: int, payload: dict) -> str:
    error = payload.get("error")
    message = error.get("message") if isinstance(error, dict) else error
    return f"HTTP {status}: {message or json.dumps(payload)[:200]}"[:300]


def classify(status: int, payload: dict, now: float) -> Failure:
    message = error_message(status, payload)
    if status == 429:
        # Gemini posle v details quotaId, u denni kvoty s "PerDay".
        if "PerDay" in json.dumps(payload):
            return Failure("quota", message, next_pacific_midnight(now))
        return Failure("quota", message, now + MINUTE_BLOCK_S)
    if status == 0 or status >= 500:
        return Failure("overload", message)
    if status == 400:
        return Failure("bad_request", message)
    return Failure("other", message)  # 401, 403, 404 (model zrusen) a podobne


def inline_refs(schema, defs=None):
    """JSON schema bez $ref/$defs: Gemini jim rozumi jen zcasti."""
    if isinstance(schema, dict):
        defs = {**(defs or {}), **schema.get("$defs", {})}
        ref = schema.get("$ref")
        if isinstance(ref, str) and ref.startswith("#/$defs/"):
            return inline_refs(defs[ref.split("/")[-1]], defs)
        return {k: inline_refs(v, defs) for k, v in schema.items() if k != "$defs"}
    if isinstance(schema, list):
        return [inline_refs(v, defs) for v in schema]
    return schema


def gemini_body(request: dict) -> dict:
    system = [m["content"] for m in request["messages"] if m["role"] == "system"]
    contents = [{"role": "model" if m["role"] == "assistant" else "user",
                 "parts": [{"text": m["content"]}]}
                for m in request["messages"] if m["role"] != "system"]
    config: dict = {}
    if "temperature" in request:
        config["temperature"] = request["temperature"]
    if request.get("max_tokens"):
        config["maxOutputTokens"] = request["max_tokens"]
    fmt = request.get("response_format") or {}
    if fmt.get("type") in ("json_object", "json_schema"):
        config["responseMimeType"] = "application/json"
    if fmt.get("type") == "json_schema":
        config["responseJsonSchema"] = inline_refs(fmt["json_schema"]["schema"])
    body = {"contents": contents, "generationConfig": config}
    if system:
        body["systemInstruction"] = {"parts": [{"text": "\n\n".join(system)}]}
    return body


def call_gemini(model: str, key: str, request: dict, post, now: float) -> Answer:
    status, payload = post(GEMINI_URL.format(model=model), gemini_body(request),
                           {"x-goog-api-key": key})
    if status != 200:
        raise classify(status, payload, now)
    candidates = payload.get("candidates") or []
    parts = (candidates[0].get("content") or {}).get("parts", []) if candidates else []
    text = "".join(p.get("text", "") for p in parts if not p.get("thought"))
    if not text:
        reason = candidates[0].get("finishReason") if candidates else payload.get("promptFeedback")
        raise Failure("other", f"prazdna odpoved ({reason})")
    usage = payload.get("usageMetadata") or {}
    return Answer(text, usage.get("promptTokenCount", 0), usage.get("candidatesTokenCount", 0))


def call_openrouter(model: str, key: str, request: dict, post, now: float) -> Answer:
    body = {k: v for k, v in request.items()
            if k in ("messages", "temperature", "max_tokens", "response_format")}
    # require_parameters: jen poskytovatele, kteri umi response_format, jinak
    # by OpenRouter schema potichu zahodil. usage.include vrati cenu dotazu.
    body.update(model=model, provider={"require_parameters": True}, usage={"include": True})
    status, payload = post(OPENROUTER_URL, body,
                           {"Authorization": f"Bearer {key}", "X-Title": "hodiny-llm"})
    if status == 200 and payload.get("choices"):
        text = (payload["choices"][0].get("message") or {}).get("content") or ""
        if not text:
            raise Failure("other", "prazdna odpoved")
        usage = payload.get("usage") or {}
        return Answer(text, usage.get("prompt_tokens", 0), usage.get("completion_tokens", 0),
                      float(usage.get("cost") or 0.0))
    if status == 200:  # chyba poskytovatele za OpenRouterem prijde i s 200
        code = (payload.get("error") or {}).get("code")
        status = code if isinstance(code, int) else 502
    if status == 402:
        # Dosel kredit nebo mesicni limit klice: do zitrka se OpenRouter neptat.
        raise Failure("quota", error_message(402, payload), next_pacific_midnight(now),
                      whole_provider=True)
    if status == 429:
        raise Failure("quota", error_message(429, payload), now + MINUTE_BLOCK_S)
    raise classify(status, payload, now)


PROVIDERS = {"gemini": call_gemini, "openrouter": call_openrouter}


def strip_fences(text: str) -> str:
    text = text.strip()
    if text.startswith("```"):
        text = text.split("\n", 1)[1] if "\n" in text else ""
        text = text.rsplit("```", 1)[0]
    return text.strip()


# --- brana -----------------------------------------------------------------------

def parse_tokens(value: str) -> dict[str, str]:
    """LLM_TOKENS="news:abc,watch:def" -> {token: sluzba}."""
    tokens = {}
    for pair in value.split(","):
        service, _, token = pair.strip().partition(":")
        if service and token:
            tokens[token] = service
    return tokens


def parse_request(body: dict, tiers: dict) -> dict:
    if not isinstance(body, dict):
        raise ValueError("telo neni JSON objekt")
    if body.get("model") not in tiers:
        raise ValueError(f"model musi byt jedna z urovni {sorted(tiers)}")
    messages = body.get("messages")
    if not isinstance(messages, list) or not messages:
        raise ValueError("messages chybi")
    for message in messages:
        if (not isinstance(message, dict) or message.get("role") not in ROLES
                or not isinstance(message.get("content"), str)):
            raise ValueError("kazda zprava potrebuje role (system/user/assistant) a textovy content")
    fmt = body.get("response_format")
    if fmt is not None:
        if not isinstance(fmt, dict) or fmt.get("type") not in ("text", "json_object", "json_schema"):
            raise ValueError("response_format.type musi byt text, json_object nebo json_schema")
        if fmt["type"] == "json_schema" and not isinstance(
                (fmt.get("json_schema") or {}).get("schema"), dict):
            raise ValueError("json_schema.schema chybi")
    if "temperature" in body and not isinstance(body["temperature"], (int, float)):
        raise ValueError("temperature musi byt cislo")
    if "max_tokens" in body and not isinstance(body["max_tokens"], int):
        raise ValueError("max_tokens musi byt cele cislo")
    return body


def error_body(message: str, kind: str) -> dict:
    return {"error": {"message": message, "type": kind}}


class Gateway:
    def __init__(self, config: dict, env, db_path, post=http_post, clock=time.time,
                 sleep=time.sleep):
        self.tiers = config["tiers"]
        self.services = config["services"]
        self.usd_per_day = float(config.get("openrouter_usd_per_day", 0.5))
        self.keys = {("gemini", project): env.get(var, "").strip()
                     for project, var in config["projects"].items()}
        self.openrouter_key = env.get("OPENROUTER_API_KEY", "").strip()
        self.tokens = parse_tokens(env.get("LLM_TOKENS", ""))
        self.post, self.clock, self.sleep = post, clock, sleep
        self.lock = threading.Lock()
        self.db = sqlite3.connect(str(db_path), check_same_thread=False)
        self.pruned_day = ""
        with self.db:
            self.db.executescript("""
                CREATE TABLE IF NOT EXISTS calls (
                    ts REAL NOT NULL, day TEXT NOT NULL, service TEXT NOT NULL,
                    tier TEXT, outcome TEXT NOT NULL,   -- ok, failed, capped, bad_request
                    answered_by TEXT, ms INTEGER);
                CREATE INDEX IF NOT EXISTS calls_day ON calls (day, service);
                CREATE TABLE IF NOT EXISTS attempts (
                    ts REAL NOT NULL, day TEXT NOT NULL, service TEXT NOT NULL,
                    provider TEXT NOT NULL, model TEXT NOT NULL,
                    outcome TEXT NOT NULL,              -- ok nebo Failure.kind
                    detail TEXT, ms INTEGER, prompt_tokens INTEGER,
                    completion_tokens INTEGER, cost REAL);
                CREATE INDEX IF NOT EXISTS attempts_day ON attempts (day);
                CREATE TABLE IF NOT EXISTS blocks (
                    target TEXT PRIMARY KEY, until REAL NOT NULL, reason TEXT);
            """)

    # -- databaze
    def _query(self, sql: str, args=()) -> list:
        with self.lock:
            return self.db.execute(sql, args).fetchall()

    def _write(self, sql: str, args=()) -> None:
        with self.lock, self.db:
            self.db.execute(sql, args)

    def service_for(self, authorization: str) -> str | None:
        token = authorization.removeprefix("Bearer ").strip()
        for known, service in self.tokens.items():
            if token and hmac.compare_digest(token, known):
                return service
        return None

    def blocked(self, target: str, now: float) -> str | None:
        rows = self._query("SELECT reason FROM blocks WHERE target = ? AND until > ?", (target, now))
        return rows[0][0] if rows else None

    def openrouter_spent(self, day: str) -> float:
        return self._query("SELECT COALESCE(SUM(cost), 0) FROM attempts WHERE day = ? "
                           "AND provider = 'openrouter'", (day,))[0][0]

    def _log_call(self, now, day, service, tier, outcome, answered_by, started) -> None:
        self._write("INSERT INTO calls VALUES (?, ?, ?, ?, ?, ?, ?)",
                    (now, day, service, tier, outcome, answered_by,
                     round((self.clock() - started) * 1000)))
        print(f"{service} {tier}: {outcome}" + (f" ({answered_by})" if answered_by else ""),
              flush=True)

    def _prune(self, now: float, day: str) -> None:
        if day == self.pruned_day:
            return
        self.pruned_day = day
        limit = now - KEEP_DAYS * 86400
        for table in ("calls", "attempts"):
            self._write(f"DELETE FROM {table} WHERE ts < ?", (limit,))
        self._write("DELETE FROM blocks WHERE until < ?", (now,))

    # -- jeden dotaz sluzby
    def complete(self, service: str, request: dict) -> tuple[int, dict]:
        started = now = self.clock()
        day = pacific_day(now)
        self._prune(now, day)
        tier = request["model"]
        settings = self.services[service]
        calls = self._query("SELECT COUNT(*) FROM calls WHERE day = ? AND service = ?",
                            (day, service))[0][0]
        if calls >= settings["daily_cap"]:
            self._log_call(now, day, service, tier, "capped", None, started)
            return 429, error_body(f"sluzba {service} vycerpala denni strop "
                                   f"{settings['daily_cap']} dotazu", "daily_cap")
        wants_json = (request.get("response_format") or {}).get("type") in ("json_object",
                                                                             "json_schema")
        errors = []
        for step in self.tiers[tier]:
            provider, model = step.split(":", 1)
            if provider == "gemini":
                key = self.keys.get(("gemini", settings["project"]), "")
                target = f"gemini:{settings['project']}:{model}"
            else:
                key = self.openrouter_key
                target = f"openrouter:{model}"
            reason = self.blocked(target, now) or (
                provider == "openrouter" and self.blocked("openrouter", now))
            if not key or reason:
                errors.append(f"{provider}/{model}: {reason or 'chybi klic'}")
                continue
            if provider == "openrouter" and self.openrouter_spent(day) >= self.usd_per_day:
                errors.append(f"{provider}/{model}: denni strop {self.usd_per_day} USD")
                continue
            for attempt in range(2):
                attempt_start = self.clock()
                try:
                    answer = PROVIDERS[provider](model, key, request, self.post, self.clock())
                    if wants_json:
                        answer.text = strip_fences(answer.text)
                        try:
                            json.loads(answer.text)
                        except ValueError:
                            raise Failure("other", "odpoved neni platny JSON") from None
                except Failure as failure:
                    self._write("INSERT INTO attempts VALUES (?, ?, ?, ?, ?, ?, ?, ?, 0, 0, 0)",
                                (self.clock(), day, service, provider, model, failure.kind,
                                 str(failure)[:300],
                                 round((self.clock() - attempt_start) * 1000)))
                    if failure.kind == "bad_request":
                        self._log_call(now, day, service, tier, "bad_request",
                                       f"{provider}/{model}", started)
                        return 400, error_body(f"{provider}/{model}: {failure}", "bad_request")
                    if failure.block_until:
                        self._write("INSERT OR REPLACE INTO blocks VALUES (?, ?, ?)",
                                    ("openrouter" if failure.whole_provider else target,
                                     failure.block_until, str(failure)[:200]))
                    if failure.kind == "overload" and attempt == 0:
                        self.sleep(OVERLOAD_RETRY_PAUSE_S)
                        continue
                    errors.append(f"{provider}/{model}: {failure}")
                    print(f"{service} {tier}: {provider}/{model} selhal: {failure}", flush=True)
                    break
                self._write("INSERT INTO attempts VALUES (?, ?, ?, ?, ?, 'ok', NULL, ?, ?, ?, ?)",
                            (self.clock(), day, service, provider, model,
                             round((self.clock() - attempt_start) * 1000),
                             answer.prompt_tokens, answer.completion_tokens, answer.cost))
                answered_by = f"{provider}/{model}"
                self._log_call(now, day, service, tier, "ok", answered_by, started)
                return 200, {
                    "id": f"llm-{int(now * 1000)}",
                    "object": "chat.completion",
                    "created": int(now),
                    "model": answered_by,
                    "choices": [{"index": 0, "finish_reason": "stop",
                                 "message": {"role": "assistant", "content": answer.text}}],
                    "usage": {"prompt_tokens": answer.prompt_tokens,
                              "completion_tokens": answer.completion_tokens,
                              "total_tokens": answer.prompt_tokens + answer.completion_tokens,
                              "cost": answer.cost},
                }
        self._log_call(now, day, service, tier, "failed", None, started)
        return 503, error_body("vsechny modely selhaly: " + "; ".join(errors), "unavailable")

    # -- prehled
    def status(self) -> dict:
        now = self.clock()
        day = pacific_day(now)
        services: dict = {name: {"calls": 0, "failed": 0, "cap": s["daily_cap"],
                                 "answered_by": {}}
                          for name, s in self.services.items()}
        for service, outcome, answered_by, count in self._query(
                "SELECT service, outcome, answered_by, COUNT(*) FROM calls WHERE day = ? "
                "GROUP BY 1, 2, 3", (day,)):
            entry = services.setdefault(service, {"calls": 0, "failed": 0, "answered_by": {}})
            entry["calls"] += count
            if outcome != "ok":
                entry["failed"] += count
            if answered_by and outcome == "ok":
                entry["answered_by"][answered_by] = count
        blocks = {target: {"until": datetime.fromtimestamp(until, PACIFIC).isoformat(),
                           "reason": reason}
                  for target, until, reason in self._query(
                      "SELECT target, until, reason FROM blocks WHERE until > ?", (now,))}
        spent = self.openrouter_spent(day)
        problems = []
        if not self.openrouter_key:
            problems.append("chybi OPENROUTER_API_KEY, zaloha mimo Google nejede")
        elif "openrouter" in blocks:
            problems.append(f"OpenRouter odmita (kredit nebo limit klice): "
                            f"{blocks['openrouter']['reason']}")
        missing = sorted(project for (_, project), key in self.keys.items() if not key)
        if missing:
            problems.append(f"chybi klic Gemini pro projekt {', '.join(missing)}")
        return {"day": day, "services": services, "blocks": blocks,
                "openrouter_usd_today": round(spent, 4),
                "openrouter_usd_per_day": self.usd_per_day,
                "problem": "; ".join(problems)}


# --- HTTP ------------------------------------------------------------------------

def make_handler(gateway: Gateway):
    class Handler(BaseHTTPRequestHandler):
        server_version = "hodiny-llm/1.0"

        def _json(self, status: int, body: dict) -> None:
            data = json.dumps(body, ensure_ascii=False).encode("utf-8")
            self.send_response(status)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)

        def do_GET(self) -> None:  # noqa: N802
            if self.path == "/status":
                self._json(200, gateway.status())
            else:
                self._json(404, error_body("nenalezeno", "not_found"))

        def do_POST(self) -> None:  # noqa: N802
            if self.path != "/v1/chat/completions":
                self._json(404, error_body("nenalezeno", "not_found"))
                return
            service = gateway.service_for(self.headers.get("Authorization", ""))
            if service is None or service not in gateway.services:
                self._json(401, error_body("neznamy token sluzby", "unauthorized"))
                return
            length = int(self.headers.get("Content-Length") or 0)
            if not 0 < length <= MAX_BODY_BYTES:
                self._json(400, error_body("telo chybi nebo je moc velke", "bad_request"))
                return
            try:
                request = parse_request(json.loads(self.rfile.read(length)), gateway.tiers)
            except ValueError as error:
                self._json(400, error_body(str(error), "bad_request"))
                return
            self._json(*gateway.complete(service, request))

        def log_message(self, *args) -> None:
            pass  # kazdy dotaz uz zapisuje Gateway._log_call

    return Handler


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--config", default=os.environ.get("LLM_CONFIG", "/opt/llm/config.json"))
    parser.add_argument("--state", default=os.environ.get("LLM_STATE", "/opt/llm/state"))
    parser.add_argument("--bind", default=os.environ.get("LLM_BIND", "127.0.0.1"))
    parser.add_argument("--port", type=int, default=int(os.environ.get("LLM_PORT", "8102")))
    args = parser.parse_args()
    config = json.loads(Path(args.config).read_text(encoding="utf-8"))
    gateway = Gateway(config, os.environ, Path(args.state) / "usage.sqlite")
    status = gateway.status()
    if status["problem"]:
        print(f"pozor: {status['problem']}", file=sys.stderr, flush=True)
    server = ThreadingHTTPServer((args.bind, args.port), make_handler(gateway))
    print(f"brana na {args.bind}:{args.port}, sluzby {sorted(gateway.services)}", flush=True)
    server.serve_forever()


if __name__ == "__main__":
    main()
