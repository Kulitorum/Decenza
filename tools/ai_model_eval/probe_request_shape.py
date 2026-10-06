#!/usr/bin/env python3
"""Verify the thinking/reasoning knobs Decenza sends actually work.

Three INVARIANTs are asserted in code but were never checked against the API:

  1. src/ai/airequestshape.h disableAnthropicThinking()
     "assumes every model either caller can select accepts thinking type
     'disabled'" — its own comment notes newer Anthropic models may REJECT the
     disabled form. If so, every Anthropic request 400s.

  2. src/ai/aiprovider.cpp GeminiProvider::sendRequest()
     sends thinkingLevel "minimal" to every non-2.x model. Google's docs say
     valid thinking_level values VARY BY MODEL (gemini-3-pro-preview takes only
     low/high). If 3.5 Flash rejects "minimal" that's a 400; if it ignores it,
     thinking runs at the default and is billed at the output rate.

  3. OpenAI: the advisor sends max_completion_tokens + reasoning_effort "none",
     the translator sends temperature 0.3 with it, and the URL/search features
     use the Responses API with web_search. Parameters are accepted per-model;
     a rejected one 400s every request instead of degrading.

Prints PASS/FAIL per check. Never echoes a key. Costs a few cents.
"""

import json
import subprocess
import sys
import urllib.error
import urllib.request

# The aiprovider.cpp catalogs, plus any candidates being considered. Override
# with --anthropic / --gemini / --openai (comma-separated) to probe others.
ANTHROPIC_MODELS = ["claude-sonnet-5-5", "claude-haiku-4-5"]
GEMINI_MODELS = ["gemini-2.5-flash", "gemini-3.8-flash", "gemini-3.5-flash-lite"]
OPENAI_MODELS = ["gpt-6.1-sol", "gpt-6-luna"]


def setting(key: str) -> str:
    try:
        out = subprocess.run(["defaults", "read", "com.decentespresso.Decenza", key],
                             capture_output=True, text=True, check=True)
        return out.stdout.strip()
    except (subprocess.CalledProcessError, FileNotFoundError):
        return ""


def post(url: str, headers: dict, body: dict):
    req = urllib.request.Request(url, data=json.dumps(body).encode(), headers=headers)
    try:
        with urllib.request.urlopen(req, timeout=90) as r:
            return r.status, json.loads(r.read())
    except urllib.error.HTTPError as e:
        raw = e.read().decode(errors="replace")
        try:
            return e.code, json.loads(raw)
        except json.JSONDecodeError:
            return e.code, {"raw": raw[:300]}
    except Exception as e:                                  # noqa: BLE001
        return 0, {"raw": f"{type(e).__name__}: {e}"}


def msg(payload) -> str:
    if isinstance(payload, dict):
        err = payload.get("error")
        if isinstance(err, dict):
            return err.get("message", json.dumps(payload)[:200])
        if isinstance(err, list) and err:
            return str(err[0])[:200]
    return json.dumps(payload)[:200]


def check_anthropic() -> None:
    key = setting("ai.anthropicKey") or setting("ai.anthropicApiKey")
    print("\n== Anthropic: thinking {type: disabled} ==")
    if not key:
        print("  SKIP — no Anthropic key configured in Decenza")
        return
    for model in ANTHROPIC_MODELS:
        status, payload = post(
            "https://api.anthropic.com/v1/messages",
            {"x-api-key": key, "anthropic-version": "2023-06-01",
             "Content-Type": "application/json"},
            {"model": model, "max_tokens": 64,
             "thinking": {"type": "disabled"},
             "messages": [{"role": "user", "content": "Reply with the single word: ok"}]})
        if status == 200:
            blocks = [b.get("type") for b in payload.get("content", [])]
            has_text = "text" in blocks
            print(f"  {'PASS' if has_text else 'FAIL'}  {model}: blocks={blocks}"
                  f"{'' if has_text else '  <-- no text block (the #1691 symptom)'}")
        else:
            print(f"  FAIL  {model} ({status}): {msg(payload)}")
        # AnthropicProvider::analyzeUrl()/searchWeb(): server tools with thinking disabled.
        for tool, beta in (({"type": "web_fetch_20250910", "name": "web_fetch", "max_uses": 2,
                             "max_content_tokens": 20000}, "web-fetch-2025-09-10"),
                           ({"type": "web_search_20250305", "name": "web_search", "max_uses": 3}, None)):
            headers = {"x-api-key": key, "anthropic-version": "2023-06-01",
                       "Content-Type": "application/json"}
            if beta:
                headers["anthropic-beta"] = beta
            status, payload = post(
                "https://api.anthropic.com/v1/messages", headers,
                {"model": model, "max_tokens": 1024, "thinking": {"type": "disabled"},
                 "tools": [tool],
                 "messages": [{"role": "user", "content": "What is on https://decentespresso.com ? One line."}]})
            print(f"  {'PASS' if status == 200 else 'FAIL'}  {model} + {tool['name']}"
                  f"{'' if status == 200 else f' ({status}): ' + msg(payload)}")


def check_gemini() -> None:
    key = setting("ai.geminiKey") or setting("ai.geminiApiKey")
    print("\n== Gemini: thinkingBudget 0 (2.x) / thinkingLevel minimal (3.x) ==")
    if not key:
        print("  SKIP — no Gemini key configured in Decenza")
        return
    for model in GEMINI_MODELS:
        # Exactly what GeminiProvider::sendRequest() picks by family.
        cfg = ({"thinkingBudget": 0} if model.startswith("gemini-2")
               else {"thinkingLevel": "minimal"})
        status, payload = post(
            f"https://generativelanguage.googleapis.com/v1beta/models/{model}:generateContent",
            {"x-goog-api-key": key, "Content-Type": "application/json"},
            {"contents": [{"parts": [{"text": "Reply with the single word: ok"}]}],
             "generationConfig": {"thinkingConfig": cfg, "maxOutputTokens": 4096}})
        knob = "thinkingBudget=0" if "thinkingBudget" in cfg else 'thinkingLevel="minimal"'
        if status != 200:
            print(f"  FAIL  {model} {knob} ({status}): {msg(payload)}")
            continue
        usage = payload.get("usageMetadata", {})
        thoughts = usage.get("thoughtsTokenCount", 0)
        # Accepted AND actually off is the thing worth knowing: a knob that is
        # silently ignored still bills thinking at the output rate.
        verdict = "PASS" if thoughts == 0 else "FAIL"
        note = "" if thoughts == 0 else "  <-- thinking ran anyway; knob ignored"
        print(f"  {verdict}  {model} {knob}: thoughtsTokenCount={thoughts}{note}")

    # The URL and search tools run alongside the same thinking knob.
    for model in GEMINI_MODELS:
        cfg = ({"thinkingBudget": 0} if model.startswith("gemini-2")
               else {"thinkingLevel": "minimal"})
        for tool in ("url_context", "google_search"):
            status, payload = post(
                f"https://generativelanguage.googleapis.com/v1beta/models/{model}:generateContent",
                {"x-goog-api-key": key, "Content-Type": "application/json"},
                {"contents": [{"parts": [{"text": "What is on https://decentespresso.com ? One line."}]}],
                 "tools": [{tool: {}}],
                 "generationConfig": {"thinkingConfig": cfg, "maxOutputTokens": 4096}})
            print(f"  {'PASS' if status == 200 else 'FAIL'}  {model} + {tool}"
                  f"{'' if status == 200 else f' ({status}): ' + msg(payload)}")


def check_openai() -> None:
    key = setting("ai.openaiKey")
    print("\n== OpenAI: advisor, translator and web-search bodies ==")
    if not key:
        print("  SKIP — no OpenAI key configured in Decenza")
        return
    headers = {"Authorization": "Bearer " + key, "Content-Type": "application/json"}
    ok = [{"role": "user", "content": "Reply with the single word: ok"}]
    for model in OPENAI_MODELS:
        for body, label in (
            # OpenAIProvider::analyze()
            ({"model": model, "max_completion_tokens": 4096, "reasoning_effort": "none",
              "messages": ok}, "advisor: max_completion_tokens + reasoning_effort=none"),
            # TranslationManager
            ({"model": model, "temperature": 0.3, "reasoning_effort": "none",
              "messages": ok}, "translator: temperature=0.3 + reasoning_effort=none"),
        ):
            status, payload = post("https://api.openai.com/v1/chat/completions", headers, body)
            print(f"  {'PASS' if status == 200 else 'FAIL'}  {model} {label}"
                  f"{'' if status == 200 else f' ({status}): ' + msg(payload)}")
        # OpenAIProvider::analyzeUrl()/searchWeb(): Responses API + web_search
        status, payload = post("https://api.openai.com/v1/responses", headers,
                               {"model": model, "input": "What is on https://decentespresso.com ? One line.",
                                "tools": [{"type": "web_search"}], "reasoning": {"effort": "low"},
                                "max_output_tokens": 4096})
        print(f"  {'PASS' if status == 200 else 'FAIL'}  {model} responses + web_search (effort low)"
              f"{'' if status == 200 else f' ({status}): ' + msg(payload)}")


if __name__ == "__main__":
    for i, arg in enumerate(sys.argv[1:-1], start=1):
        value = [m.strip() for m in sys.argv[i + 1].split(",") if m.strip()]
        if arg == "--anthropic": ANTHROPIC_MODELS = value
        elif arg == "--gemini": GEMINI_MODELS = value
        elif arg == "--openai": OPENAI_MODELS = value
    check_anthropic()
    check_gemini()
    check_openai()
    print("\nDone.", file=sys.stderr)
