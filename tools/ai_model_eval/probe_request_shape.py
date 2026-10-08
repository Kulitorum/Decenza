#!/usr/bin/env python3
"""Verify the thinking/reasoning settings Decenza sends are accepted.

Sends each catalog model the exact settings src/ai/airequestshape.h gives it
(mirrored below; keep them in step), on the advisor, translator and web-tool
request bodies. A rejected setting 400s every request instead of degrading, so
probe a model here before adding it to a catalog.

Prints PASS/FAIL per check. Never echoes a key. Costs a few cents.
"""

import json
import subprocess
import sys
import urllib.error
import urllib.request

# The aiprovider.cpp catalogs. Override with --anthropic / --gemini / --openai /
# --openrouter (comma-separated) to probe candidates.
ANTHROPIC_MODELS = ["claude-sonnet-5-5", "claude-haiku-5-5"]
GEMINI_MODELS = ["gemini-3.8-flash"]
OPENAI_MODELS = ["gpt-6.1-sol", "gpt-6-luna"]
OPENROUTER_MODELS = ["openai/gpt-6-luna", "openai/gpt-6.1-sol", "anthropic/claude-sonnet-5.5",
                     "anthropic/claude-haiku-5.5",
                     "google/gemini-3.8-flash", "z-ai/glm-5.3-flash", "google/gemma-4-31b-it"]


# Mirrors of src/ai/airequestshape.h.
def anthropic_thinking(model: str) -> dict:
    return {"type": "between_tools" if model == "claude-sonnet-5-5" else "disabled"}


def anthropic_advisor_thinking(model: str) -> dict:
    """setAnthropicAdvisorThinking: analyze()/analyzeConversation() only."""
    if model == "claude-haiku-5-5":
        return {"thinking": {"type": "adaptive"}, "output_config": {"effort": "low"}}
    return {"thinking": anthropic_thinking(model)}


def gemini_thinking(model: str) -> dict:
    return {"thinkingBudget": 0} if model.startswith("gemini-2") else {"thinkingLevel": "low"}


def openai_effort(model: str) -> str:
    return "low" if model == "gpt-6.1-sol" else "none"


def openai_takes_temperature(model: str) -> bool:
    return model != "gpt-6.1-sol"


def openrouter_reasoning(model: str) -> dict:
    if model == "google/gemma-4-31b-it":
        return {"enabled": True}
    return {"effort": "none" if model == "openai/gpt-6-luna" else "low"}


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
    print("\n== Anthropic: the app's thinking setting ==")
    if not key:
        print("  SKIP — no Anthropic key configured in Decenza")
        return
    for model in ANTHROPIC_MODELS:
        status, payload = post(
            "https://api.anthropic.com/v1/messages",
            {"x-api-key": key, "anthropic-version": "2023-06-01",
             "Content-Type": "application/json"},
            {"model": model, "max_tokens": 64,
             "thinking": anthropic_thinking(model),
             "messages": [{"role": "user", "content": "Reply with the single word: ok"}]})
        if status == 200:
            blocks = [b.get("type") for b in payload.get("content", [])]
            has_text = "text" in blocks
            print(f"  {'PASS' if has_text else 'FAIL'}  {model}: blocks={blocks}"
                  f"{'' if has_text else '  <-- no text block (the #1691 symptom)'}")
        else:
            print(f"  FAIL  {model} ({status}): {msg(payload)}")
        # analyze()/analyzeConversation(): the advisor's setting at its 4,096 cap.
        status, payload = post(
            "https://api.anthropic.com/v1/messages",
            {"x-api-key": key, "anthropic-version": "2023-06-01",
             "Content-Type": "application/json"},
            {"model": model, "max_tokens": 4096, **anthropic_advisor_thinking(model),
             "messages": [{"role": "user", "content": "Reply with the single word: ok"}]})
        blocks = [b.get("type") for b in payload.get("content", [])] if status == 200 else []
        print(f"  {'PASS' if 'text' in blocks else 'FAIL'}  {model} advisor: "
              f"{status} blocks={blocks}{'' if status == 200 else ' ' + msg(payload)}")
        # AnthropicProvider::analyzeUrl()/searchWeb(): server tools, thinking off.
        for tool, beta in (({"type": "web_fetch_20250910", "name": "web_fetch", "max_uses": 2,
                             "max_content_tokens": 20000}, "web-fetch-2025-09-10"),
                           ({"type": "web_search_20250305", "name": "web_search", "max_uses": 3}, None)):
            headers = {"x-api-key": key, "anthropic-version": "2023-06-01",
                       "Content-Type": "application/json"}
            if beta:
                headers["anthropic-beta"] = beta
            status, payload = post(
                "https://api.anthropic.com/v1/messages", headers,
                {"model": model, "max_tokens": 1024, "thinking": anthropic_thinking(model),
                 "tools": [tool],
                 "messages": [{"role": "user", "content": "What is on https://decentespresso.com ? One line."}]})
            print(f"  {'PASS' if status == 200 else 'FAIL'}  {model} + {tool['name']}"
                  f"{'' if status == 200 else f' ({status}): ' + msg(payload)}")


def check_gemini() -> None:
    key = setting("ai.geminiKey") or setting("ai.geminiApiKey")
    print("\n== Gemini: thinkingBudget 0 (2.x) / thinkingLevel low (3.x) ==")
    if not key:
        print("  SKIP — no Gemini key configured in Decenza")
        return
    for model in GEMINI_MODELS:
        cfg = gemini_thinking(model)
        status, payload = post(
            f"https://generativelanguage.googleapis.com/v1beta/models/{model}:generateContent",
            {"x-goog-api-key": key, "Content-Type": "application/json"},
            {"contents": [{"parts": [{"text": "Reply with the single word: ok"}]}],
             "generationConfig": {"thinkingConfig": cfg, "maxOutputTokens": 4096}})
        knob = json.dumps(cfg)
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
        cfg = gemini_thinking(model)
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
        effort = openai_effort(model)
        translator = {"model": model, "reasoning_effort": effort, "messages": ok}
        if openai_takes_temperature(model):
            translator["temperature"] = 0.3
        for body, label in (
            # OpenAIProvider::analyze()
            ({"model": model, "max_completion_tokens": 4096, "reasoning_effort": effort,
              "messages": ok}, f"advisor: reasoning_effort={effort}"),
            # TranslationManager
            (translator, f"translator: reasoning_effort={effort}"
                         + (" + temperature=0.3" if "temperature" in translator else "")),
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


def check_openrouter() -> None:
    key = setting("ai.openrouterKey")
    print("\n== OpenRouter: the app's reasoning setting ==")
    if not key:
        print("  SKIP — no OpenRouter key configured in Decenza")
        return
    headers = {"Authorization": "Bearer " + key, "Content-Type": "application/json"}
    for model in OPENROUTER_MODELS:
        # analyze() at the advisor cap, and testConnection()'s 10-token request.
        for max_tokens in (4096, 10):
            status, payload = post("https://openrouter.ai/api/v1/chat/completions", headers,
                                   {"model": model, "max_tokens": max_tokens,
                                    "reasoning": openrouter_reasoning(model),
                                    "messages": [{"role": "user", "content": "Reply with the single word: ok"}]})
            print(f"  {'PASS' if status == 200 else 'FAIL'}  {model} max_tokens={max_tokens}"
                  f"{'' if status == 200 else f' ({status}): ' + msg(payload)}")


if __name__ == "__main__":
    for i, arg in enumerate(sys.argv[1:-1], start=1):
        value = [m.strip() for m in sys.argv[i + 1].split(",") if m.strip()]
        if arg == "--anthropic": ANTHROPIC_MODELS = value
        elif arg == "--gemini": GEMINI_MODELS = value
        elif arg == "--openai": OPENAI_MODELS = value
        elif arg == "--openrouter": OPENROUTER_MODELS = value
    check_anthropic()
    check_gemini()
    check_openai()
    check_openrouter()
    print("\nDone.", file=sys.stderr)
