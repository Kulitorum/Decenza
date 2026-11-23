# Proposal

## Why

The `build-config` spec names the `VERSION_GREATER_EQUAL "6.5.0"` guard around the Qt policy settings in `CMakeLists.txt`. With the CMake minimum at the version in `qt-version.txt`, that guard is always true and its `6.5.0` says nothing about the Qt the project builds against. The guard moves to the pinned version, and the spec should state the behaviour (no policy warnings) rather than the guard's text, which is what made it go stale.

## What Changes

- `CMakeLists.txt`: the Qt policy guard compares against `DECENZA_QT_VERSION` (from `qt-version.txt`) instead of `6.5.0`.
- `build-config`: the "No New Qt Policy Warnings" scenario describes policies being set explicitly so configure stays warning-free, without naming the guard or a Qt release.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `build-config`: "No New Qt Policy Warnings" no longer names a specific guard or Qt release.

## Impact

`CMakeLists.txt` only; configure output is unchanged. No user-visible change.
