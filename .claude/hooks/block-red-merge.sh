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
cmd=$(printf '%s' "$payload" | python3 -c 'import json,sys; print(json.load(sys.stdin).get("tool_input",{}).get("command",""))' 2>/dev/null)

# Only `gh pr merge`. Not `gh pr view`, not `git merge`.
printf '%s' "$cmd" | grep -Eq '(^|[;&|[:space:]])gh[[:space:]]+pr[[:space:]]+merge([[:space:]]|$)' || exit 0

# Deliberate override for a red check known to be irrelevant. Reason required,
# same shape as `// log-marker-exempt:` in the source tree.
printf '%s' "$cmd" | grep -Eq 'merge-red-ok:[[:space:]]*[^[:space:]]' && exit 0

cd "${CLAUDE_PROJECT_DIR:-.}" || exit 2

# PR number if the command names one; absent, gh resolves the current branch.
pr=$(printf '%s' "$cmd" | sed -nE 's/.*gh[[:space:]]+pr[[:space:]]+merge[[:space:]]+([0-9]+).*/\1/p')

out=$(gh pr checks $pr 2>&1)
rc=$?

# gh encodes the verdict in its exit status: 0 = all passed, 8 = no checks
# reported, anything else = failing or pending. "No checks" is legitimate here —
# every workflow is path-filtered or tag-triggered, so a docs-only PR genuinely
# has nothing to report.
case $rc in 0|8) exit 0 ;; esac

# Red, still running, or gh could not tell us. One message, because the remedy is
# the same for all three and a gate that opens when it cannot see is the failure
# mode this hook exists to fix.
cat >&2 <<MSG
BLOCKED: this PR's checks are not green.

$out

Read the run before merging. A red \`main\` is not cleanup for later — every
later PR inherits it, and the next person cannot tell their change from yours.
If it is still running, wait for it; that result is the thing you are about to
act on.

To merge over a check you have read and judged irrelevant, say why on the
command line:
  gh pr merge ... # merge-red-ok: <reason>
MSG
exit 2
