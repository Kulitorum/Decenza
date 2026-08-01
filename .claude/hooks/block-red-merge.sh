#!/usr/bin/env bash
# Block `gh pr merge` unless the PR's checks are green.
#
# The merge-pr skill already said to check CI first. #1729 was merged with its own
# text-invariants run red anyway, because the merge ran as a plain `gh pr merge`
# and the skill was never invoked. A rule that lives only in a skill protects only
# the path that goes through the skill.
#
# Not a substitute for a required status check on GitHub — that needs repo admin
# we do not have. This covers the one path an assistant actually takes.
#
# Reads the PreToolUse JSON payload on stdin. Exit 2 = block, message to stderr.

payload=$(cat)

block() { printf 'BLOCKED: %s\n' "$1" >&2; exit 2; }

# One python call does the parsing: the command, its PR target, and its --repo.
# Target is searched across the WHOLE argument list, not just the token after
# `merge` — `gh pr merge --squash 1729` and the PR-URL form are both accepted by
# gh, and a parser that only looks right after `merge` silently falls back to the
# current branch's PR, i.e. checks a different (possibly green) PR and allows the
# merge. That was a real hole, reproduced against red #1729.
parsed=$(printf '%s' "$payload" | python3 -c '
import json, re, sys
cmd = json.load(sys.stdin).get("tool_input", {}).get("command", "")
if not re.search(r"(^|[;&|\s])gh\s+pr\s+merge(\s|$)", cmd):
    print("SKIP"); sys.exit(0)
if re.search(r"merge-red-ok:\s*\S", cmd):
    print("OVERRIDE"); sys.exit(0)
tail = re.split(r"gh\s+pr\s+merge", cmd, maxsplit=1)[1]
toks = tail.split()
repo = ""
for i, t in enumerate(toks):
    if t == "--repo" and i + 1 < len(toks): repo = toks[i + 1]
    elif t.startswith("--repo="): repo = t.split("=", 1)[1]
target = ""
for i, t in enumerate(toks):
    if i and toks[i-1] in ("--repo", "-R", "--subject", "--body", "-t", "-b"): continue
    m = re.match(r"^https://[^/]+/[^/]+/[^/]+/pull/(\d+)", t)
    if m: target = m.group(1); break
    if re.fullmatch(r"\d+", t): target = t; break
print("CHECK"); print(target); print(repo)
')

# python3 missing, or the payload shape changed: we cannot read the command. Do
# not wave it through — but do not block every Bash call either. Fall back to a
# grep on the raw payload so only merges are stopped.
if [ -z "$parsed" ]; then
    printf '%s' "$payload" | grep -q 'gh pr merge' \
        && block "cannot read the PreToolUse payload (python3 missing or shape changed), so this merge cannot be verified. The gate is not functioning — fix it rather than working around it."
    exit 0
fi

verdict=$(printf '%s' "$parsed" | sed -n 1p)
[ "$verdict" = "SKIP" ] && exit 0
if [ "$verdict" = "OVERRIDE" ]; then
    echo "block-red-merge: overridden by merge-red-ok" >&2
    exit 0
fi

target=$(printf '%s' "$parsed" | sed -n 2p)
repo=$(printf '%s' "$parsed" | sed -n 3p)

cd "${CLAUDE_PROJECT_DIR:-.}" || block "CLAUDE_PROJECT_DIR is not a directory, so the PR's checks cannot be read."

out=$(gh pr checks $target ${repo:+--repo "$repo"} 2>&1)
rc=$?

# gh's exit codes, from `gh pr checks --help`: 0 = all passed, 8 = checks
# PENDING, 1 = failing OR "no checks reported". 8 does NOT mean "no checks" —
# assuming it did is how an earlier cut of this hook allowed merges mid-run,
# which is #1729's exact shape. Verified against gh 2.96.0 and real PRs
# (#1731 -> 0, #1729 -> 1 fail, #1700/#1500 -> 1 "no checks reported").
[ $rc -eq 0 ] && exit 0

# A PR outside every workflow's path filter genuinely has nothing to gate on.
printf '%s' "$out" | grep -q 'no checks reported' && exit 0

block "this PR's checks are not green$([ -n "$target" ] && echo " (checked #$target)").

$out

Read the run before merging. A red \`main\` is not cleanup for later — every
later PR inherits it, and the next person cannot tell their change from yours.
If it is still running, wait for it; that result is the thing you are about to
act on.

To merge over a check you have read and judged irrelevant, say why on the
command line:
  gh pr merge ... # merge-red-ok: <reason>"
