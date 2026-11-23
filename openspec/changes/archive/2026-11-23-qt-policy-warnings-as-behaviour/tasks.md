# Tasks

## 1. Policy settings and spec

- [x] 1.1 `CMakeLists.txt`: remove the always-true guard so the two `qt_policy(SET … NEW)` calls
      run unconditionally. Verified 2026-11-23: the build configures with no `QTP` warning.
- [x] 1.2 Modify `build-config`'s "No New Qt Policy Warnings" scenario to state the behaviour
      without naming a guard.
