# Replace grind/RPM text inputs with the grind picker

## Why

The grind quick-select pill and its `GrindPickerDialog` wheel are the best grind
control the app has, and they exist in exactly one place — the brew bar. Every
other surface that accepts a grind or RPM value still uses a bare text field, and
an audit of all seven of them found they have quietly drifted apart: two offer
history suggestions and two do not, one represents "unset" as `""` while the rest
use `0`, one reads a different RPM-capability source than every other site, and
one RPM field is not registered for soft-keyboard avoidance at all. The two
surfaces with the *worst* grind entry — creating a bag and creating a recipe — are
the two where the user is least likely to remember their setting.

The wheel cannot simply be dropped in, because it has no way to express a value it
cannot generate rows for: a blank new bag, an unparseable notation, or a target far
outside the current step range. That gap is why this change pairs the rollout with
keyboard entry inside the picker — not as a convenience, but as the mechanism that
makes the picker viable on surfaces that start empty.

## What Changes

- **`GrindPickerDialog` gains a keyboard mode.** A visible icon button in the
  dialog header toggles the two wheels to two text fields and back; the icon swaps
  between a keyboard glyph and a picker glyph so it always indicates the
  destination. No hidden gesture. (`ValueInput`'s existing double-tap-to-type is
  the anti-pattern being corrected — it is undiscoverable.)
- **Text is the ground truth; the wheel is an accelerator over it.** The picker
  SHALL NOT gate what a grind value can be. Typing a value the wheel was not
  offering is accepted verbatim and the wheels re-seed centred on it, rather than
  snapping to the step lattice.
- **The dialog opens in text mode whenever the wheel cannot express the value** —
  no rows at all (new bag, new recipe), unparseable notation, or a value outside
  what stepping can reach. The current "Set a grind value in Brew Settings first"
  empty state is removed; that dead end becomes the entry point.
- **No validation is added, and one existing guard is removed.** Off-grid values,
  values past a dial's printed maximum, and free text are all stored as-is. The
  `stepped < 0.0` row-skip is dropped for plain-numeric grinders: a stepless
  collar whose zero is a user-set calibration reference (the Niche Zero) can
  legitimately be dialled finer than zero, and both parsers already accept a
  leading `-` — only the steppers refused. Compound rotation notation keeps the
  skip, since a negative rotation count is meaningless.
- **The widget is split into reusable layers.** The stepping/row-generation logic
  (~200 lines currently inside `GrindQuickSelectItem.qml`) and the value source are
  separated from the brew-bar pill, producing a shared `GrindField` with two
  presentations — `pill` (today's brew-bar capsule, unchanged) and `field` (a
  bordered control that drops in where a text field is today). Both are
  **tap-to-open**: hosts keep no inline grind/RPM text inputs; all typing happens
  in the picker. Committing an emptied grind field is an explicit clear on
  `field` hosts (blank is load-bearing — a recipe with no pinned grind adopts the
  bag's dial); the brew-bar pill keeps ignoring empty commits.
- **All four QML grind/RPM sites adopt the shared control**: `BrewDialog`,
  `PostShotReviewPage`, `ChangeBeansDialog`, `RecipeWizardPage`.
- **ShotServer adopts a web-native equivalent**, not a ported wheel: `<input
  list>` + `<datalist>` seeded with the same stepped candidates. One native
  element that is simultaneously the quick-select and the improved text entry, and
  which keeps free text. Candidates are computed server-side by the existing C++
  stepping machinery and delivered in the JSON payloads the forms already fetch,
  resolved against each record's own grinder (no JS stepping port). Applied to
  the shot, bag, and recipe edit forms via a shared helper (no per-page
  inlining).
- **Every grinder-derived behaviour resolves against the grinder that owns the
  value** — the shot's grinder in post-shot review, the recipe's selected package
  in the wizard, the bag's equipment in the beans dialog, the active grinder only
  where the live dial-in is genuinely what is being edited. The shared control
  takes the grinder identity as a host-supplied input alongside the value and its
  writer. Notation makes this load-bearing: stepping a compound `"3+2"` with
  plain-numeric rules produces wrongly *formatted* output, not just a wrong
  increment.
- **Existing inconsistencies are fixed as part of the consolidation**: RPM "unset"
  normalises to `0` everywhere; keyboard avoidance is owned once by the picker
  (the `ChangeBeansDialog` RPM registration gap dissolves along with the inline
  fields); history-derived candidates are available on all four QML sites; the
  RPM gate uses one function (`grinderRpmCapable(brand, model)`) on every
  surface, called with that surface's own grinder rather than a stored package
  flag. **Not** a change of scope — all four sites are already context-correct
  today (see design D7).
- Two new icons: `keyboard.svg` (derivable from `hide-keyboard.svg` by dropping
  its dismiss chevron) and a picker-wheel glyph, both in the current 2px line-art
  style so `redraw-icon-set` sweeps them up later.

## Capabilities

### New Capabilities

- `grind-value-entry`: the cross-cutting contract for accepting a grind or RPM
  value on any surface — the shared control and its two presentations, the
  wheel/keyboard toggle and its affordance, the accelerator-over-text principle
  (accept anything, re-seed rather than snap), the automatic text-mode fallback,
  the web `<datalist>` equivalent, and the uniform unset/capability/suggestion
  semantics that the four QML sites and three web forms all inherit.

### Modified Capabilities

- `layout-brew-widgets`: the `grindQuickSelect` requirement currently specifies
  the picker as pick-only ("picking a row … SHALL write only that half"). It gains
  the keyboard mode, the text-mode fallback, and loses the empty-state message.
  The pill's own appearance and write-through behaviour are unchanged.

**Checked and deliberately NOT modified.** `grind-rpm-pairing` was listed here in
an earlier draft; on reading it in full, nothing in it is contradicted. Its
ShotServer scenario requires that the forms "include an RPM input alongside the
grind field" — an `<input list>` is still an RPM input — and the RPM unset
representation it never specifies. Likewise `change-beans-dialog`,
`recipe-wizard`, `post-shot-review-layout`, `brew-settings-equipment`,
`shotserver-bags` and `shotserver-recipes` state *that* each surface has a grind
field, never *which control* it is, so the new cross-cutting capability supersedes
their implementation without a delta. Writing partial MODIFIED blocks for these
would risk losing detail at archive time for no requirement change.

## Impact

**QML** — `qml/components/GrindPickerDialog.qml` (keyboard mode, own
`HideKeyboardButton`, keyboard-avoidance while text mode is active in a centred
modal); `qml/components/layout/items/GrindQuickSelectItem.qml` (logic extracted);
new shared `GrindField` + row-source components (both require `CMakeLists.txt`
`qt_add_qml_module` registration); `BrewDialog.qml`, `PostShotReviewPage.qml`,
`ChangeBeansDialog.qml`, `RecipeWizardPage.qml`.

**C++/web** — `src/network/shotserver_shots.cpp`, `_bags.cpp`, `_recipes.cpp` plus
a shared datalist helper in the common ShotServer style/JS layer. No storage or
schema change: grind is already `QString` and RPM already integral in all six
storage layers (`settings_dye.h`, `shothistory_types.h`, `shotprojection.h`,
`coffeebagstorage.h`, `recipestorage.h`, `shotcomparisonmodel.h`), which is what
makes a single shared control possible.

**Resources** — two new SVGs in `resources/icons/`.

**Not changed** — `GrinderAliases` (`parseGrinderSetting` already accepts plain
numerics on compound grinders; `formatGrinderSetting` already carries an
over-range position, e.g. `"3+150"` → `"4+50"` on a 100-position Mignon, so no new
normalisation is needed) and `stepGrinderSetting`.

**Accessibility** — the toggle needs `Accessible.role`/`name`/`onPressAction` with
a name reflecting its destination; the two text fields need
`Accessible.EditableText`. Per project rule, pre-existing violations in every file
touched get fixed rather than deferred.

**Docs** — the wiki manual's grind/dial-in coverage needs updating for the new
keyboard affordance (tracked as a task, not an afterthought).

## Open Questions

- **Step size is per-grinder, not per-beverage.** `grindStepForGrinder(model)`
  takes only the model, while the MCP `grinderContext` block is beverage-scoped
  (`beverageType: "espresso"`). A user whose espresso step is 0.25 and who types a
  filter setting far coarser will get a 0.25 re-seed there too, which is probably
  too fine to be useful. Options: make the estimator beverage-aware, or widen the
  step when a typed value lands far outside observed history. Deferred pending a
  decision — it does not block the rest of the change.
- Whether desktop should additionally support type-to-jump on a focused wheel
  (additive to the icon; does nothing on a tablet).
