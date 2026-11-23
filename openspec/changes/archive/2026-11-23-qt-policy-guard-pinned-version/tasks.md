# Tasks

## 1. Guard and spec

- [x] 1.1 `CMakeLists.txt`: compare the Qt policy guard against `DECENZA_QT_VERSION` instead of
      `6.5.0`. Verified 2026-11-23: the build configures with no `QTP` warning.
- [x] 1.2 Modify `build-config`'s "No New Qt Policy Warnings" scenario to state the behaviour
      without naming the guard.
