## Context

`bean-freshness-followup` (#1510, archived `2026-07-17`) shipped `storageHint` and `openedDate` on `CoffeeBag`. Its data model asked the wrong question about `storageHint` — "is this an alternative way of saying frozen?" — and answered it defensively by hiding the dropdown while the freeze toggle is on and clearing the value to null on save.

The right question is what `storageHint` describes. It is the user's **plan for how the beans are kept once out of the freezer**: forward-looking on a frozen bag, descriptive on a thawed or never-frozen one. That makes it orthogonal to the freezer axis:

| Axis | Fields | Question answered |
|---|---|---|
| Freezer | `frozenDate`, `defrostDate` | Is it in the freezer; when did this portion leave? |
| Container | `storageHint` | How is it kept when *not* in the freezer? |
| Use | `openedDate` | When did this portion start being used at room temperature? |

Orthogonal axes cannot disagree, so there was never a contradiction to defend against. The invariant that does matter — `storageHint` must never assert frozen-ness — is satisfied by the enum simply having no `"frozen"` value, which requires no hiding and no clearing.

The current `bag-freeze-lifecycle` spec states the correct model in prose (line 10: "the two pairs are independent") and then contradicts it in three requirements (66, 78-80, 83). The code faithfully implements the contradiction, which is why this shipped: it was never a deviation from spec.

Constraint: the `storage_hint`/`opened_date` columns already exist on both `coffee_bags` and `shots`, shot snapshots are immutable, and the `bag_update` MCP tool already accepts both fields. This change is confined to QML and spec text.

## Goals / Non-Goals

**Goals:**
- Redefine `storageHint` as the out-of-freezer storage plan, orthogonal to the freezer axis.
- Stop the app destroying user data — the force-clears on save are silent data loss, including of MCP-written values.
- Make the frozen + thawed + hint + `openedDate` state reachable through the UI, as the archived `design.md:28` always intended.
- Resolve the spec's self-contradiction in favour of independence.

**Non-Goals:**
- No new settings or preferences (a "show hint while frozen" toggle would be exactly the wrong fix).
- No migration, no schema change, no shot-snapshot change.
- No change to how the AI reasons about freshness — the shipped `beanFreshness` instruction already treats airtight/vacuum storage as a staling brake that outlives the freezer. This change only lets the field survive to be read.
- No redefinition of `defrostDate` or `openedDate` semantics; they stay distinct per the archived `design.md:28`.
- Not fixing the `isFrozen` naming globally — see the Decisions note on scope.

## Decisions

**`storageHint` is redefined, not merely un-hidden.** The visibility bug is a symptom; the cause is the field's definition. "Describes non-frozen storage only" implies the field is meaningless while frozen, from which hiding and clearing follow logically. Redefining it as the out-of-freezer *plan* makes a hint on a frozen bag not merely tolerable but the most useful case — it is what the bag's contents are destined for. Considered leaving the definition alone and only removing the `visible:` binding — rejected: it would leave the spec asserting the field is meaningless in a state where the UI now offers it, and the next reader would "fix" the inconsistency by restoring the hide.

**Always visible, rather than gating on "currently in the freezer."** The narrower alternative — show the dropdown only once `defrostDate` is set (portion out of the freezer) — was considered and rejected. It preserves the false premise that the hint describes present state, and it makes the field unsettable exactly when a user is most likely to be planning ahead: at the moment they freeze the bag. Under the plan reading there is no state in which the control should disappear, so no gate is correct. This also keeps the control's presence stable, avoiding a widget that vanishes as a side effect of an unrelated toggle.

**The force-clears are fixed regardless of the visibility decision.** `ChangeBeansDialog.qml:593` and `:604` clear `storageHint`/`openedDate` to `""` whenever `fFreeze` is true. This is unconditional data loss on the *save* path, not a display concern: a hint set through `bag_update` (MCP) is silently wiped the next time anyone opens and saves that bag in the dialog — with no user action naming those fields and no feedback that anything was discarded. Under "always visible" the fix is also mandatory: a control the user can set must not be erased on save.

**"Mark Opened" is re-gated to "no portion currently in the freezer", not to "never frozen".** `BagCard.qml:454` uses `!card.isFrozen`, and `isFrozen` (`BagCard.qml:28`) is `frozenDate` presence alone — which stays set forever once frozen. So a thawed bag reads as frozen and never offers the action, making `openedDate` unreachable for exactly the bags the archived `design.md:28` describes. The correct predicate is `frozenDate` set **and** `defrostDate` empty.

**A thawed bag will show both "Thaw" and "Mark Opened" — accepted deliberately.** This is a visible bag-card change. It is coherent: a thawed portion can be re-thawed (a later portion leaving the freezer) *and* opened (leaving airtight storage), and the archived design keeps those events distinct on purpose. The alternative — keeping the card to one action — would require choosing which of two legitimate events the user is forbidden to record. Two buttons on the subset of bags that are frozen-and-thawed is the cheaper cost.

**Scope of the `isFrozen` conflation is held to this feature's call sites.** `isFrozen`/`fFreeze` meaning "has ever been frozen" is the shared root cause, and the name invites the same bug again. But `isFrozen` also drives the card's "Frozen" badge and the freeze toggle's own checked state, where "has ever been frozen" is arguably the intended reading. Renaming or re-deriving it globally would touch behaviour this change has no evidence about. This change fixes the call sites where the predicate is demonstrably wrong and leaves the derivation alone; a note in the spec records the trap.

## Risks / Trade-offs

**A frozen, never-thawed bag can now carry `storageHint = "counter"`, which reads oddly out of context** → Under the plan reading this is correct and intended ("when this comes out, it goes on the counter"), and it cannot mislead the freshness logic: `buildBeanFreshness()` anchors age on `defrostDate`/`openedDate`, never on `storageHint`, so a plan with no thaw date contributes no aging anchor. The label wording in the dialog should read as a plan ("Stored as / When out of the freezer") rather than as present tense.

**Two actions on a thawed bag card may read as clutter on small screens** → Confined to bags that are both frozen and thawed; never-frozen bags still show one action. Verify on the tablet's card width before shipping.

**Users with bags already silently cleared cannot have that data recovered** → Unavoidable; the values are gone. No backfill is possible or attempted. The fix stops future loss only.

**The spec's own history now contains two opposite rationales** (archived `design.md:26` argues for hiding; this design argues against) → Intentional and worth the noise: the archive is a historical record and must not be rewritten. This design supersedes it, and the delta spec is where the operative requirement lives.

## Migration Plan

None required. The columns exist on `coffee_bags` and `shots`; no data shape changes. Existing rows are unaffected — including bags whose `storageHint` was already cleared by the bug, which simply remain null until a user sets one again. Rollback is a straight revert of the QML changes; no data written under the new behaviour is invalid under the old (an unexpected `storageHint` on a frozen bag would just be re-cleared on the next save).

## Open Questions

- Dialog label wording for the dropdown under the plan reading — present-tense "Storage" is now slightly wrong for a frozen bag. Resolve during implementation against the existing translation key `changebeans.form.storageHint`; changing the visible string means a translation-key update.
- Whether the bag card should surface the hint at all (it currently renders only dates). Out of scope here; noted in case the plan reading makes it worth showing later.
