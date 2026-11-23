# Proposal

## Why

The `build-config` spec quoted the `VERSION_GREATER_EQUAL "6.5.0"` guard around the Qt policy settings in `CMakeLists.txt`. That guard can never be false: `find_package(Qt6 ${DECENZA_QT_VERSION} REQUIRED …)` already refuses any older Qt. A spec that quotes a build file's text goes stale whenever the text is tidied, as this one did.

## What Changes

- `CMakeLists.txt`: the two `qt_policy(SET … NEW)` calls run unconditionally; the always-true guard is removed.
- `build-config`: the "No New Qt Policy Warnings" scenario states the behaviour (no `QTP` warnings; a policy that would warn is set explicitly) without quoting a guard or a Qt release.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `build-config`: "No New Qt Policy Warnings" no longer names a guard or a Qt release.

## Impact

`CMakeLists.txt` only; configure output is unchanged. No user-visible change.
