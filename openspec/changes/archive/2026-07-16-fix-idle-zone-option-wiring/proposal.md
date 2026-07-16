## Why

The per-zone options (`distribution` / `alignment` / `style`) are offered for every zone by both editors, persisted in the layout JSON, and rendered by both previews — but the idle screen honors them for only two of nine zones.

[#1517](https://github.com/Kulitorum/Decenza/pull/1517) found this for `alignment` on the center zones. Auditing the rest of the wiring showed the gap is much wider:

| Zone | Renderer | distribution | alignment | style |
|---|---|---|---|---|
| `statusBar` | `StatusBar.qml` | yes | yes | yes |
| `lowerMidBar` | `IdlePage.qml` | yes | yes | yes |
| `topLeft` / `topRight` | `IdlePage.qml` | **no** | **no** | **no** |
| `bottomLeft` / `bottomRight` | `IdlePage.qml` | **no** | **no** | **no** |
| 3 center zones | `LayoutCenterZone.qml` | n/a | **no** | **no** |

`LayoutBarZone` implements all three options correctly; `IdlePage` simply never passed them to the four top/bottom bar zones, so they silently fell back to the component defaults. `LayoutCenterZone` implemented neither `alignment` nor `style`.

The result is a dead control: a user picks an option, watches both previews honor it, and sees nothing on the idle screen.

## What Changes

- **Wire the four top/bottom bar zones** — `topLeft`, `topRight`, `bottomLeft`, `bottomRight` receive `distribution` / `alignment` / `style` from the layout config, as `lowerMidBar` already did. This is the bulk of the fix and adds no new mechanism.
- **Center zones honor `alignment`** — via the same auto-spacer mechanism `LayoutBarZone` uses (the original #1517 change).
- **Center zones honor `style`** — item text color and value bold, matching what the top/bottom bar zones already do. Center zones paint no background of their own; only `statusBar` and `lowerMidBar` do.
- **Center zones drop `distribution`** — the option is hidden for them in both editors and no longer faked by the web preview. A center zone sizes every item from a fixed cell (`buttonWidth`, capped at `Theme.scaled(150) * zoneScale`) so its action buttons never stretch, which is precisely what `equalWidth` and `spaced` require. With the cap kept the two values collapse into each other and do nothing at all on a button-only row like `centerTop`, so the honest fix is to stop offering them rather than ship a third dead control.
- **A single `zoneOpts(zone)` accessor** in `IdlePage` replaces three hand-rolled per-zone lookups, so a future zone is wired by one consistent line.

## Impact

Existing layouts that never set these options are unchanged: every default (`packed` / `center` / `standard`) resolves to exactly today's rendering.

Layouts that **did** set them will change on upgrade — the idle screen starts honoring a choice the user already made and previewed. That is the intent of the fix, but it is a visible change rather than a no-op, and worth calling out in release notes.

## Capabilities

### New Capabilities

_None — this makes shipped options work as already specified._

### Modified Capabilities

- `layout-zone-configuration`: the item-distribution option is scoped to the bar zones (the center zones' fixed-cell sizing cannot express it); alignment and style are clarified to apply to every zone, and the idle screen is required to honor what the editors offer.
