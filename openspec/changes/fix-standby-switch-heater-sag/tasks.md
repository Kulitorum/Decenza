# Tasks

- [x] 1. Require `Error_NoAC` to persist for a settling interval before warning,
      matching de1app's 6 s.
- [x] 2. Record the hardware carve-out to the no-timers rule in CLAUDE.md, with the
      argument for why no event-based rule works for this signal.
- [x] 3. Add `[DE1][StandbySwitch]` logging at INFO: when the warning is shown,
      when it clears, and once per suppressed episode.
- [x] 4. Regression tests in `tests/tst_machinestate.cpp`.
- [x] 5. Full test suite green before opening the PR.
