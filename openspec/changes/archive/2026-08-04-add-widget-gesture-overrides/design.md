## Context

Ten built-in action widgets hard-code their gestures. Where they are hard-coded depends on
the zone, because these widgets render **two different ways**:

- **Center and action zones** — `LayoutItemDelegate.compileToCustom(type)` builds a
  CustomItem-shaped object (`action`, `longPressAction`, `doubleclickAction`, emoji,
  content, background) and the widget renders through `CustomItem.qml`. Dispatch therefore
  already goes through `executeActionString()`, and the whole action catalog is already
  reachable — this half needs storage, not plumbing.
- **Every other zone** — `items/<Type>Item.qml`, each with its own
  `onAccessibleLongPressed` / `onAccessibleDoubleClicked` calling a local function
  (`BeansItem.qml:181-182` → `goToBeanInfo()`, `EquipmentItem.qml:173-174` →
  `goToEquipment()`). These know nothing about action strings.

The gestures divide into exactly two groups, and the division is not stylistic — it is
about whether the widget's page has another way in:

| Group | Tap | Long-press | Double-click |
|---|---|---|---|
| recipes, beans, steam, hotwater, flush, espresso, equipment | `togglePreset:<mode>` | opens page | opens page |
| history, autofavorites, settings | opens page | *(empty)* | *(empty)* |

For the first group the page is reachable ONLY by gesture, and both gestures currently do
the same thing. That redundancy is the budget this change spends: override one, keep the
other. For the second group tap opens the page, so both gestures are genuinely free.

**The blocking defect.** `LayoutItemDelegate.qml:293` builds the compiled item as
`var merged = { id: ..., type: ... }` and then copies **only** the compiled keys. Stored
per-instance properties are discarded on every compiled widget. So today, even if a
`longPressAction` were written to a Beans widget, it would never be read.

## Goals / Non-Goals

**Goals:**

- Per-instance long-press / double-click overrides on the ten action widgets, from the
  existing action catalog.
- The one-slot rule enforced *and visible*, so a user cannot strand their own page and can
  see why the second slot is locked.
- Identical behaviour in both render formats and both editors.
- Defaults byte-identical until a user stores an override.

**Non-Goals:**

- No override of **tap**. Tap is the widget's reason to exist (start steam, toggle the
  recipe row); a widget whose tap is redefined is a Custom widget, and that already exists.
- No new actions. This consumes `layoutActionTable()` as-is.
- No gesture overrides on readout widgets (Temperature, Scale Weight, …) — they have no
  gesture behaviour to compose with, and adding one is a separate question.
- Not unifying the two render formats. That is a much larger refactor and this change does
  not need it.

## Decisions

### Two option keys, and the slot rule lives in the schema

`longPressAction` and `doubleclickAction`, reusing the Custom widget's key names so a
stored value means the same thing everywhere and the merge is a plain property override.

The per-type rule — how many slots, which destination the reserved one opens — goes in the
capability schema in `settings_network.cpp`, beside `readoutOptionSchema()`. Both editors
derive it. The alternative (each editor knowing "steam reserves a gesture for the Steam
page") is two lists that must agree about ten widgets, which is precisely the drift the
action catalog was just centralized to end — and the web copy of that list was sixteen
entries stale when it was found.

Shape: per type, the option keys plus a `reservedDestination` (the navigate action the
locked gesture performs, or absent for the two-slot widgets). The editors then say
"Opens Steam" by resolving that action id through `layoutActionLabels()` — no hand-written
destination strings on either surface.

### The reserved slot is chosen by which one the user fills

Not fixed to long-press. The rule is: on a one-slot widget, the *unoverridden* gesture is
the reserved one. Fill long-press and double-click locks; fill double-click and long-press
locks; clear the override and both are free again.

This costs nothing over a fixed slot — the editor is disabling whichever is empty rather
than a named one — and it lets a user who finds double-click unreliable on their tablet put
their action on long-press instead, which is the actual reason to offer the choice.

Alternative considered: allow both and warn. Rejected — a warning that can be clicked past
leaves the user with a Steam widget whose page they cannot open, and the recovery (find it
in the layout editor, clear a gesture) is exactly the path a confused user won't find.

### Compiled path: stored properties win over compiled defaults

The merge inverts to: start from the stored `modelData`, then apply compiled keys, then
re-apply the stored gesture keys on top. Only the two gesture keys are allowed to override
a compiled value — a stored `content` or `emoji` on a compiled widget stays discarded,
because those are the widget's identity and a stale copy of them in an old layout should
not resurrect.

Stated as a rule rather than a diff, because "apply stored over compiled" sounds like the
whole fix and is not: an unrestricted merge would let a layout saved before this change
resurrect stale `content`/`backgroundColor` values that the compiled defaults have since
changed, silently changing how existing widgets look.

### Dedicated items: route the existing handler through the override

Each `items/<Type>Item.qml` keeps its handlers but consults the stored override first —
one shared helper, since the shape is identical in all ten files: if an override is stored
for this gesture, run it through the action dispatcher; otherwise call the local function
as today.

That helper is the thing to get right, because ten hand-written copies of "check override,
else default" is the shape this codebase keeps finding drifted. The dispatcher itself is
`CustomItem.executeActionString()`, which is not currently reachable from a sibling item —
so it moves to somewhere both can call (a small QML singleton or a shared JS module),
with `CustomItem` calling the same one. That extraction is the only structural work in this
change.

### Both editors reuse the Custom widget's action picker

In-app: a Gestures section in the instance editor, with two rows that open the same
`SelectionDialog` the Custom editor uses. Web: the same two rows in the instance editor,
built from the already-injected `LAYOUT_ACTION_CATALOG`. Neither surface gains an action
list; both already have one.

## Risks / Trade-offs

- **Ten files gain a behaviour they did not have** → the shared helper keeps it one
  implementation, but each file still needs its handlers routed, and a missed file is a
  widget that silently ignores its stored override in one format only. A test that asserts
  every gesture-capable type routes through the helper is the guard; it is a slot in an
  existing test file, not a new one.
- **Extracting `executeActionString()` touches the Custom widget's dispatch** — the code
  path every existing Custom widget already depends on, and which this session's work just
  landed. The extraction must be behaviour-preserving and is worth verifying against the
  existing Custom widgets before the new callers are added.
- **A user can still make a confusing widget** — "long-press Steam goes to Settings" is
  permitted and strange. That is the same latitude the Custom widget has; the one-slot rule
  guards reachability, not taste.
- **`pragma ComponentBehavior: Bound` and required properties** — several of these items
  have delegates (`BeansItem`'s pill row). Adding properties to a delegate's base type has
  broken `modelData` in unrelated files here before, and neither the compiler, qmllint nor
  the suite catches it. Any touched screen needs opening.
- **Settings widget** — included for consistency, but a stray gesture on the Settings
  button is the one that could annoy rather than help. Easy to drop from the schema if it
  proves unwanted; nothing else depends on it.

## Migration Plan

None. New option keys are absent from every existing layout, and absent means "behave as
today". The compiled-merge change is gated to the two gesture keys, so no existing widget's
appearance moves. Library items and imported layouts are unaffected.

## Open Questions

None outstanding. Two were settled before writing: Flush, Profiles and Settings are
included (identical structure to the widgets named, so excluding them would leave an
unexplained exception), and the reserved slot locks in the editor rather than warning.
