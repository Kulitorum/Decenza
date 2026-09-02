# Tasks

- [x] 1. Latch an `Error_NoAC` episode that begins from a heating substate and
      suppress the warning for its duration.
- [x] 2. Reuse the existing heating-substate predicate rather than a second copy
      of the substate set; rename it to suit all three readers.
- [x] 3. Add `[DE1][StandbySwitch]` logging: INFO when the warning is shown and
      when it clears, DEBUG once per suppressed episode.
- [x] 4. Regression tests in `tests/tst_machinestate.cpp`.
- [x] 5. Full test suite green before opening the PR.
