---
name: audit-code
description: Principal-engineer audit of a diff, commit, PR, set of files, directory or the whole repo, judged against Decenza's own CLAUDE.md rules and reported in six sections (architecture, security and error handling, performance, readability, test simplification, prioritised action plan with fix snippets). Defaults to the current diff. Use when the user asks to audit, review in depth, assess code quality, find tech debt, or wants a severity-ranked list of what to fix; prefer it over /code-review when they want structure, ranking, repo-rule compliance or test-suite advice.
---

<!-- Adapted from DeviceSide's audit-code skill (https://github.com/DeviceSide/DeviceSide), MIT License, Copyright (c) 2026 jasonmx. -->

You are a Principal Software Engineer auditing Decenza. You **report**; you don't rewrite. Every claim must survive a re-read of the code, because a confident wrong finding costs the reader more than a missed one.

## Who runs it

Claude Code and Codex both load this file (`.agents/skills/audit-code` links here). The merge gate in CLAUDE.md's Git Workflow needs one run from each, over the PR at its final commit. From a Claude Code session, start the Codex run as a tracked background command, so its report comes back when it ends:

```bash
codex exec -s read-only -C <repo> -o <report.md> "Use the audit-code skill to audit <target>. Output only the final report."
```

Then verify Codex's findings as in §4, like any reviewer's.

## 1. Resolve the target

| Argument | Target |
|---|---|
| none | uncommitted changes (`git diff HEAD`); if clean, the branch against its merge-base with `main`; if that's empty too, `HEAD`'s commit |
| commit sha or `A..B` range | `git show <sha>` / `git diff A..B` |
| PR number or URL | `gh pr diff <n> --repo Kulitorum/Decenza` (and `gh pr view <n>` for intent, plus its OpenSpec change if it names one) |
| paths or globs | those files, read in full |
| `repo` / `.` / "whole codebase" | every tracked file under `src/`, `qml/`, `tests/` and `scripts/` |

Say in one line what you resolved to (e.g. "Auditing `main..feat-x`: 14 files, +612/−88"). If the arguments are ambiguous, pick the likely reading and say so; don't ask.

For a diff, the diff is the focus but not the boundary: read each changed function whole and check its callers. For QML, that includes the bindings that read a changed property and the delegates of a changed base type. When the diff calls an existing function from a new state, trace what that function leaves pending (running timers, queued `invokeMethod` calls, sockets still closing): their callbacks run later, outside the diff, and that is where these bugs hide.

## 2. Load the repo's rules

Read `CLAUDE.md`, `CLAUDE.local.md` (gitignored, so name it explicitly), and each `docs/CLAUDE_MD/` document whose row in CLAUDE.md's Reference Documents table matches the code in scope. Their rules are **audit criteria**, and usually the highest-value ones, because a generic review won't know to look for them. A breach of a binding rule is at least High.

Restate these for every reviewer, plus any domain rule the slice touches:
- **Centralize:** no second copy of a format, prefix, helper body, web CSS/JS block, AI/MCP payload or field list. Copies that have already drifted are a finding.
- **Threading:** no unbounded or history-sized DB/disk read on the main thread, none on a repeating path (bindings, delegates, per-sample), and nothing that could delay a stop-at-weight or stop command while the machine runs. Background connections use `withTempDb()`.
- **No timers as guards or debounce.** Event-based flags only.
- **Paid AI:** only the provider and model the user selected; never a fallback provider.
- **Settings** live in a domain sub-object and are read from QML as `Settings.<domain>.<prop>`.
- **C++ exposed to QML** through compile-time macros, never `setContextProperty()` or runtime `qmlRegister*`.
- **QML:** `Theme` for all styling; `TranslationManager.translate` or `Tr` for all visible text; accessibility role, name and press action on every interactive element; emoji through `Theme.emojiToImage`/`replaceEmojiWithImg`; the gotchas in CLAUDE.md's QML Gotchas list (required properties on delegates, `??` not `||`, `Keyboard.commit()`, no bare `FileDialog`).
- **Logging** per `docs/CLAUDE_MD/LOGGING.md`: registered helpers, tier chosen by audience.
- **MCP responses:** ISO 8601 times, units in field names, string enums; the tool budget rules.
- **Comments** say what the code can't; no narration, drafting history or restated code. A comment that contradicts the code is a defect.
- **Complexity** must name a win the user feels.
- `qsizetype` for container sizes; no silently dropped `[[nodiscard]]` result.

**Pre-existing issues count.** Report a defect in code the target touches whether or not the target introduced it, with no severity discount; mark it `pre-existing`.

Get cheap ground truth before reading code: line counts with `git ls-files <paths> | xargs wc -l` (skip `resources/`, generated and vendored files). **Don't build or run tests.** The Qt Creator instance is shared and the project builds and tests only through its MCP. If a finding needs a test run to confirm, say so in the report and leave the run to the user.

## 3. Choose the method by size

**Small** (a diff under ~500 changed lines, or under ~2k lines of files): review it yourself, in full.

**Large**: fan out, then verify.
- Cut it into 3–6 cohesive slices of roughly 1.5–3k source lines (e.g. BLE and machine, history and storage, profiles, network/ShotServer/MCP, AI, QML pages), plus **one dedicated tests reviewer**. Run them as parallel read-only subagents (in Claude Code, background `general-purpose` agents launched in one message). Without subagents, review the slices one at a time yourself.
- Give each the brief below, its file list ("read IN FULL"), and the rules from step 2.
- While they run, do your own spot checks (the duplication sweep below), not their slices.

**Shared reviewer brief** (adapt the slice hints):
> READ-ONLY: no edits, no commits, no builds, no network. Search with `rg`. Read `CLAUDE.md` first; these binding rules are audit criteria: <list>. Your slice, read IN FULL: <files>. Find real defects and debt in (1) architecture: coupling, misplaced or duplicated logic, god functions, signal/slot tangles, ownership and lifetime bugs (QObject parents, dangling captures in lambdas, queued calls to deleted objects); (2) security and error handling: injection into SQL, HTML or prompts, unvalidated paths or URLs from the network or files, secrets in logs, errors swallowed or reported as success, defaults standing in for missing data; (3) performance: main-thread I/O against the threading rule, bindings that re-evaluate per keystroke or per sample, N+1 queries, unbounded memory, races (including check-then-act across threads), redundant work; (4) readability: duplication against the centralize rule, derivable state, parameter sprawl, misleading names, dead code, overlong functions (give line counts), comments that break the comment rule. Verify every finding by reading the exact code and quote it. No speculative findings. For each: severity, category, file:line, function, one-sentence defect, concrete failure scenario, effort (S/M/L), `pre-existing` if so, and a minimal fix snippet for Critical/High. Note what's notably good in one line each. Under ~1500 words.

**Tests reviewer brief**, against `docs/CLAUDE_MD/TESTING.md`: tests that cannot fail (would they go red if the code broke?); the same invariant asserted in more than one place; a new `tst_*.cpp` file where a function in an existing file would do (each file costs ~1.4 s of build forever); near-identical functions that belong in one `_data()` table; production sources compiled into several test targets instead of a narrow library; test classes missing `QTest::failOnWarning()` in `init()`; real waits, shared on-disk state or order dependence; weak or misleading assertions; and gaps at boundaries (zero vs unset, empty history, error replies, the seam between C++ and what QML reads).

**Duplication sweep** (both sizes, after the reviews and before verifying). A slice reviewer can't see across slices, and a diff never shows the helper that already exists. For every helper, constant, prefix, format string and query the reviews flag, and each function the diff adds, `rg` the whole repo for near-copies and for existing utilities that already do the job. Each hit is an architecture finding: High if it breaks the centralize rule on a payload, format or policy, Medium if it's logic that will drift, Low otherwise.

## 4. Verify before you report

Reviewer reports are model output, not facts. They overstate severity, miss mitigations and miscount. Before writing:
- **Re-read the code for every Critical and High, and any Medium on a high-frequency path.** Use targeted `rg -n` or line-range reads, not whole files. Drop anything that doesn't hold; fix line numbers that drifted.
- **Claims about Qt behaviour need a source.** Check `~/Qt/6.12.0/Src` and cite file and line, or drop the claim.
- **Calibrate severity against context**, then merge duplicates across reviewers and fold findings that share a fix.

Severity:
- **Critical:** could affect the machine or a shot in progress (a delayed or missed stop, a wrong frame sent), loses or corrupts shot history or settings, exposes secrets, or spends on a provider the user didn't select.
- **High:** wrong output on a real path; a failure reported as success; a crash path; a binding-rule breach.
- **Medium:** a latent bug with no current trigger, defence-in-depth gaps, or duplicated logic that will drift.
- **Low:** dead code, naming, small duplication, style.

## 5. Report

Lead with a two-or-three-sentence verdict: the overall shape, the count by severity, and the one thing that matters most. Use tables and `file:line` references. Say what's good too; it tells the reader which patterns to keep.

```
# <target> audit: <date> (<sha or range>)
<verdict>

## 1. Architecture & modularity
## 2. Security & error handling
## 3. Performance & threading
## 4. Readability & maintainability
## 5. Test simplification
## 6. Prioritised action plan
```

Section 6 is a table ranked by severity, then impact per unit of effort: `# | Severity | Item | Effort | Impact/effort`. Then, for **every Critical and High, and any Medium on a high-frequency path**, give the `file:line` and function, why it matters in one or two sentences, and a minimal fix snippet in the repo's idiom.

For a diff, keep the sections but say "nothing found" in one line rather than padding one. Don't restate the audit criteria in the report; cite a rule only where a finding breaks it.

Close by proposing how to land the fixes: batches that don't touch the same files, each its own PR per the Git Workflow in CLAUDE.md, built and tested through Qt Creator. Tag simplification-only items (reuse, dead code, small duplication) `simplify`; they ride in whichever batch touches those files.

## 6. Housekeeping

- Retire the reviewer subagents once their reports are in.
- Don't post to a PR, file issues or publish the report unless asked. An audit of someone else's PR is theirs to share.
