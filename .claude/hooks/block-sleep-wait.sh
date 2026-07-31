#!/usr/bin/env bash
# Block Bash commands that use sleep as a wait.
#
# Waiting by sleeping burns real wall-clock time and tells you nothing. Every
# thing worth waiting on here has a blocking API:
#   builds  -> mcp__qtcreator__get_build_status  { "wait": true }
#   tests   -> mcp__qtcreator__get_test_status   { "wait": true }
#   a condition that only a shell can see -> Monitor, or Bash run_in_background
#                                            with an `until <cond>; do ...` loop
#
# Reads the PreToolUse JSON payload on stdin. Exit 2 = block, message to stderr.
payload=$(cat)
cmd=$(printf '%s' "$payload" | python3 -c 'import json,sys; print(json.load(sys.stdin).get("tool_input",{}).get("command",""))' 2>/dev/null)

[ -z "$cmd" ] && exit 0

# Bare `sleep N`, or a poll loop built out of sleep. Deliberately narrow: this
# targets sleep used AS the wait, not sleep buried in an unrelated script.
if printf '%s' "$cmd" | grep -Eq '(^|[;&|[:space:]])sleep[[:space:]]+[0-9.]+'; then
    cat >&2 <<'MSG'
BLOCKED: `sleep` is not how you wait here.

  Build  -> mcp__qtcreator__get_build_status with {"wait": true}
  Tests  -> mcp__qtcreator__get_test_status  with {"wait": true}
            (run_tests with wait_for_completion:false, then poll that)
  Other  -> Monitor, or Bash run_in_background with `until <condition>; do :; done`

Do not spawn a background sleep-poller either, and never more than one waiter
for the same thing.
MSG
    exit 2
fi
exit 0
