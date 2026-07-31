#!/usr/bin/env python3
"""Replay the app's real advisor prompts against candidate models.

Two modes:

  blind     — prose quality. Responses are written under shuffled labels so the
              judging pass cannot see which model produced which answer.
  emission  — does the model emit the trailing `nextShot` JSON block when the
              system prompt requires it, and is the block well-formed?

Both replay prompts captured from the running app (see README: capture step),
using the exact request shape OpenAIProvider::analyze() builds. Reconstructing
the prompt in this script instead would guarantee drift from the shipped one.

Usage:
    python3 replay.py capture-help
    python3 replay.py emission --models gpt-5.6-terra,gpt-5.6-luna
    python3 replay.py blind    --models gpt-5.6-terra,gpt-5.6-luna --efforts none
    python3 replay.py reveal   --run blind

API key: $OPENAI_API_KEY, else Decenza's own setting on macOS.
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
PRICES = {
    "gpt-5.6-sol":   (5.00, 30.00),
    "gpt-5.6-terra": (2.00, 12.00),
    "gpt-5.6-luna":  (0.20, 1.20),
    "gpt-5.4":       (2.50, 15.00),
    "gpt-5.4-mini":  (0.75, 4.50),
    "gpt-5.4-nano":  (0.20, 1.25),
}

# Mirror of OpenAIProvider::analyze() — keep in step with src/ai/aiprovider.cpp.
MAX_OUTPUT_TOKENS = 4096          # src/ai/aiprovider.h MAX_OUTPUT_TOKENS
DEFAULT_EFFORT = "none"           # src/ai/aiprovider.cpp analyze()

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


def api_key() -> str:
    key = os.environ.get("OPENAI_API_KEY", "").strip()
    if key:
        return key
    try:                                  # fall back to Decenza's own setting
        out = subprocess.run(
            ["defaults", "read", "com.decentespresso.Decenza", "ai.openaiKey"],
            capture_output=True, text=True, check=True)
        key = out.stdout.strip()
    except (subprocess.CalledProcessError, FileNotFoundError):
        key = ""
    if not key:
        sys.exit("No API key: set $OPENAI_API_KEY (or configure one in Decenza on macOS).")
    return key


def load_scenarios(path: str, mode: str) -> list:
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
        if os.path.exists(os.path.join(CAPTURED, s["key"] + ".json")):
            usable.append(s)
        else:
            missing.append(s["key"])
    if missing:
        print(f"note: no captured prompt for {', '.join(missing)} — skipping. "
              f"Run 'replay.py capture-help'.\n", file=sys.stderr)
    if not usable:
        sys.exit("No captured prompts found. Run 'replay.py capture-help'.")
    return usable


def call(key: str, model: str, effort: str, system: str, user: str):
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
        return content, None, payload.get("usage", {}), choice.get("finish_reason")
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
    key = api_key()
    scenarios = load_scenarios(args.scenarios, args.mode)
    models = [m.strip() for m in args.models.split(",") if m.strip()]
    efforts = [e.strip() for e in args.efforts.split(",") if e.strip()]
    outdir = os.path.join(RESULTS, args.mode)
    os.makedirs(outdir, exist_ok=True)

    rng = random.Random(args.seed)
    keymap, results, total = {}, {}, 0.0

    for scen in scenarios:
        with open(os.path.join(CAPTURED, scen["key"] + ".json")) as f:
            payload = json.load(f)
        system, user = payload["systemPromptUsed"], payload["userPromptUsed"]
        print(f"\n=== {scen['key']} — {scen['description']} ===")

        combos = [(m, e) for m in models for e in efforts]
        if args.mode == "blind":
            # Shuffle labels per scenario so the judge cannot carry a mapping
            # across scenarios.
            labels = [chr(ord("A") + i) for i in range(len(combos))]
            rng.shuffle(labels)
            keymap[scen["key"]] = {}

        for i, (model, effort) in enumerate(combos):
            text, err, usage, finish = call(key, model, effort, system, user)
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
            reasoning = (usage.get("completion_tokens_details") or {}).get("reasoning_tokens", 0)
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
                print(f"  {tag:24s} {verdict} [reasoning={reasoning}]{flag}")
                path = os.path.join(outdir, f"{scen['key']}__{model}__{effort}.md")
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
        for model in models:
            for effort in efforts:
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
                       help=f"comma-separated reasoning_effort values (default {DEFAULT_EFFORT})")
        p.add_argument("--scenarios", default=os.path.join(HERE, "scenarios.json"))
        p.add_argument("--seed", type=int, default=20260730,
                       help="label-shuffle seed; reproducible, unknown to the judge")

    p = sub.add_parser("reveal")
    p.set_defaults(func=reveal)
    p.add_argument("--run", default="blind")

    p = sub.add_parser("capture-help")
    p.set_defaults(func=lambda a: print(CAPTURE_HELP))

    args = ap.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
