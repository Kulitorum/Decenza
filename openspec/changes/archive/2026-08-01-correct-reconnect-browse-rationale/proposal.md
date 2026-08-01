## Why

The reconnect-browse requirement (#1739) states as fact that "a direct A-record query for the saved hostname is not a reliable way to locate this responder". A run on the same Android tablet and the same scale contradicts it: the direct A-query answered in **357 ms, one query, one record**, and the scale connected 4 s from launch with no browse involved.

So the 82 consecutive zero-record misses that motivated the change were almost certainly **not** a property of the responder. The most likely cause was already on record in this project's own notes and was not applied: after a **tablet reboot**, mDNS discovery works reliably, and pre-reboot failures were tablet-side stale state rather than scale or protocol behaviour. The tablet had been rebooted between the failing session and this one.

The behaviour the requirement mandates is unaffected — a browse fallback when the direct path fails is still correct and still recovered nothing incorrectly. What is wrong is the **stated cause**, and a wrong cause in a spec is worse than no cause: it is the sentence the next person reasons from.

## What Changes

- Rewrite the rationale paragraph of "Saved WiFi scale reconnect resolves by service browse" to describe what was **observed** (a persistent failure state in which the direct query returns nothing, cleared by a tablet reboot) rather than asserting a property of the responder.
- Record the counter-example explicitly, so the claim cannot be re-derived from the old text.
- Drop the "This is not an Android-only defect" framing to a weaker, accurate statement: the same *symptom* was seen once on macOS, which is a reason to keep the fallback cross-platform, not evidence of a shared root cause.
- No normative SHALL changes. Every requirement keeps its current behaviour.
- The same correction is applied to the code comments that carry the claim (`blemanager.cpp`, `decentscalewifi.cpp`), which are not spec text but say the same wrong thing.

## Capabilities

### New Capabilities

_None._

### Modified Capabilities

- `wifi-scale-discovery`: rationale text only, on the existing "Saved WiFi scale reconnect resolves by service browse" requirement. No scenario, no SHALL, and no behaviour changes.

## Impact

- `openspec/specs/wifi-scale-discovery/spec.md` — via this change's delta.
- `src/ble/blemanager.cpp`, `src/ble/scales/decentscalewifi.cpp` — comments asserting the responder never answers a bare A-query.
- No code behaviour change, so no new tests. The existing suite still applies unchanged.
