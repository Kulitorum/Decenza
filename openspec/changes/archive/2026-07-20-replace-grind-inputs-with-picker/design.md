## Context

`GrindPickerDialog` + the `grindQuickSelect` pill are the app's best grind
control, but they live only on the brew bar. Six other surfaces still take grind
and RPM through bare text inputs. An audit of all seven found the QML sites have
diverged:

| Site | suggestions | RPM unset | keyboard-aware | RPM gate |
|---|---|---|---|---|
| `BrewDialog.qml:1410/1430` | history | `int 0` | yes | `grinderRpmCapable` |
| `PostShotReviewPage.qml:1595/1626` | history | `int 0` | yes | `grinderRpmCapable` |
| `ChangeBeansDialog.qml:1797/1812` | **none** | **`string ""`** | **RPM missing** | `grinderRpmCapable` |
| `RecipeWizardPage.qml:3117/3124` | **none** | raw `.text` | yes | **`pkg.rpmCapable`** |

Plus three ShotServer forms (`shotserver_shots.cpp:1898`, `_bags.cpp:581`,
`_recipes.cpp:606`) using plain `<input>`.

The enabling fact: **grind is `QString` in all six storage layers and RPM is
integral in all of them.** No type reconciliation is needed for a single shared
control. The blocking fact: the wheel generates rows as `current ±5 steps`, so it
has nothing to show when there is no current value — precisely the state of a new
bag or a new recipe.

Constraints carried in from the codebase and from the machine:

- `stepGrinderSetting`'s only guard is `stepped < 0.0`. There is no upper bound
  anywhere, and `positionsPerRev` is explicitly ignored for `NumericWithSuffix`.
- A Niche Zero's dial is stepless and its printed maximum of 50 is not a real
  limit — filter grinds legitimately run past it. Any bounds check would be a
  regression, not a safeguard.
- `GrindPickerDialog` is `modal: true`, centre-anchored at
  `min(scaled(480), parent.height * 0.92)`. Introducing a text input into it
  brings soft-keyboard problems the dialog has never had.
- QML has no test harness in this project; correctness of the presentations is
  established by looking at them.

## Goals / Non-Goals

**Goals:**

- One grind/RPM control shared by all four QML sites, with two presentations.
- Keyboard entry inside the picker, discoverable, with no hidden gesture.
- The picker becomes usable on surfaces that start empty.
- The four sites stop disagreeing about unset, capability, suggestions and
  keyboard registration.
- A web equivalent that improves text entry rather than imitating a wheel.

**Non-Goals:**

- Porting the tumbler to HTML.
- Any change to `GrinderAliases` parse/format/step behaviour.
- Any storage, schema, or REST payload change.
- Beverage-aware step derivation (see Open Questions).
- Changing the brew-bar pill's appearance or write-through path.

## Decisions

### D1. Text is canonical; the wheel is an accelerator over it

The picker never gates what a value can be. A typed value is accepted verbatim,
stored as-is, and the wheels re-seed centred on it.

*Why:* the data model already says this — grind is an opaque `QString`
everywhere, never a number. A wheel is a view over a string, and a view does not
get to veto its model. Concretely, a Niche Zero at 0.25 steps spans 6.75–9.25
around a current value of 8; a filter grind is hundreds of steps away and
unreachable by spinning at any length. Text is not the escape hatch there, it is
the only door.

*Alternative rejected — snap to the nearest step.* This is what `ValueInput`
does (`ValueInput.qml:706`, `Math.round(parsed / roundTo) * roundTo`). On a
stepless grinder it silently destroys the single best reason to type: landing
between the steps history happens to have taught the app. Type `8.13`, get
`8.25`, no feedback.

*Consequence:* re-seeding rebases the grid on the typed value rather than
returning to the old lattice — `8.13` yields `7.63 / 7.88 / 8.13 / 8.38 / 8.63`.
On a stepless grinder this is the honest model; there was never a lattice, only a
habit.

### D2. Reject nothing — including below zero

Nothing is rejected. Negative, off-grid, past-the-printed-maximum, and
unparseable free text are all accepted and stored.

This extends further than first drafted. The existing `stepped < 0.0` row-skip
was initially treated as a keeper. It is not: on a stepless collar whose zero is
a **user-set calibration reference** — the Niche Zero — dialling finer than zero
is a real operation, not an error. Confirmed by the machine's owner as something
they need for fine grinds.

The skip also looks incidental rather than designed. Both parsers already accept
a leading `-`:

```
grinderaliases.h:403    ^(-?\d+)\s*\+\s*(\d+(?:\.\d+)?)$     compound
GrindQuickSelectItem:156  /^-?\d+(\.\d+)?$/                    numeric
```

Negative values were always *readable*; only stepping toward one was blocked, in
two places (`stepGrinderSetting`'s `if (stepped < 0.0)` and the QML numeric
branch's `if (v < 0) return ""`).

Leaving it in would break the D1 re-seed in exactly this case: type `-0.25`,
return to the wheels, and every negative candidate is dropped — the user sees
`0, 0.25, 0.5…` and cannot spin further down. Text would reach a value the wheel
then strands them on, one-directionally.

*Decision:* generate candidates with negatives included for plain-numeric
notation, across a window wide enough to be effectively unbounded
(`grindWindowSteps: 400` half-width; RPM 40). An earlier draft kept the widget's
±5 window and argued "someone at `8` never sees a negative" — user testing
killed both halves of that: ±5 forced closing and reopening the picker over and
over on a long move (Niche `9` → `-1` is 40 steps), and reaching a negative *by
spinning from a positive value* is exactly the operation being performed. The
window is an implementation buffer, not a limit; the semantic limits live in the
stepper (compound floors at 0, letters clamp A–Z). Only ~5 rows are visible at
once, so the wide window has no visual cost. No setting, no grinder-specific
table — the current-value anchor does the work.

*Exempt:* compound grinders. A negative position on a click-indexed grinder is
meaningless, so that skip stays.

*Exemption key (pinned by review):* the exemption follows the grinder's registry
notation (`entry->notation == Compound`), **not** the current value's written
form. A Mignon whose setting is logged as plain `"2.5"` (the form
`tst_settings.cpp:1760` covers) still skips negative candidates — a negative
linear position is meaningless on click-indexed hardware however it is written.
Without this sentence two implementers would ship two behaviours.

*Why:* every case that looked like it needed a rejection is already handled
upstream. `parseGrinderSetting` (`grinderaliases.h:410`) falls through to accept
plain numerics on compound grinders — with a comment noting some Mignon users log
`"0.5"`. `stepGrinderSetting` only re-notates when `current.contains('+')`, so
that numeric form is preserved rather than forced back to `"0+0.5"`.
`formatGrinderSetting` (`:431`) *carries* an over-range position:
`"3+150"` on a 100-position Mignon parses to 450 linear and formats back as
`"4+50"`. The permissive decision is already made and already implemented; the
keyboard path follows it instead of inventing a stricter rule for itself.

*Note for future readers:* an earlier draft of this change proposed rejecting
compound values whose position exceeds `positionsPerRev`. That was wrong — see
the carry above. Recorded so it is not reintroduced.

### D3. Automatic text-mode fallback

The dialog opens in text mode when the grind wheel has nothing useful to show:

```
can the GRIND wheel offer candidates?
├─ yes → wheels; text reachable via the header toggle
└─ no  → open in TEXT MODE (both halves — one toggle, one mode)
         ├─ no grind rows generatable       (new bag / new recipe, no history)
         └─ unparseable value AND no observed history to fall back on
```

An unparseable value *with* observed history stays on the wheels — the history
fallback already handles it, and reject-nothing requires that it does; stating
the trigger unqualified (as an earlier draft did) made two SHALLs contradict.
The RPM half never decides the mode: its rows always generate (seeded from the
neutral anchor when unset), so the grind half is the trigger and both halves
follow it together, matching the single toggle.

A third trigger — "target too far to reach by stepping" — appeared in an earlier
draft and was cut by review: rows are anchored on the current value, so at open
time every parseable value is reachable by construction. The far-target case
(espresso → filter territory) is served by the header toggle, not auto-fallback.

*Consequence:* the `grind.picker.empty` string
("Set a grind value in Brew Settings first…") is **removed**, not reworded. It is
an apology for a dead end that no longer exists.

### D4. One header toggle, icon swaps to indicate destination

A single icon button in the dialog header switches both wheels to both fields and
back. In wheel mode it shows a keyboard glyph; in text mode a picker glyph.

*Why one control for both halves:* the dialog is 280px wide with only a grind
wheel. Two per-column toggles is clutter for something explicitly infrequent. The
two *fields* remain separate, which is what matters — grind takes free text, RPM
takes `Qt.ImhDigitsOnly`.

*Why swap the icon rather than fill it with the accent colour:* a swapped icon
names where you would go; a filled icon only names where you are. Costs one new
glyph and removes the need for the user to infer the toggle's semantics.

*Assets:* `keyboard.svg` falls out of the existing `hide-keyboard.svg` by
dropping its dismiss chevron — same stroke weight, same family, nothing drawn
from scratch. The picker glyph is `list.svg` with a highlight bar. Both in
today's 2px line art so `redraw-icon-set` restyles them with the rest.

*Anti-pattern being corrected:* `ValueInput.qml:812` already has typed entry, on
`onDoubleClicked`, with nothing on screen indicating it. It has presumably never
been found by a user.

### D5. Three layers, split by what varies

```
GrindQuickSelectItem.qml (392 lines today)
├── SOURCE   reads/writes Settings.dye directly          → varies per site
├── LOGIC    stepGrind, grindRows, _rpmRows,             → identical everywhere
│            _observedFallback, step derivation
└── VISUAL   pill: zoneStyle, zoneTextColor,             → brew-bar only
             glass scrim, ratio-width parity
```

Only the middle layer transfers unchanged. The split produces a shared row-source
component plus a `GrindField` taking `presentation: "pill" | "field"`, with the
value and its writer injected by the host rather than read from `Settings.dye`.

*Precedent:* `RatioPresetDialog` already does exactly this decoupling —
`property bool pickOnly` plus `signal ratioPicked(double)`, with the host
deciding what a pick means. `GrindPickerDialog` is already halfway there: it
takes rows as input, emits `grindPicked`/`rpmPicked`, and touches no settings.
The dialog is already reusable; only the item wrapper is not.

*Why two presentations and not four:* each host already owns its own label
arrangement (`FieldRow`'s shared label column, PostShotReview's tiny label above,
RecipeWizard's placeholder-only pair). The control only needs to supply the
control. This is the least-confident decision in the document — see Risks.

*Interaction model of `field` (settled by review — the artifacts previously
admitted two incompatible readings):* `field` is **tap-to-open**, like the pill —
a bordered, field-shaped control that displays the current value and opens the
picker on tap. It is not an inline text input with a picker bolted on; all typing
happens inside the picker's text mode. Three consequences the tasks reflect:

- Host surfaces no longer register grind/RPM inputs with
  `KeyboardAwareContainer` — there is no inline field left to avoid; the picker
  owns keyboard avoidance once (D4 / task 4.6). ChangeBeansDialog's missing RPM
  registration dissolves rather than being fixed.
- Blur-driven commits become commit-driven: PostShotReview's `autosave` fires on
  the picker's Done, not on a field blur a tap-to-open control does not have.
- History suggestions surface as wheel candidates and the observed-history
  fallback, not as a `SuggestionField` dropdown.

### D6. Web gets `<datalist>`, not a wheel

```html
<input id="editGrinderSetting" list="grindOpts">
<datalist id="grindOpts"><option>7.5</option>…</datalist>
```

*Why:* one native element that is simultaneously the quick-select and the
improved text entry. It keeps free text, so `"C4"` and `"3+2"` still work; it
needs no JS widget; it behaves natively on mobile browsers. It is also
functionally the web's `SuggestionField` — the two surfaces converge on one idea
rather than diverging.

*Alternative rejected — `<select>`:* forecloses free text, which is exactly the
property that makes grind a `QString`.

*Alternative rejected — porting the tumbler:* the wheel is a touch-first
affordance; the ShotServer pages are used with a keyboard, where a text input is
already the better control. This is a deliberate divergence from the
keep-the-surfaces-in-sync rule, made explicitly rather than by omission.

*Placement:* `<datalist>` does not exist anywhere in ShotServer today (it is
`<select class="form-input">` throughout), so it is a new primitive and belongs in
the shared style/JS helper layer, not inlined across three files.

*Candidate data path (settled by review; refined during implementation):* the
stepping machinery is C++ (`ShotHistoryStorage::grindStepForGrinder`,
`SettingsDye::stepGrinderSetting`) and is not ported to JavaScript. Candidates
are served by a dedicated `GET /api/grind-candidates?brand=&model=&current=&rpm=`
endpoint, resolved against **that record's own grinder** (the shot's, the bag's
linked package, the recipe's package; D7 applies to the web too), never the
active one. An earlier draft embedded the candidates in the list payloads the
forms already fetch; implementation showed that goes stale — the bag and recipe
dialogs let the user switch equipment mid-edit, so the pages instead re-request
from the endpoint when the selection changes and the candidates follow the
grinder the value will actually be ground on. The shared helper
(`webtemplates/grind_datalist_js.h`) attaches the `<datalist>` to whatever input
it is given, so the two field names (`grinderSetting` vs `grindPinned`) need no
special-casing.

### D7. Context always matters — the grinder belongs to the value, not the app

Every grinder-derived behaviour (step size, suggestions, notation, RPM
capability) resolves against the grinder that **owns the value being edited**:
the shot's grinder in post-shot review, the recipe's selected package in the
wizard, the bag's equipment in the beans dialog, the active grinder only on the
brew bar and brew dialog, where the live dial-in genuinely is the active one.

The shared control therefore takes a **third** host-supplied input alongside the
value and its writer: the grinder identity. Without it every adopting site would
silently fall back to the active grinder.

*Why this is stricter than a preference:* notation is per-grinder. Stepping a
compound `"3+2"` with plain-numeric rules produces a wrongly *formatted* value,
not just a wrong increment. A user reviewing an old 1Zpresso shot while a Niche
is active would get corrupted output, not merely unhelpful candidates.

*Correction to an earlier draft.* The audit reported `RecipeWizardPage` as
reading "a different RPM-capability source" (`!!pkg.rpmCapable` vs
`Settings.dye.grinderRpmCapable()`) and this design initially prescribed
switching it to the latter to match the other sites. **That was wrong and would
have introduced the exact bug this decision guards against** —
`grinderRpmCapable()` with no arguments resolves against the *active* grinder,
so the recipe editor would have ignored the package the wizard just made the user
choose (the equipment window precedes the grind window specifically so the
grinder is known first). Verified: all four sites are already context-correct —

```
BrewDialog           grinderRpmCapable(active brand/model)      live dial-in
PostShotReviewPage   grinderRpmCapable(editGrinderBrand, …)     ← the shot's (:280 from editShotData)
ChangeBeansDialog    grinderRpmCapable(fEquipmentBrand, …)      ← the bag's   (:73)
RecipeWizardPage     !!pkg.rpmCapable                           ← the recipe's (:1028)
```

The real divergence is *mechanism*, not scope: a stored package flag can drift
from what the catalog function reports. The fix is
`grinderRpmCapable(pkg.grinderBrand, pkg.grinderModel)` — one function, called
with each surface's own context — not a change of scope.

### D8. Consistency fixes ride along, not after

The divergences in the Context table are fixed by the consolidation itself: a
shared control owns candidates, unset semantics, the capability *mechanism* and
keyboard avoidance (owned by the picker — D5's tap-to-open model), so those rows
collapse to one.

Note the RPM-gate column of that table is **not** one of them — see D7. Uniform
mechanism, per-context arguments; two surfaces offering different candidates
because they own different grinders is correct, not drift.

### D9. Clearing a value is a first-class commit

Blank is load-bearing on two adoption sites: a recipe with no pinned grind adopts
the linked bag's dial on create (`shotserver_recipes.cpp:~1023` omits blank;
recipe-model's "defaults from the bag, once"), and a bag's grind may simply be
unset. The pill never needed clearing — the live dial-in always has a value —
which is why `applyGrind` ignores empty. `field` hosts cannot inherit that.

*Decision:* committing Done with an emptied grind or RPM field is an **explicit
clear**, and **every host applies it** — grind `""`, RPM `0`.

*Two corrections got us here, both from real use.* The first draft scoped clears
to the `field` presentation and exempted the brew-bar pill "to preserve today's
behaviour". Review then caught that Brew Settings — a `field` host — was ignoring
clears too, regressing the inline RPM field it replaced. Fixing that exposed the
deeper error: the pill exemption was never a decision at all. The old pill had no
text entry, so a clear was simply *not expressible*; "the live dial-in has never
been clearable" described a missing affordance, not an intent. Now that the
picker can express it, honouring it everywhere is both simpler (no host taxonomy)
and correct — the user emptied a field and pressed Done. Confirmed against the
running app: clearing RPM from the pill's picker did nothing, silently.

*Why not a separate change:* they are only visible as bugs when the four sites
are put side by side, which is what this change does. Splitting them out means
re-deriving the audit later. Project rule is that issues found during work get
fixed in that work.

## Risks / Trade-offs

**[One `field` presentation may not cover four layout idioms]** → The claim rests
on reading, not rendering, and there is no QML test harness. Mitigation: build
`field` against `ChangeBeansDialog`'s `FieldRow` first (the most constrained —
shared label column, 20px margins), then check the other three before adopting.
If RecipeWizard's fixed 110px RPM or PostShotReview's `preferredWidth: 1`
equal-share columns fight it, add a second variant rather than distorting the
hosts.

**[Soft keyboard buries a centred modal]** → The dialog is centre-anchored at up
to 92% of parent height; on a phone the keyboard covers the lower half including
Done. It also needs its own `HideKeyboardButton`, since the global one in
`main.qml` sits behind the modal overlay. This is the one part of "add a keyboard
icon" that is more than an afternoon. Mitigation: treat keyboard-avoidance as
part of the dialog work, not a follow-up; verify on a real device.

**[Re-seeding surprises a user who wanted the old grid]** → Typing `8.13` moves
the whole ladder off the quarter-step lattice. Accepted deliberately (D1): the
alternative silently discards what they typed. Mitigation: none needed if the
wheel visibly centres on the typed value, which it does.

**[`<datalist>` styling may not match the app's look]** → It is a new primitive
here and its dropdown is browser-chrome, less styleable than
`<select class="form-input">`. Mitigation: render it in the ShotServer theme
before committing all three forms.

**[Removing `grind.picker.empty` loses a hint]** → The string currently tells the
user where to set a grind. Its replacement is a text field already focused, which
is more direct. Translation key is retired, not repurposed.

**[Extraction regresses the brew-bar pill]** → It is the one grind control users
already rely on. Mitigation: the pill's appearance, zone handling and
write-through path are explicit non-goals; the extraction must be behaviour-
preserving there, and `layout-brew-widgets`' existing scenarios are the check.

## Migration Plan

No data migration — no storage, schema or payload changes. Sequencing is chosen
so the highest-risk unknown is resolved first:

1. `GrindPickerDialog` keyboard mode, fallback rule, keyboard-avoidance, icons —
   self-contained, verifiable through the existing brew-bar entry point.
2. Extract logic + source from `GrindQuickSelectItem`; re-point the brew bar at
   the shared components. Behaviour-preserving; pill unchanged.
3. `field` presentation, proven against `ChangeBeansDialog` first.
4. Remaining three QML sites, each carrying its consistency fix.
5. ShotServer `<datalist>` helper, then the three forms.
6. Wiki manual update.

Rollback is per-site: any site can keep its text field while the others adopt,
since the shared control is additive until a site is re-pointed at it.

## Open Questions

- **Step size is per-grinder, not per-beverage.** `grindStepForGrinder(model)`
  takes only the model, while the MCP `grinderContext` block is beverage-scoped.
  A user whose espresso step is 0.25 who types a much coarser filter setting gets
  a 0.25 re-seed there too — probably too fine to be useful at that end of the
  dial. Options: make the estimator beverage-aware, or widen the step when a typed
  value lands far outside observed history. Does not block this change.
- Whether desktop should also support type-to-jump on a focused wheel — additive
  to the toggle, does nothing on a tablet.
