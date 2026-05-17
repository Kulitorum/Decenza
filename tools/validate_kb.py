#!/usr/bin/env python3
"""Build-time validator for the structured profile knowledge base.

Change: restructure-kb-as-validated-json (design D5, Option B). Pure stdlib —
zero new dependency, portable across the 6 CI platforms. This is a pass/fail
check, NOT a code generator: no artifact enters the build graph (the
cross-platform-codegen failure class, commit 2b6e0965, is deliberately avoided).

Exit 0 = valid; exit 1 = a hard violation (build MUST fail). D9 prose/band
lint findings are best-effort WARNINGS (printed, non-fatal) per the design:
they are false-positive-prone (the Adaptive v2 band 6-9 vs prose dial-in "8-9"
is intentionally different), so they inform an author, they do not gate.

Usage: validate_kb.py [path/to/profile_knowledge.json]
"""
import json
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
DEFAULT_KB = REPO / "resources" / "ai" / "profile_knowledge.json"

FAMILY_ENUM = {
    "allonge", "blooming", "filter", "flat-pressure", "flow-adaptive",
    "gentle-long-preinfusion", "lever-decline", "maintenance", "manual",
    "pressure-ramp-flow", "tea", "turbo", "volume-based",
}
AXIS_ENUM = {"pressurePeak", "extractionFlow"}
PROVENANCE_ENUM = {"cited", "author-stated", "inferred"}
CONFIDENCE_ENUM = {"high", "medium", "low"}
INTRINSIC_SRC = {"profile-notes"}

ENTRY_KEYS = {
    "id", "displayName", "alsoMatches", "defaultForEditorType", "ugs",
    "analysisFlags", "skipCatalog", "family", "expertBand", "prose",
}
BAND_KEYS = {"axis", "lo", "hi", "src", "srcArchive", "provenance",
             "confidence", "rationale"}
ID_RE = re.compile(r"^[a-z0-9][a-z0-9-]*$")
URL_RE = re.compile(r"^https?://")


def validate(kb_path: Path):
    errors, warnings = [], []
    try:
        doc = json.loads(kb_path.read_text(encoding="utf-8"))
    except Exception as e:  # malformed JSON is the loudest failure of all
        return [f"JSON parse failed: {e}"], []

    if doc.get("schemaVersion") != 1:
        errors.append(f"schemaVersion must be 1, got {doc.get('schemaVersion')!r}")
    profiles = doc.get("profiles")
    if not isinstance(profiles, list) or not profiles:
        return errors + ["'profiles' must be a non-empty array"], []

    ids = {}
    alias_to_id = {}          # normalized lookup key -> id (collision = fatal)
    editor_default_count = {}

    def norm(s):
        # MUST match shotsummarizer_kb.cpp normalizeProfileKey EXACTLY, or the
        # alias-collision check is unsound: two aliases the C++ resolver folds
        # to one key (silently overwriting in s_aliasToId) but this validator
        # keeps distinct would pass here yet mis-resolve at runtime.
        s = s.strip().lower()
        for ch in "éèêë":
            s = s.replace(ch, "e")
        return s.replace(" & ", " and ")

    for idx, p in enumerate(profiles):
        where = f"profiles[{idx}]"
        if not isinstance(p, dict):
            errors.append(f"{where}: entry is not an object")
            continue
        pid = p.get("id", f"<{where}>")

        unknown = set(p) - ENTRY_KEYS
        if unknown:
            errors.append(f"{pid}: unknown key(s) {sorted(unknown)}")

        if not isinstance(p.get("id"), str) or not ID_RE.match(p.get("id", "")):
            errors.append(f"{pid}: id must be kebab-case ^[a-z0-9][a-z0-9-]*$")
        elif p["id"] in ids:
            errors.append(f"id {p['id']!r} duplicated (also {ids[p['id']]})")
        else:
            ids[p["id"]] = where

        if not p.get("displayName"):
            errors.append(f"{pid}: missing/empty displayName")
        if not p.get("prose"):
            errors.append(f"{pid}: missing/empty prose")

        skip = p.get("skipCatalog", False)
        if not isinstance(skip, bool):
            errors.append(f"{pid}: skipCatalog must be boolean")
        if not skip:
            if p.get("family") not in FAMILY_ENUM:
                errors.append(f"{pid}: family {p.get('family')!r} not in enum")

        am = p.get("alsoMatches", [])
        if not isinstance(am, list) or any(not isinstance(x, str) or not x for x in am):
            errors.append(f"{pid}: alsoMatches must be a list of non-empty strings")
        ded = p.get("defaultForEditorType")
        if ded not in (None, "dflow", "aflow"):
            errors.append(f"{pid}: defaultForEditorType {ded!r} invalid")
        if ded:
            editor_default_count[ded] = editor_default_count.get(ded, 0) + 1

        ugs = p.get("ugs")
        if ugs is not None:
            if not isinstance(ugs, dict) or set(ugs) - {"value", "inferred", "note"}:
                errors.append(f"{pid}: ugs shape invalid")
            else:
                if not isinstance(ugs.get("value"), (int, float)):
                    errors.append(f"{pid}: ugs.value must be a number")
                if not isinstance(ugs.get("inferred"), bool):
                    errors.append(f"{pid}: ugs.inferred must be boolean")

        af = p.get("analysisFlags", [])
        if not isinstance(af, list) or any(not isinstance(x, str) or not x for x in af):
            errors.append(f"{pid}: analysisFlags must be a list of non-empty strings")

        # alias integrity over the FULL runtime keyspace (must match
        # shotsummarizer_kb.cpp registerAlias): displayName + alsoMatches +
        # the synthetic "__editor_default__:<type>" key the resolver registers
        # for defaultForEditorType. Omitting the synthetic key here would let
        # a collision the runtime warns about pass the build gate.
        keys = []
        if isinstance(p.get("displayName"), str):
            keys.append(p["displayName"])
        keys += [x for x in am if isinstance(x, str)]
        if ded in ("dflow", "aflow"):
            keys.append(f"__editor_default__:{ded}")
        for k in keys:
            nk = norm(k)
            if nk in alias_to_id and alias_to_id[nk] != p.get("id"):
                errors.append(
                    f"alias collision {k!r}: maps to both "
                    f"{alias_to_id[nk]!r} and {p.get('id')!r}")
            elif nk in alias_to_id:
                warnings.append(f"{pid}: redundant within-entry alias {k!r}")
            else:
                alias_to_id[nk] = p.get("id")

        eb = p.get("expertBand")
        if eb is not None:
            _validate_band(pid, eb, p.get("prose", ""), kb_path,
                           errors, warnings)

    for ed, n in editor_default_count.items():
        if n > 1:
            errors.append(f"defaultForEditorType {ed!r} set on {n} entries (max 1)")

    return errors, warnings


def _validate_band(pid, eb, prose, kb_path, errors, warnings):
    if not isinstance(eb, dict):
        errors.append(f"{pid}: expertBand is not an object")
        return
    unknown = set(eb) - BAND_KEYS
    if unknown:
        errors.append(f"{pid}: expertBand unknown key(s) {sorted(unknown)}")
    for req in ("axis", "provenance", "confidence", "rationale"):
        if not eb.get(req):
            errors.append(f"{pid}: expertBand missing required {req!r}")
    if eb.get("axis") not in AXIS_ENUM:
        errors.append(f"{pid}: expertBand.axis {eb.get('axis')!r} not in enum")
    if eb.get("confidence") not in CONFIDENCE_ENUM:
        errors.append(f"{pid}: expertBand.confidence invalid")
    prov = eb.get("provenance")
    if prov not in PROVENANCE_ENUM:
        errors.append(f"{pid}: expertBand.provenance {prov!r} not in enum")

    lo, hi = eb.get("lo"), eb.get("hi")
    if lo is None and hi is None:
        errors.append(f"{pid}: expertBand needs at least one of lo/hi")
    for nm, v in (("lo", lo), ("hi", hi)):
        if v is not None and (not isinstance(v, (int, float)) or v <= 0):
            errors.append(f"{pid}: expertBand.{nm} must be a positive number")
    if isinstance(lo, (int, float)) and isinstance(hi, (int, float)) and lo >= hi:
        errors.append(f"{pid}: expertBand.lo ({lo}) must be < hi ({hi})")

    src = eb.get("src")
    if prov == "cited":
        if not (isinstance(src, str) and URL_RE.match(src)):
            errors.append(f"{pid}: provenance=cited requires an http(s) src URL")
    elif prov == "author-stated":
        if src not in INTRINSIC_SRC:
            errors.append(f"{pid}: provenance=author-stated requires src in {sorted(INTRINSIC_SRC)}")
    elif prov == "inferred":
        if src is not None:
            errors.append(f"{pid}: provenance=inferred must have no src")

    arch = eb.get("srcArchive")
    if arch is not None:
        if not isinstance(arch, str) or not arch:
            errors.append(f"{pid}: expertBand.srcArchive must be a non-empty path")
        elif not (kb_path.resolve().parent.parent.parent / arch).exists():
            # repo-relative; kb is resources/ai/x.json -> repo root is parents[2]
            if not (REPO / arch).exists():
                errors.append(
                    f"{pid}: srcArchive {arch!r} does not exist in the repo "
                    f"(relocate the source first — task 1.5)")

    # D9 best-effort lint (WARNING, non-fatal): a prose line restating the
    # band bounds verbatim is the third-copy regression D9 guards against.
    for bound in (lo, hi):
        if bound is None:
            continue
        b = f"{bound:g}"
        if re.search(rf"\b{re.escape(b)}\s*(bar|ml/s|ml per second)", prose, re.I):
            warnings.append(
                f"{pid}: D9 lint — prose appears to restate band bound "
                f"{b!r}; verify it is profile commentary, not a copy of the "
                f"struct band (Adaptive-v2-style intentional difference is OK)")


def main():
    kb = Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_KB
    if not kb.exists():
        print(f"validate_kb: KB not found: {kb}", file=sys.stderr)
        return 1
    errors, warnings = validate(kb)
    for w in warnings:
        print(f"WARN  {w}", file=sys.stderr)
    for e in errors:
        print(f"ERROR {e}", file=sys.stderr)
    if errors:
        print(f"\nvalidate_kb: FAILED — {len(errors)} error(s), "
              f"{len(warnings)} warning(s) in {kb}", file=sys.stderr)
        return 1
    print(f"validate_kb: OK — {kb} valid "
          f"({len(warnings)} non-fatal lint warning(s))")
    return 0


if __name__ == "__main__":
    sys.exit(main())
