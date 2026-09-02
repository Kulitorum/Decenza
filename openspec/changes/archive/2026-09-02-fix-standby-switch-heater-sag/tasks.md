# Tasks

- [x] 1. Require `Error_NoAC` to persist for a settling interval before warning. The
      interval is our own estimate, NOT de1app's — de1app warns immediately, and its
      `after 6000` is a one-shot post-connect re-check.
- [x] 2. State in the PR that this knowingly departs from the no-timers rule, and leave
      CLAUDE.md alone until a field log confirms the mechanism.
- [x] 3. Add `[DE1][StandbySwitch]` logging at INFO: when the warning is shown,
      when it clears, and once per suppressed episode.
- [x] 4. Regression tests in `tests/tst_machinestate.cpp`, including one that lets the
      real timer fire rather than hand-firing its effect.
- [x] 5. Log content asserted, not just emitted — the log lines are the deliverable.
- [x] 6. Full test suite green before opening the PR.
