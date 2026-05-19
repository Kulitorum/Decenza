#!/usr/bin/env python3
"""Offline analysis harness for the cross-profile grinder calibration.

Read-only. Faithfully replays the legacy `buildGrinderCalibrationBlock`
(pooled all-time medians, two-anchor pick) to reproduce the #1223 failure,
and reproduces the proposed path's SLOPE-ESTIMATION and GATE LOGIC only
(within-batch pairing, conversionKey, dimensionless spread gate). It does
NOT reproduce the full block assembly — the per-batch anchor, per-profile
rgs table, extrapolation cap, or directional fallback are not modelled
here; this tool exists to tune the named constants in the C++ rewrite
(openspec change `fix-grinder-calibration-cross-profile`, tasks 1.2/1.3)
against a real shots.db, not to mirror the production output.

Defaults below MUST stay in sync with the shipped C++ constants in
src/ai/dialing_blocks.cpp (kCalibMinPairSpanUgs, kCalibMinEndpointN,
kCalibMinValidatedPairs, kCalibMaxSpreadRatio, kCalibCap).

Usage:
    python3 tools/calib_analysis.py --db /path/to/shots.db [--grinder Zero]
            [--min-pair-span 0.75] [--min-endpoint-samples 2]
            [--min-validated-pairs 3] [--max-spread-ratio 0.6]
            [--cap 1.5]

The KB resolution (alias map, recipe-prefix, UGS lookup) mirrors
src/ai/shotsummarizer_kb.cpp so grouping matches the C++ exactly.
"""
import argparse, itertools, json, sqlite3, statistics, sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
KB_PATH = REPO / "resources/ai/profile_knowledge.json"


def norm(s):
    s = (s or "").lower().strip()
    for a, b in (("é", "e"), ("è", "e"), ("ê", "e"), ("ë", "e")):
        s = s.replace(a, b)
    return s.replace(" & ", " and ")


def load_kb(kb_path):
    kb = json.loads(Path(kb_path).read_text())["profiles"]
    alias_to_id, recipe_aliases, info = {}, [], {}
    for p in kb:
        pid = p["id"]
        u = p.get("ugs", {})
        info[pid] = dict(ugs=u.get("value"), inferred=u.get("inferred", False),
                         name=p.get("displayName", pid))
        is_editor = p.get("defaultForEditorType", "") in ("dflow", "aflow")

        def reg(raw, recipe_anchor):
            k = norm(raw)
            first = k not in alias_to_id
            alias_to_id.setdefault(k, pid)
            if recipe_anchor and first:
                recipe_aliases.append((k, pid))

        reg(p.get("displayName", ""), not is_editor)
        for a in p.get("alsoMatches", []):
            if a.strip():
                reg(a, not is_editor)
    recipe_aliases.sort(key=lambda kv: len(kv[0]), reverse=True)
    return alias_to_id, recipe_aliases, info


def make_resolver(alias_to_id, recipe_aliases, info):
    def recipe_prefix(nk):
        for k, pid in recipe_aliases:
            n = len(k)
            if len(nk) <= n or not nk.startswith(k):
                continue
            if nk[n] in "/- " or nk[n].isdigit():
                return pid
        return ""

    def resolve_kb_input(kbid):
        if not kbid:
            return ""
        if kbid in info:
            return kbid
        nk = norm(kbid)
        return alias_to_id.get(nk) or recipe_prefix(nk)

    def resolve_title(title):
        if not title:
            return ""
        nk = norm(title)
        return alias_to_id.get(nk) or recipe_prefix(nk)

    return resolve_kb_input, resolve_title


def fnum(s):
    try:
        return float(str(s).strip())
    except (TypeError, ValueError):
        return None


def median(xs):
    return statistics.median(xs) if xs else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--db", required=True)
    ap.add_argument("--grinder", default="Zero")
    # Defaults mirror the shipped C++ constants (dialing_blocks.cpp).
    ap.add_argument("--min-pair-span", type=float, default=0.75)
    # >=2: a single-shot "median" is one noisy point; the safe DIRECTIONAL
    # outcome at n=1 on any one DB is luck, not robustness (review S3).
    ap.add_argument("--min-endpoint-samples", type=int, default=2)
    ap.add_argument("--min-validated-pairs", type=int, default=3)
    # Reproduces the D1b counterfactual: bean-only identity (no roast
    # batch) — the falsely-confident path the design rejects.
    ap.add_argument("--bean-only", action="store_true")
    # Dimensionless: IQR(slopes) <= k * |median slope|. Grinder-portable,
    # unlike an absolute steps/UGS threshold (slope magnitude varies per
    # grinder+burrs — Al-Shemmeri: 13.4 vs 58.4 microns/step across grinders).
    ap.add_argument("--max-spread-ratio", type=float, default=0.6)
    ap.add_argument("--cap", type=float, default=1.5)
    args = ap.parse_args()

    alias_to_id, recipe_aliases, info = load_kb(KB_PATH)
    rki, rt = make_resolver(alias_to_id, recipe_aliases, info)

    def ugs(pid):
        i = info.get(pid)
        return i["ugs"] if i and i["ugs"] is not None else None

    con = sqlite3.connect(f"file:{args.db}?mode=ro", uri=True)
    con.row_factory = sqlite3.Row
    rows = con.execute(
        """SELECT timestamp,profile_name,profile_kb_id,grinder_setting,
        final_weight,enjoyment,drink_tds,beverage_type,bean_brand,bean_type,
        roast_date,
        channeling_detected,grind_issue_detected,skip_first_frame_detected,
        pour_truncated_detected, json_extract(profile_json,'$.target_weight') tw
        FROM shots WHERE grinder_model=? ORDER BY timestamp DESC""",
        (args.grinder,)).fetchall()

    NINETY_D = 90 * 24 * 3600

    def is_dated(r):
        rd = (r["roast_date"] or "").strip()
        return rd and rd != "--"

    # Sliding-window (single-linkage, 90d gap) clustering for UNDATED
    # shots, per bean — matches the spec's "within 90 days of one
    # another" and fixes the prototype's fixed-bucket boundary bug
    # (review S5b: two shots 3 days apart could straddle a fixed bucket).
    undated_cluster = {}
    by_bean = {}
    for r in rows:
        if is_dated(r) or not (r["bean_brand"] and r["bean_type"]):
            continue
        by_bean.setdefault(f"{r['bean_brand']} / {r['bean_type']}", []).append(r)
    for bean, rs in by_bean.items():
        rs.sort(key=lambda x: x["timestamp"] or 0)
        cid, prev = 0, None
        for x in rs:
            ts = x["timestamp"] or 0
            if prev is not None and ts - prev > NINETY_D:
                cid += 1
            undated_cluster[id(x)] = f"{bean} ~undated#{cid}"
            prev = ts

    def coffee_batch(r):
        bean = f"{r['bean_brand']} / {r['bean_type']}"
        if args.bean_only:
            return bean  # D1b counterfactual: ignores roast batch
        if is_dated(r):
            return f"{bean} @ {(r['roast_date'] or '').strip()}"
        return undated_cluster.get(id(r), f"{bean} ~undated?")

    def resolve(r):
        return (rki((r["profile_kb_id"] or "").strip())
                or rt((r["profile_name"] or "").strip()))

    def base_clean(r):
        bt = (r["beverage_type"] or "").lower()
        if bt not in ("", "espresso"):
            return False
        if (r["final_weight"] or 0) < 15:
            return False
        return not (r["channeling_detected"] or r["grind_issue_detected"]
                    or r["skip_first_frame_detected"]
                    or r["pour_truncated_detected"])

    def dialed_in(r):
        if not base_clean(r):
            return False
        if (r["enjoyment"] or 0) >= 50:
            return True
        if (r["drink_tds"] or 0) > 0:
            return True
        tw, fw = fnum(r["tw"]), r["final_weight"]
        return bool(tw and fw and abs(fw - tw) <= 0.10 * tw)

    # ---- legacy pooled reproduction ----
    pooled = {}
    for r in rows:
        if not base_clean(r):
            continue
        v = fnum(r["grinder_setting"])
        if v is None:
            continue
        pid = resolve(r)
        if pid:
            pooled.setdefault(pid, []).append(v)
    canon, infer = [], []
    for p, v in pooled.items():
        if ugs(p) is None:
            continue
        row = (info[p]["name"], info[p]["ugs"], median(v))
        (infer if info[p]["inferred"] else canon).append(row)
    cands = sorted(canon + infer, key=lambda c: c[1])
    print("=== LEGACY pooled all-time (reproduces #1223) ===")
    for nm, u, m in cands:
        print(f"  {nm:<26} ugs={u:<5} median={m}")

    def select_anchors(pool):  # port of the PRE-#1236 selectAnchors (removed from dialing_blocks.cpp); kept only to reproduce the legacy #1223 failure
        if len(pool) < 2:
            return None
        pool = sorted(pool, key=lambda c: c[1])
        pos = neg = 0
        for i in range(len(pool)):
            for j in range(i + 1, len(pool)):
                d = pool[j][2] - pool[i][2]
                if d > 1e-9:
                    pos += 1
                elif d < -1e-9:
                    neg += 1
        ms = 1 if pos > neg else -1 if neg > pos else 0
        best = None
        for f in range(len(pool)):
            for c in range(len(pool) - 1, f, -1):
                ud = pool[c][1] - pool[f][1]
                sd = pool[c][2] - pool[f][2]
                ok = (ms == 0) or (sd * ms > 0)
                if ok and abs(sd) >= 0.5 and (best is None or ud > best[0]):
                    best = (ud, pool[f], pool[c])
        return best

    # canonical-only first, fall back to canonical+inferred (mirrors C++)
    sel = select_anchors(canon) or select_anchors(canon + infer)
    if sel:
        _, fa, ca = sel
        ck = round((ca[2] - fa[2]) / (ca[1] - fa[1]), 2)
        print(f"  fine={fa[0]} coarse={ca[0]} conversionKey={ck}"
              f"  TurboTurbo(ugs6) rgs={round(fa[2]+(6-fa[1])*ck,1)}"
              f"  {'<-- WRONG SIGN' if ck < 0 else ''}")

    # ---- proposed within-coffee paired ----
    coffees = {}
    for r in rows:
        if not dialed_in(r):
            continue
        v = fnum(r["grinder_setting"])
        if v is None:
            continue
        pid = resolve(r)
        if ugs(pid) is None:
            continue
        if not (r["bean_brand"] and r["bean_type"]):
            continue
        c = coffee_batch(r)
        coffees.setdefault(c, {}).setdefault(pid, []).append(v)

    pairs = []
    for c, pm in coffees.items():
        pts = sorted([(info[p]["name"], info[p]["ugs"], median(v), len(v))
                      for p, v in pm.items()], key=lambda x: x[1])
        for (n1, u1, m1, c1), (n2, u2, m2, c2) in itertools.combinations(pts, 2):
            if abs(u2 - u1) < args.min_pair_span:
                continue
            if c1 < args.min_endpoint_samples or c2 < args.min_endpoint_samples:
                continue
            pairs.append((u2 - u1, (m2 - m1) / (u2 - u1), c))
    print(f"\n=== PROPOSED within-coffee (span>={args.min_pair_span}, "
          f"n>={args.min_endpoint_samples}) ===")
    if pairs:
        sl = sorted(s for _, s, _ in pairs)
        ck = round(statistics.median(sl), 2)
        q1, q3 = sl[len(sl) // 4], sl[3 * len(sl) // 4]
        spread = round(q3 - q1, 2)
        # Dimensionless spread, grinder-portable (see --max-spread-ratio).
        ratio = round(spread / abs(ck), 2) if abs(ck) > 1e-9 else float("inf")
        gated = (len(sl) >= args.min_validated_pairs
                 and ratio <= args.max_spread_ratio)
        print(f"  nPairs={len(sl)} conversionKey={ck} "
              f"range={round(min(sl),2)}..{round(max(sl),2)} "
              f"IQRspread={spread} spreadRatio={ratio}")
        print(f"  gate(minPairs={args.min_validated_pairs}, "
              f"maxSpreadRatio={args.max_spread_ratio}) -> "
              f"{'PUBLISH numeric (confidence=approximate)' if gated else 'DIRECTIONAL only'}")
        print(f"  extrapolation cap = {args.cap} UGS beyond validated range")
    else:
        print("  no surviving pairs -> DIRECTIONAL only (confidence=directional)")
    con.close()


if __name__ == "__main__":
    sys.exit(main())
