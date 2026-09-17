## Why

`hds-firmware-update`'s promoted spec (archived 2026-09-16 as part of `add-hds-firmware-update`) was written against an earlier state of the implementation and was never updated for three things that landed in the same PR (#1952) after that change was archived: HDS refresh now rides the app-update checker's shared timer instead of never polling; a preview/rc-installed scale can be offered an equal-numbered (not strictly newer) stable release; and a WiFi-connected HDS's synchronous refusal is now surfaced to the user instead of always being silent. The promoted spec currently states the OLD behavior as a hard requirement — including one line ("SHALL NOT run a periodic HDS-update polling timer") the shipped code directly violates. Per project convention, a requirement change goes through a change delta rather than a hand-edit of `openspec/specs/`.

## What Changes

- Correct the "HDS release availability is lifecycle-driven" requirement: HDS refresh now shares `UpdateChecker`'s periodic timer (30 s post-startup kick, hourly thereafter) rather than never polling — gated by the same `Settings.app().autoCheckUpdates()` toggle and compiled out on iOS, matching the app-update checker exactly. Launch and resume-from-suspend remain unconditional triggers on every platform, and a manual check-for-updates action refreshes it too.
- Add a scenario to "HDS has no newer eligible release" for the preview/rc-to-equal-stable exception: a scale reporting a `-preview.<n>` or `-rc.<n>` installed version may be offered a numerically equal (never older) stable release, since that firmware-side allowance is real but not universal across preview builds — the scale itself remains the final authority and may still refuse it.
- Add a scenario to "A started update is never reported as an installed update" for WiFi's new synchronous-refusal surfacing, and note that Bluetooth/USB have no equivalent reply channel so continue to rely on reconnect-on-target-version or the scale's own display.

## Impact

- Specs: `hds-firmware-update` (modified requirements only — no behavior change, this corrects the spec to match already-shipped, already-tested code from PR #1952).
- No code changes; this change is documentation-only.
