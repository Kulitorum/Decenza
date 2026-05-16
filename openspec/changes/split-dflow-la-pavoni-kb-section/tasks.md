> Lean correctness fix completing #1160 for the profile it deferred. Mirrors the archived `correct-dflow-variant-ugs` method exactly. Pure KB data + existing-parser resolution; no code/parser change. Build + dialing tests via Qt Creator MCP, not CLI.

## 1. KB section split

- [x] 1.1 Add `## D-Flow La Pavoni variant` to `resources/ai/profile_knowledge.md` (title without `" / "` — D1): `Also matches: "D-Flow / La Pavoni"`; `AnalysisFlags: flow_trend_ok`; `Category: Lever/Flow hybrid (Londinium family)`; `Family: lever-decline`; `UGS: ~1.0 (inferred — 6–9 bar target + 84°C fill pull coarser than D-Flow / default's ~9 bar/88°C, same mechanism as the Q variant; not on the UGS chart)`.
- [x] 1.2 Section prose (mirror the Q-variant style; reference, don't duplicate, the shared DO-NOT-flag list per D3): D-Flow-editor La Pavoni emulation (Damian ran D-Flow/default + a real La Pavoni side by side vs a Slayer V3 reference; 18 g VST; milk-drink tuned); verbatim profile-notes dial-in goal "grind for a pressure peak between 6 and 9 bar"; de1app stock params (fill 1.2 / 84°C / pour 2.4 mL/s / pressure limit 9.0 bar / 46 g); "coarser than D-Flow / default — do not transfer a default grinder setting 1:1"; expected curves (peak ~6–9 then lever decline; slow 0–0.4 ml/s early soak normal).
- [x] 1.3 Edit `## D-Flow`: remove `"D-Flow / La Pavoni"` from `Also matches:`; update the `UGS:` parenthetical and the "Profiles in this section" prose so the section cleanly describes `D-Flow / default` + the standalone-by-alias profiles only. Confirmed no shared DO-NOT-flag behavioral line removed.

## 2. Human-facing twin

- [x] 2.1 Mirrored in `docs/PROFILE_KNOWLEDGE_BASE.md`: parser-footgun example now lists the La Pavoni variant section; inferred-positions table gains a `~1.0 | D-Flow / La Pavoni` row (the per-profile `#### D-Flow / La Pavoni` detail subsection already existed and is consistent).

## 3. Regression coverage

- [x] 3.1 `tests/tst_dialing_blocks.cpp::dflowLaPavoniVariant_distinctPosition`: La Pavoni canonical name distinct from default *and* from the Q variant; `ugsForKbId` strictly coarser than default; `ugsInferredForKbId` true; `flow_trend_ok` preserved.
- [x] 3.2 Structural guard (`shippedKb_editorModelAndRealProfileNames_guard`): `## ` heading count 42→43, `Also matches:` count 27→28, new `## D-Flow La Pavoni variant` header pinned, base `## D-Flow` alias line re-pinned (La Pavoni removed), new La Pavoni alias line pinned.

## 4. Verification

- [x] 4.1 `openspec validate split-dflow-la-pavoni-kb-section` → valid.
- [x] 4.2 Built via Qt Creator MCP (0 errors, 0 warnings); full Qt Test suite green — **2076 passed, 0 failed, 0 skipped, 0 warnings**.
- [ ] 4.3 Live spot-check via `dialing_get_context` on a real `D-Flow / La Pavoni` shot — **not performed this session** (decenza MCP unreachable; no known La Pavoni shots on this machine). Deterministic resolution is fully covered by the §3 regression tests; optional confirmation if a La Pavoni shot is pulled later.
