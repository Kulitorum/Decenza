#!/usr/bin/env bash
# Block `gh pr merge` while the PR's own checks are red or still running.
#
# The merge-pr skill already says to check CI before merging. That did not help:
# #1729 was merged with its own `text-invariants` run red, because the merge was
# run as a plain `gh pr merge` and the skill was never invoked. A rule that lives
# only in a skill protects only the path that goes through the skill.
#
# This is not a substitute for a required status check on GitHub — that is the
# real fix and needs repo admin, which we do not have. It covers the one path an
# assistant actually takes.
#
# Reads the PreToolUse JSON payload on stdin. Exit 2 = block, message to stderr.

payload=$(cat)
cmd=$(printf '%s' "$payload" | python3 -c 'import json,sys; print(json.load(sys.stdin).get("tool_input",{}).get("command",""))' 2>/dev/null)

[ -z "$cmd" ] && exit 0

# Only `gh pr merge`. Not `gh pr view`, not `git merge`.
printf '%s' "$cmd" | grep -Eq '(^|[;&|[:space:]])gh[[:space:]]+pr[[:space:]]+merge([[:space:]]|$)' || exit 0

# Deliberate override, for the case where a red check is known-irrelevant. Must
# carry a reason, same shape as `// log-marker-exempt:` in the source tree.
if printf '%s' "$cmd" | grep -Eq 'merge-red-ok:[[:space:]]*[^[:space:]]'; then
    exit 0
fi

cd "${CLAUDE_PROJECT_DIR:-.}" || exit 0

# PR number is the first bare token after `merge`, if any. Absent, gh resolves
# the current branch — which is what we want anyway.
pr=$(printf '%s' "$cmd" | sed -nE 's/.*gh[[:space:]]+pr[[:space:]]+merge[[:space:]]+([0-9]+).*/\1/p')

checks=$(gh pr checks $pr --json name,bucket,link 2>&1)
rc=$?

# gh exits 8 for "no checks reported". That is a legitimate state here: every
# workflow is path-filtered or tag-triggered, so a docs-only PR genuinely has
# nothing to report. Nothing to read means nothing to block on.
if [ $rc -eq 8 ] || printf '%s' "$checks" | grep -q "no checks reported"; then
    exit 0
fi

# Any other gh failure: block rather than wave it through. A gate that opens
# when it cannot see is the failure mode that produced this hook.
if [ $rc -ne 0 ] && ! printf '%s' "$checks" | grep -q '^\['; then
    cat >&2 <<MSG
BLOCKED: could not read this PR's checks, so the merge is not safe to run.

  gh said: $(printf '%s' "$checks" | head -3)

Fix the gh/network problem and retry. If the check state is genuinely
unknowable and you have read the runs by hand, append a reason to the command:
  gh pr merge ... # merge-red-ok: <why>
MSG
    exit 2
fi

verdict=$(printf '%s' "$checks" | python3 -c '
import json, sys
try:
    rows = json.load(sys.stdin)
except Exception:
    sys.exit(0)
bad  = [r for r in rows if r.get("bucket") == "fail"]
wait = [r for r in rows if r.get("bucket") == "pending"]
if bad:
    print("FAIL")
    for r in bad:
        print("  {}  {}".format(r.get("name",""), r.get("link","")))
elif wait:
    print("PENDING")
    for r in wait:
        print("  {}".format(r.get("name","")))
' 2>/dev/null)

case "$verdict" in
  FAIL*)
    cat >&2 <<MSG
BLOCKED: this PR's checks are RED. Merging would land a known-broken commit on main.

$(printf '%s' "$verdict" | tail -n +2)

Read the run and fix it. \`main\` going red is not a CI problem to clean up
later — every later PR inherits it, and the next person cannot tell their own
change from yours.

If the failure is genuinely unrelated and you have read it, say so on the
command line:
  gh pr merge ... # merge-red-ok: <why this red check does not matter>
MSG
    exit 2
    ;;
  PENDING*)
    cat >&2 <<MSG
BLOCKED: this PR's checks are still running. Their result is the thing you are
about to act on, so wait for it.

$(printf '%s' "$verdict" | tail -n +2)

Wait with a Monitor or a backgrounded \`gh pr checks $pr --watch\`, not by
merging and hoping. Override only if you have decided the pending check is
irrelevant:
  gh pr merge ... # merge-red-ok: <why>
MSG
    exit 2
    ;;
esac

exit 0
