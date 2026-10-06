#!/usr/bin/env python3
"""Replay the app's real advisor prompts against candidate models.

Two modes:

  blind     — prose quality. Responses are written under shuffled labels so the
              judging pass cannot see which model produced which answer.
  emission  — does the model emit the trailing `nextShot` JSON block when the
              system prompt requires it, and is the block well-formed?

Both replay prompts captured from the running app (see README: capture step),
using the request shape each provider's analyze() builds. Reconstructing the
prompt in this script instead would guarantee drift from the shipped one.

Usage:
    python3 replay.py capture-help
    python3 replay.py emission --models gpt-6.1-sol,gpt-6-luna
    python3 replay.py blind    --models gpt-6.1-sol,openai/gpt-6-luna
    python3 replay.py emission --models gpt-6-luna --efforts none,low
    python3 replay.py reveal   --run blind

API keys: $OPENAI_API_KEY / $ANTHROPIC_API_KEY / $GEMINI_API_KEY /
$OPENROUTER_API_KEY, else
Decenza's own settings on macOS.
"""

import argparse
import json
import os
import random
import re
import subprocess
import sys
import urllib.error
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
CAPTURED = os.path.join(HERE, "captured")   # gitignored; see README
RESULTS = os.path.join(HERE, "runs")        # gitignored

# Per-1M-token (input, output), standard tier, short context. THESE ROT.
# Verify at https://developers.openai.com/api/docs/pricing before trusting a
# cost figure — third-party pricing pages were checked on 2026-07-30 and found
# wrong (one listed Terra at $2.50/$15 against an actual $2.00/$12).
# Anthropic: https://platform.claude.com/docs/en/models/overview ; Gemini:
# https://ai.google.dev/gemini-api/docs/pricing (checked 2026-10-05).
PRICES = {
    "gpt-6.1-sol":   (2.00, 10.00),
    "gpt-6-sol":     (2.00, 10.00),
    "gpt-6-luna":    (0.10, 0.50),
    "gpt-5.6-sol":   (4.00, 20.00),
    "gpt-5.6-terra": (2.00, 12.00),
    "gpt-5.6-luna":  (0.20, 1.20),
    "gpt-5.4":       (2.50, 15.00),
    "gpt-5.4-mini":  (0.75, 4.50),
    "gpt-5.4-nano":  (0.20, 1.25),
    "claude-opus-5-5":   (4.00, 20.00),
    "claude-sonnet-5-5": (2.00, 10.00),
    "claude-sonnet-5":   (2.00, 10.00),
    "claude-haiku-4-5":  (1.00, 5.00),
    "gemini-3.8-flash":      (0.75, 3.75),
    "gemini-3.5-flash":      (1.50, 9.00),
    "gemini-3.5-flash-lite": (0.30, 2.50),
    "gemini-3.1-flash-lite": (0.25, 1.50),
    "gemini-2.5-flash":      (0.30, 2.50),
}

# The thinking knob the app sends per model (src/ai/airequestshape.h). Anthropic
# and Gemini have no effort axis here: each model gets the one the app sends.
def anthropic_thinking(model: str) -> dict:
    """The thinking fields for the body. Opus 5.5 cannot switch thinking off;
    adaptive at low effort is the closest it gets."""
    if model == "claude-opus-5-5":
        return {"output_config": {"effort": "low"}}
    return {"thinking": {"type": "between_tools"} if model == "claude-sonnet-5-5" else {"type": "disabled"}}

def gemini_thinking(model: str) -> dict:
    if model.startswith("gemini-2"):
        return {"thinkingBudget": 0}
    return {"thinkingLevel": "low" if model == "gemini-3.8-flash" else "minimal"}

def provider_of(model: str) -> str:
    if "/" in model:                       # OpenRouter ids are vendor/model
        return "openrouter"
    return "anthropic" if model.startswith("claude-") else "gemini" if model.startswith("gemini-") else "openai"

# AIRequestShape::setOpenRouterReasoning: "none" for Luna, "enabled" for
# Gemma, "low" for the app's other OpenRouter models. A candidate outside the
# catalog is sent nothing, i.e. the model's own default.
OPENROUTER_CATALOG_LOW = {"openai/gpt-6.1-sol", "anthropic/claude-sonnet-5.5",
                          "google/gemini-3.8-flash", "z-ai/glm-5.3-flash"}

# Mirror of OpenAIProvider::analyze() — keep in step with src/ai/aiprovider.cpp.
MAX_OUTPUT_TOKENS = 4096          # src/ai/aiprovider.h MAX_OUTPUT_TOKENS
DEFAULT_EFFORT = "app"            # each model's own setting, as below


def openai_app_effort(model: str) -> str:
    """AIRequestShape::disableOpenAIReasoning: gpt-6.1-sol takes no "none"."""
    return "low" if model == "gpt-6.1-sol" else "none"

# structuredNext contract — src/ai/shotsummarizer.cpp, "Response Format".
REQUIRED_FIELDS = ["expectedDurationSec", "expectedFlowMlPerSec",
                   "successCondition", "reasoning"]


def extract_structured_next(text):
    """Port of AIManager::parseStructuredNext (src/ai/aimanager.cpp).

    Must match the app exactly, or the harness measures a block the app would
    reject. A naive "first ```json fence" regex does NOT: the app takes the
    LAST fence pair and requires its closer to be the final non-whitespace
    content, so a model that echoes an example block mid-prose and emits no
    trailing block scores a block here and none in the app.

    Returns (obj, reason). obj is None when no acceptable block exists, and
    reason says which rule rejected it.
    """
    if not text:
        return None, "empty response"

    fences, pos = [], 0
    while True:
        i = text.find("```", pos)
        if i < 0:
            break
        fences.append(i)
        pos = i + 3
    if len(fences) < 2:
        return None, "no fenced block"

    # Last two fences unconditionally — an odd count (a stray ``` earlier in
    # the prose) must not silently drop a structurally valid trailing block.
    opener, closer = fences[-2], fences[-1]

    if text[closer + 3:].strip():
        return None, "block is not trailing (content after closing fence)"

    nl = text.find("\n", opener + 3)
    if nl < 0 or nl >= closer:
        return None, "malformed opening fence"
    if text[opener + 3:nl].strip().lower() != "json":
        return None, "trailing fence is not tagged json"

    inner = text[nl + 1:closer].strip()
    if not inner:
        return None, "empty block body"
    try:
        obj = json.loads(inner)
    except json.JSONDecodeError as e:
        return None, f"invalid JSON ({str(e)[:60]})"
    # The app requires an OBJECT (aimanager.cpp: `doc.isObject()`); an array or
    # scalar is rejected there. Without this the harness would score such a
    # block as accepted, and audit_block()'s obj.get() would then raise on a
    # non-dict — outside call()'s try/except, aborting a paid run.
    if not isinstance(obj, dict):
        return None, f"block is a {type(obj).__name__}, not an object"
    return obj, ""

CAPTURE_HELP = """\
Capture step — do this before any replay.

The prompts must come from the running app, never be reconstructed here.
For each scenario in scenarios.json, call the MCP tool:

    ai_advisor_invoke  { "shot_id": <id>, "dryRun": true }

dryRun assembles the real system + user prompt with NO network call, NO token
cost, and NO conversation side effects. The result exceeds the tool's inline
limit and is written to a file; copy each to:

    tools/ai_model_eval/captured/<scenario-key>.json

That directory is gitignored on purpose — the payloads embed personal shot
history (bean names, tasting notes, timestamps) and this is a public repo.

Scenario shot ids in scenarios.json are from one maintainer's database and will
NOT resolve elsewhere. Treat the file as a description of scenario SHAPES; pick
local shots matching each shape and record the ids you used in the run notes.
"""


KEY_SOURCES = {"openai": ("OPENAI_API_KEY", "ai.openaiKey"),
               "anthropic": ("ANTHROPIC_API_KEY", "ai.anthropicKey"),
               "gemini": ("GEMINI_API_KEY", "ai.geminiKey"),
               "openrouter": ("OPENROUTER_API_KEY", "ai.openrouterKey")}


def api_key(provider: str) -> str:
    env, setting = KEY_SOURCES[provider]
    key = os.environ.get(env, "").strip()
    if key:
        return key
    try:                                  # fall back to Decenza's own setting
        out = subprocess.run(
            ["defaults", "read", "com.decentespresso.Decenza", setting],
            capture_output=True, text=True, check=True)
        key = out.stdout.strip()
    except (subprocess.CalledProcessError, FileNotFoundError):
        key = ""
    if not key:
        sys.exit(f"No {provider} key: set ${env} (or configure one in Decenza on macOS).")
    return key


def load_scenarios(path: str, mode: str, captured: str) -> list:
    with open(path) as f:
        scenarios = json.load(f)["scenarios"]
    # The emission test REQUIRES taste feedback on the shot. Without it the
    # taste gate fires, the model correctly asks a clarifying question, and
    # correctly omits the block — so the run would measure nothing.
    if mode == "emission":
        scenarios = [s for s in scenarios if s.get("hasTasteFeedback")]
        if not scenarios:
            sys.exit("emission mode needs scenarios with hasTasteFeedback: true")
    usable, missing = [], []
    for s in scenarios:
        if os.path.exists(os.path.join(captured, s["key"] + ".json")):
            usable.append(s)
        else:
            missing.append(s["key"])
    if missing:
        print(f"note: no captured prompt for {', '.join(missing)} — skipping. "
              f"Run 'replay.py capture-help'.\n", file=sys.stderr)
    if not usable:
        sys.exit("No captured prompts found. Run 'replay.py capture-help'.")
    return usable


def post_json(url: str, headers: dict, body: dict) -> dict:
    req = urllib.request.Request(url, data=json.dumps(body).encode(),
                                 headers={**headers, "Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=300) as r:
        return json.loads(r.read())


def call(key: str, model: str, effort: str, system: str, user: str):
    """Returns (text, error, usage, finish). usage is normalised to OpenAI's
    prompt_tokens / completion_tokens, plus reasoning_tokens."""
    provider = provider_of(model)
    try:
        if provider == "anthropic":
            # AnthropicProvider::analyze(): cached system prompt, thinking off.
            payload = post_json("https://api.anthropic.com/v1/messages",
                                {"x-api-key": key, "anthropic-version": "2023-06-01"},
                                {"model": model, "max_tokens": MAX_OUTPUT_TOKENS,
                                 **anthropic_thinking(model),
                                 "system": [{"type": "text", "text": system,
                                             "cache_control": {"type": "ephemeral", "ttl": "5m"}}],
                                 "messages": [{"role": "user", "content": user}]})
            text = "".join(b.get("text", "") for b in payload.get("content", []) if b.get("type") == "text")
            thought_blocks = sum(1 for b in payload.get("content", []) if b.get("type") == "thinking")
            u = payload.get("usage", {})
            usage = {"prompt_tokens": u.get("input_tokens", 0) + u.get("cache_creation_input_tokens", 0)
                     + u.get("cache_read_input_tokens", 0),
                     "completion_tokens": u.get("output_tokens", 0), "reasoning_tokens": thought_blocks,
                     "cached_tokens": u.get("cache_read_input_tokens", 0),
                     "cache_write_tokens": u.get("cache_creation_input_tokens", 0)}
            return (text or None), (None if text else "no text block"), usage, payload.get("stop_reason")
        if provider == "gemini":
            # GeminiProvider::analyze()/sendRequest().
            payload = post_json(
                f"https://generativelanguage.googleapis.com/v1beta/models/{model}:generateContent",
                {"x-goog-api-key": key},
                {"system_instruction": {"parts": [{"text": system}]},
                 "contents": [{"role": "user", "parts": [{"text": user}]}],
                 "generationConfig": {"thinkingConfig": gemini_thinking(model),
                                      "maxOutputTokens": MAX_OUTPUT_TOKENS}})
            cand = (payload.get("candidates") or [{}])[0]
            text = "".join(p.get("text", "") for p in cand.get("content", {}).get("parts", [])
                           if not p.get("thought"))
            u = payload.get("usageMetadata", {})
            thoughts = u.get("thoughtsTokenCount", 0)
            usage = {"prompt_tokens": u.get("promptTokenCount", 0),
                     "completion_tokens": u.get("candidatesTokenCount", 0) + thoughts,
                     "reasoning_tokens": thoughts,
                     "cached_tokens": u.get("cachedContentTokenCount", 0)}
            finish = cand.get("finishReason")
            return (text or None), (None if text else f"no text ({finish})"), usage, \
                ("length" if finish == "MAX_TOKENS" else finish)
        if provider == "openrouter":
            # OpenRouterProvider::analyze(): OpenAI-compatible, max_tokens.
            body = {"model": model,
                    "messages": [{"role": "system", "content": system},
                                 {"role": "user", "content": user}],
                    "max_tokens": MAX_OUTPUT_TOKENS}
            if model == "openai/gpt-6-luna":
                body["reasoning"] = {"effort": "none"}
            elif model == "google/gemma-4-31b-it":
                body["reasoning"] = {"enabled": True}
            elif model in OPENROUTER_CATALOG_LOW:
                body["reasoning"] = {"effort": "low"}
            payload = post_json("https://openrouter.ai/api/v1/chat/completions",
                                {"Authorization": "Bearer " + key}, body)
            choice = payload["choices"][0]
            text = choice["message"].get("content")
            u = payload.get("usage", {})
            usage = {"prompt_tokens": u.get("prompt_tokens", 0),
                     "completion_tokens": u.get("completion_tokens", 0),
                     "reasoning_tokens": (u.get("completion_tokens_details") or {}).get("reasoning_tokens", 0),
                     "cached_tokens": (u.get("prompt_tokens_details") or {}).get("cached_tokens", 0),
                     "cost": u.get("cost")}
            return (text or None), (None if text else "no text"), usage, choice.get("finish_reason")
    except urllib.error.HTTPError as e:
        return None, f"HTTP {e.code}: {e.read().decode(errors='replace')[:300]}", {}, None
    except Exception as e:                       # noqa: BLE001 — see call_openai()
        return None, f"{type(e).__name__}: {str(e)[:200]}", {}, None
    return call_openai(key, model, effort, system, user)


def call_openai(key: str, model: str, effort: str, system: str, user: str):
    body = {"model": model,
            "messages": [{"role": "system", "content": system},
                         {"role": "user", "content": user}],
            "max_completion_tokens": MAX_OUTPUT_TOKENS,
            "reasoning_effort": effort}
    req = urllib.request.Request(
        "https://api.openai.com/v1/chat/completions",
        data=json.dumps(body).encode(),
        headers={"Authorization": "Bearer " + key, "Content-Type": "application/json"})
    # Catch broadly. A run is paid for and partly irreplaceable (blind labels
    # are only meaningful alongside the key), so one socket timeout or one
    # refusal with a null content must degrade to a recorded per-call error,
    # never abort the run.
    try:
        with urllib.request.urlopen(req, timeout=300) as r:
            payload = json.loads(r.read())
        choice = payload["choices"][0]
        content = choice["message"]["content"]
        if content is None:
            return None, "null content (refusal?)", payload.get("usage", {}), None
        usage = payload.get("usage", {})
        usage["reasoning_tokens"] = (usage.get("completion_tokens_details") or {}).get("reasoning_tokens", 0)
        usage["cached_tokens"] = (usage.get("prompt_tokens_details") or {}).get("cached_tokens", 0)
        return content, None, usage, choice.get("finish_reason")
    except urllib.error.HTTPError as e:
        return None, f"HTTP {e.code}: {e.read().decode(errors='replace')[:300]}", {}, None
    except Exception as e:                       # noqa: BLE001 — see above
        return None, f"{type(e).__name__}: {str(e)[:200]}", {}, None


def audit_block(text: str) -> dict:
    """Would the app accept this response's nextShot block, and is it usable?"""
    obj, reason = extract_structured_next(text)
    if obj is None:
        return {"block": False, "reason": reason}

    grind = obj.get("grinderSetting", "")
    # Models write prose here ("a touch coarser than 9"). Mirrors
    # GrinderAliases::looksLikeSetting(): a setting must BEGIN WITH A DIAL
    # NUMBER, and whitespace decides nothing either way — compound notation
    # writes "1 + 4" (every Eureka/1Zpresso entry) and is a real setting, while
    # a bare "coarser" has no whitespace and is not.
    #
    # This mirrored the app on the whitespace rule alone, which the app had
    # already abandoned for exactly this reason: it scored "coarser" as a valid
    # setting, so the harness reported prose-grind=0 on responses the app was
    # correctly refusing to score. Because the per-model prose counts are what
    # the catalog-default decision rests on, a harness that under-counts here
    # argues for the wrong model. Keep all three shapes in step with
    # looksLikeSetting(); tst_dialing_blocks.cpp is the C++ side of the pair.
    prose = False
    if isinstance(grind, str) and grind.strip():
        s = grind.strip()
        prose = not (
            re.fullmatch(r"-?\d+\s*\+\s*\d+(?:\.\d+)?", s)          # compoundRe
            or re.fullmatch(r"-?\d+(?:\.\d+)?(?:\s+\S.*)?", s)      # numRe
            or re.fullmatch(r"-?\d+(?:\.\d+)?[A-Za-z]{1,3}", s))    # letteredRe
    return {
        "block": True,
        "missing": [f for f in REQUIRED_FIELDS if f not in obj],
        "grinderSetting": grind if grind != "" else "(omitted)",
        "grind_is_prose": prose,
        # Schema says string. An unquoted number reads as empty via
        # QJsonValue::toString() in the app and used to score as full adherence.
        "grind_wrong_type": "grinderSetting" in obj and not isinstance(grind, str),
    }


def spend(model: str, usage: dict) -> float:
    if usage.get("cost") is not None:      # OpenRouter bills and reports it per call
        return float(usage["cost"])
    if model not in PRICES:
        # Silence here would report $0.0000 for exactly the case this harness
        # exists to serve: a model too new to be in the table.
        print(f"  warning: no price for {model} — its spend is NOT counted",
              file=sys.stderr)
        return 0.0
    pin, pout = PRICES[model]
    return (usage.get("prompt_tokens", 0) * pin
            + usage.get("completion_tokens", 0) * pout) / 1_000_000


def write_key(outdir: str, keymap: dict) -> None:
    with open(os.path.join(outdir, "key.json"), "w") as f:
        json.dump(keymap, f, indent=2)


def run(args) -> None:
    scenarios = load_scenarios(args.scenarios, args.mode, args.captured)
    # model[@effort]: an @ pins that model's effort (gpt-6.1-sol takes no "none").
    specs = [m.strip() for m in args.models.split(",") if m.strip()]
    efforts = [e.strip() for e in args.efforts.split(",") if e.strip()]
    models = [m.split("@")[0] for m in specs]
    keys = {p: api_key(p) for p in {provider_of(m) for m in models}}
    outdir = os.path.join(RESULTS, args.mode + (f"-{args.label}" if args.label else ""))
    os.makedirs(outdir, exist_ok=True)

    rng = random.Random(args.seed)
    keymap, results, total = {}, {}, 0.0

    for scen in scenarios:
        with open(os.path.join(args.captured, scen["key"] + ".json")) as f:
            payload = json.load(f)
        system, user = payload["systemPromptUsed"], payload["userPromptUsed"]
        print(f"\n=== {scen['key']} — {scen['description']} ===")

        combos = []
        for spec in specs:
            model, _, pinned = spec.partition("@")
            if pinned:
                combos.append((model, pinned))
            elif provider_of(model) == "openai":
                combos += [(model, openai_app_effort(model) if e == "app" else e) for e in efforts]
            else:
                combos.append((model, "app"))
        if args.mode == "blind":
            # Shuffle labels per scenario so the judge cannot carry a mapping
            # across scenarios.
            labels = [chr(ord("A") + i) for i in range(len(combos))]
            rng.shuffle(labels)
            keymap[scen["key"]] = {}

        for i, (model, effort) in enumerate(combos):
            text, err, usage, finish = call(keys[provider_of(model)], model, effort, system, user)
            tag = f"{model}/{effort}"
            if err:
                # Record it. Skipping made an errored call print as "n" in the
                # summary — indistinguishable from the model omitting the block,
                # i.e. a 429 read as a capability failure in the table the
                # catalog decision rests on.
                print(f"  {tag:24s} ERROR {err}")
                results[(scen["key"], model, effort)] = {"block": None, "error": err}
                if args.mode == "blind":
                    keymap[scen["key"]][labels[i]] = tag
                    write_key(outdir, keymap)
                continue
            total += spend(model, usage)
            reasoning = usage.get("reasoning_tokens", 0)
            a = audit_block(text)
            results[(scen["key"], model, effort)] = a

            if args.mode == "blind":
                label = labels[i]
                keymap[scen["key"]][label] = tag
                # Persist after EVERY call. Written only at the end, an abort
                # left labelled responses with no mapping — a paid run that
                # cannot be judged.
                write_key(outdir, keymap)
                # Deliberately NOT printing cost or token counts here: on the
                # 2026-07-30 run those fingerprinted the model (the cheapest
                # response is unmistakable) and broke the blind.
                print(f"  {label}: written")
                path = os.path.join(outdir, f"{scen['key']}__{label}.md")
                header = f"# {scen['key']} — {scen['description']}\n\n"
            else:
                if not a["block"]:
                    verdict = f"NO BLOCK ({a['reason']})"
                else:
                    parts = [f"grind={a['grinderSetting']}"]
                    if a["grind_wrong_type"]:
                        parts.append("NOT-A-JSON-STRING")
                    if a["grind_is_prose"]:
                        parts.append("PROSE-NOT-SETTING")
                    if a["missing"]:
                        parts.append("MISSING " + ",".join(a["missing"]))
                    verdict = "block " + " ".join(parts)
                flag = "  <-- TRUNCATED" if finish == "length" else ""
                print(f"  {tag:30s} {verdict} [reasoning={reasoning} in={usage.get('prompt_tokens', 0)} "
                      f"cached={usage.get('cached_tokens', 0)} out={usage.get('completion_tokens', 0)}]{flag}")
                path = os.path.join(outdir, f"{scen['key']}__{model.replace('/', '_')}__{effort}.md")
                header = (f"# {scen['key']} — {scen['description']}\n"
                          f"# model: {model}  effort: {effort}\n"
                          f"# reasoning_tokens: {reasoning}  finish: {finish}\n\n")
            with open(path, "w") as f:
                f.write(header + text + "\n")

    if args.mode == "blind":
        write_key(outdir, keymap)
        print(f"\nResponses: {outdir}/<scenario>__<label>.md")
        print("Key:       key.json — do NOT open until judgments are written down.")
    else:
        # Y = app would accept the block, n = it would not, E = the call failed.
        # E must stay distinct from n: an error is not evidence about the model.
        print("\n=== block accepted by the app's parser? (scenarios in order) ===")
        print("    Y = accepted   n = no usable block   E = call failed\n")
        tried = sorted({(m, e) for (_, m, e) in results}, key=lambda me: (models.index(me[0]), me[1]))
        for model, effort in tried:
                cells = []
                for s in scenarios:
                    r = results.get((s["key"], model, effort))
                    if r is None or r.get("block") is None:
                        cells.append("E")
                    else:
                        cells.append("Y" if r["block"] else "n")
                flags = []
                prose = sum(1 for s in scenarios
                            if results.get((s["key"], model, effort), {}).get("grind_is_prose"))
                wrong = sum(1 for s in scenarios
                            if results.get((s["key"], model, effort), {}).get("grind_wrong_type"))
                if prose:
                    flags.append(f"prose-grind={prose}")
                if wrong:
                    flags.append(f"grind-not-a-string={wrong}")
                extra = ("   " + "  ".join(flags)) if flags else ""
                print(f"  {model:15s} {effort:5s} {' '.join(cells)}{extra}")

    print(f"\nTotal spend: ${total:.4f}")


def reveal(args) -> None:
    with open(os.path.join(RESULTS, args.run, "key.json")) as f:
        print(json.dumps(json.load(f), indent=2))


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)

    for mode in ("blind", "emission"):
        p = sub.add_parser(mode)
        p.set_defaults(func=run, mode=mode)
        p.add_argument("--models", required=True, help="comma-separated model ids")
        p.add_argument("--efforts", default=DEFAULT_EFFORT,
                       help="comma-separated OpenAI reasoning_effort values; 'app' (default) is what the app sends")
        p.add_argument("--scenarios", default=os.path.join(HERE, "scenarios.json"))
        p.add_argument("--seed", type=int, default=20260730,
                       help="label-shuffle seed; reproducible, unknown to the judge")
        p.add_argument("--captured", default=CAPTURED,
                       help="directory of captured prompts, for comparing two prompt versions")
        p.add_argument("--label", default="",
                       help="suffix for the runs/ subdirectory, so a comparison run keeps the baseline")

    p = sub.add_parser("reveal")
    p.set_defaults(func=reveal)
    p.add_argument("--run", default="blind")

    p = sub.add_parser("capture-help")
    p.set_defaults(func=lambda a: print(CAPTURE_HELP))

    args = ap.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
