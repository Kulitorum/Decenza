## Why

`storageHint` is the user's **plan for how beans are stored once out of the freezer** — forward-looking on a frozen bag ("when I thaw this, it goes in a vacuum jar"), descriptive on a thawed or never-frozen one. The shipped model (#1510) instead treats it as a competing way to express frozen-ness, and so hides the dropdown and **clears the value to null** whenever the bag is frozen. That destroys the plan at exactly the moment it matters most, and silently wipes values set through the `bag_update` MCP tool.

The two fields were never in competition. `frozenDate`/`defrostDate` describe the **freezer axis**; `storageHint` describes the **container axis** — how the beans are kept when *not* in the freezer. They are orthogonal and cannot disagree. The only invariant that ever mattered is that `storageHint` has no `"frozen"` **value**, which the enum already guarantees without any hiding or clearing.

The spec says as much and then contradicts itself: `bag-freeze-lifecycle` states a bag "may carry both ... — the two pairs are independent" (line 10), then requires the dropdown "shown only while the freeze toggle is OFF" (line 66), requires `storageHint` "cleared to null on save" when freezing (lines 78-80), and gates "Mark Opened" to cards "where `frozenDate` is null" (line 83). It declares the combined state legal and then makes it unreachable. The archived `design.md:28` names precisely that state — "frozen AND later `openedDate` once thawed and moved to a counter jar" — as the reason the columns are distinct.

The shipped AI guidance already assumes the correct model: the `beanFreshness` instruction tells the model "Freezing (and airtight/vacuum storage) pauses staling", i.e. the hint outlives the freezer. Only the data model prevented it from surviving to be read.

## What Changes

- **Redefine `storageHint`** as the out-of-freezer storage plan, orthogonal to the freezer axis — replacing the "describes non-frozen storage only" wording. Unchanged: no `"frozen"` value; `frozenDate` remains the sole source of truth for frozen-ness.
- **The storage-hint dropdown is always visible**, never hidden by the freeze toggle. Removes the hide scenario and the clear-on-freeze scenario.
- **Stop force-clearing `storageHint` and `openedDate` on save.** This is the data-loss half of the bug and is independent of the visibility question.
- **"Mark Opened" becomes available on thawed bags** (`frozenDate` set *and* `defrostDate` set), not only never-frozen ones. A thawed bag will show both "Thaw" and "Mark Opened" — a deliberate, visible bag-card change.
- **Regression tests** for the force-clear, for the frozen + thawed + hint + `openedDate` combination, and for an MCP-set hint surviving a dialog save.

Not breaking: the columns already exist, no migration is needed, and existing shot snapshots are immutable and unaffected. The change only widens which states are representable — every state legal today stays legal.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `bag-freeze-lifecycle`: the dropdown-visibility requirement is rewritten (always visible); the hide scenario and the clear-on-freeze scenario are removed; "Mark Opened" is re-gated from "never frozen" to "no portion currently in the freezer"; the fields' independence is stated as an orthogonal-axes rule rather than asserted and then contradicted.
- `coffee-bag-model`: the `storageHint` field definition is rewritten from "describes non-frozen storage only" to the out-of-freezer storage plan, with the no-`"frozen"`-value invariant retained and its rationale corrected.

## Impact

**Code**
- `qml/components/ChangeBeansDialog.qml` — dropdown visibility (`:1736`), the `storageHint` force-clear on save (`:593`), the `openedDate` force-clear on save (`:604`), `openedDate` field visibility (`:1763`).
- `qml/components/BagCard.qml` — "Mark Opened" gating (`:454`); the `isFrozen` derivation (`:28`) conflates "has ever been frozen" with "is currently in the freezer".

**Root cause note.** `isFrozen`/`fFreeze` are derived from `frozenDate` presence alone (`BagCard.qml:28`, `ChangeBeansDialog.qml:466`), so a thawed bag — which keeps its `frozenDate` — reads as frozen forever. Any gating that must distinguish "currently in the freezer" needs `frozenDate` set **and** `defrostDate` empty.

**Not affected**
- No database migration: `storage_hint`/`opened_date` already exist on `coffee_bags` and `shots`.
- No shot-snapshot change: snapshots are immutable and already carry all four lifecycle fields.
- No MCP schema change: `bag_update` already accepts both fields; this stops the UI from wiping what it writes.
- No Visualizer sync change: both fields remain local-only.
