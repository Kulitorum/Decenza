# Proposal

## Why

Three `build-config` requirements describe CI and the sanitizer setup as they once were, not as they are, so a reader trusting the spec gets the wrong picture of what is verified:

- It requires a nightly six-platform build. That build (`nightly-platforms.yml`) was removed; only the Linux sanitizer nightly runs, and the six platforms build on a `v*` tag or an on-demand dispatch (`docs/CLAUDE_MD/CI_CD.md`).
- It requires ASan and UBSan in every desktop Debug build. `CMakeLists.txt` auto-enables them only for single-config generators, and UBSan not under MSVC.
- It names "six platform workflows" and a "Qt 6.12" series. There is one `release.yml` with six platform jobs, and the version comes from `qt-version.txt`.

## What Changes

- `build-config`: "Verification Does Not Wait For A Release Tag" states the Linux sanitizer nightly plus on-demand and tag builds of all six platforms.
- `build-config`: "Debug Builds Are Instrumented By Default" is scoped to single-config Debug builds, with UBSan off under MSVC.
- `build-config`: "Qt 6.12 as Build Framework" names the release workflow's platform jobs and the version in `qt-version.txt`.
- `CLAUDE.md`'s sanitizer sentence matches the same scope.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `build-config`: the three requirements above.

## Impact

Spec and `CLAUDE.md` text only; no build or CI behaviour changes.
