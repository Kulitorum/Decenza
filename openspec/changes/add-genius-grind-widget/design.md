# Design

## Context

`buildGrinderCalibrationBlock` (`src/ai/dialing_blocks.cpp`) is the one place that decides a grind number. It does four things:
1. It reads the package's dialed-in shots into rows, dropping badged and off-target ones before anything else (`:1316-1328`).
2. It assigns each row its batch: same bean, plus the roast date when dated, else 90-day clustering. It marks rows in the current batch (`:1438-1448`).
3. It takes the intercept (`coffeeAnchor`) as the current batch's latest dialed-in shot (`:1544-1562`).
4. It gives each profile either the current batch's median on that profile (`history`) or the intercept moved by the key (`derived`).

`grindSuggestionFor` only builds that block for the bag's latest shot and reads one profile's `rgs`, returning `{}` when there is none. `EquipmentService.requestGrindSuggestion` runs it off the main thread; `grindSuggestionReady` echoes the query, and each listener drops answers for another selection.

Mark's fork (`fredphoesh/Decenza-Descendant`, through `97daea2e3`) has the widget and popup this change takes: `GeniusGrindItem.qml` and `GeniusGrindDialog.qml`, written to this repo's conventions.

The rules for the starting shot come from testing on his database (6,111 shots; DF83V, Mignon, and DF83V with a Panda basket), with each rule's predictions compared to where he settled, in time order:
- **Latest good shot:** best of the metric-only rules on both grinders.
- **Rating-based picks:** within noise on both grinders.
- **Closest to target weight:** worse.
- **A stale fixed reference:** worse.

The 50 threshold, the highest rating winning (so 75+ beats 60), a rating outranking badges, and the most recent winning a tie are product decisions. The rating is the only taste signal the app has.

**How the order was chosen.** An independent replay compared about a dozen orders with Mark's prototype on all six setups (Mark's three, Jeff's tablet's three), every profile switch and every shot, scored against where the user settled, with bag-clustered intervals.
- **Every order that kept the bag's median on a profile lost to the prototype somewhere**, including four age-aware versions. The deciding bag was on Mark's current setup: a D-Flow shot rated 64 at 16 beat the median of 16.5–17, against a settled 15.5. The median averages in shots from before dialing in finished; the latest good shot does not.
- **"Rated first, then the latest good shot" was no worse than the prototype on every setup**, and better on Mark's three (switches: DF83V −0.07, Mignon −0.01, Panda −0.31 steps). It ties the prototype exactly on Jeff's tablet, giving up the median-first version's edge there.
- **The floor of 50:** with it, every answer the rule still gives is unchanged, and it is better on Jeff's tablet (2 of 3 setups). It drops five prototype answers across six setups. Four rested on shots rated 40–43, two of them 3+ steps off; one (rated exactly 40) was right.
- **Caveat:** this order was chosen after looking, from two users and three grinders, and the margins are small. The claim is "at least as good as the prototype on the data we have", not "more accurate".

## Goals / Non-Goals

**Goals:**
- One rule, in one place, so the widget, the wizard and the AI advisor give the same number for the same bag and profile.
- The user always sees what a number came from.

**Non-Goals:**
- Changing the conversion key, anchors or the validated range.
- Changing the existing `grindQuickSelect` widget.
- Moving a grind to a profile with no conversion key at all. A key needs at least one bag pulled on two profiles, or a marked pair; the UGS chart has no per-grinder default to fall back on.

## Decisions

**The starting shot is chosen inside `buildGrinderCalibrationBlock`.**
- **How:**
  1. In the row loop, before the badge and dialed-in `continue`s, keep a side list of shots rated 50+ that are espresso, with a parsable setting, a profile on the UGS chart and a known bean; mark shots rated 1–49 as bad so they never count as the latest good shot.
  2. After the batch pass, mark which of those are in the current batch, using the same membership lambdas as the rows.
  3. The intercept is the top-rated of those, newest on a tie; with none, it is the latest dialed-in current-batch shot, as today.
- **Profile numbers**, in order:
  1. a profile with a current-batch shot rated 50+ gives its highest-rated shot's setting (`rated`);
  2. else a profile with current-batch good shots gives the latest one (`history`), not their median;
  3. else the highest-rated 50+ shot at the same UGS on another profile (`sameUgs`);
  4. else the profile gets the intercept moved by the key (`derived`), only with a published key and inside the validated range.
- A dated bag is its own roast date only; an undated bag is the undated shots within 90 days of its latest one.
- The selection is `selectStartingShots`, one function the AI block, the wizard and the widget all read. The range check and the derived formula are `GrindCalibrationStatus::inRange` and `derivedSetting`, used by the block and the suggestion alike.
- **Alternative rejected:** a separate rated query in `grindSuggestionFor`. It would give the AI one number and the widget another, and it would add a third copy of the bag-membership rule.

**History alone publishes a key with 10 or more mined pairs, whatever their spread.**
- **Why:** moving a grind between profiles from your own history, with no anchors, is the point of UGS. Today the spread gate withholds the key on both of Mark's older grinders, so they get no number at all without a marked pair.
- **Evidence**, scored per held-out bag against keeping the same grind:

  | Pairs | DF83V (old) | Mignon |
  |---|---|---|
  | Same grind, no key | 2.75 | 0.56 |
  | 3, spread ignored | 2.78 | 0.62 |
  | 5, spread ignored | 2.54 | 0.56 |
  | 10, spread ignored | 2.40 | 0.52 |
  | All (99 / 58) | 2.47 | 0.46 |

  Both grinders' histories fail today's gate. With fewer than 10 pairs, an ungated key is no better than the same grind on at least one grinder, so below 10 the existing 3-pair gate stays.
- **A second measure disagrees.** In a time-ordered replay of every profile switch (the independent audit), the numbers this adds on the two older grinders were no worse than keeping the bag's latest grind, but not clearly better (bag-clustered 95% intervals include 0). On the current setup they were much better. So the claim is "no worse than the same grind, and a number where today shows none", not "more accurate".
- The key's sign was positive in every held-out fold of both full histories.

**Coverage of the prototype.** The prototype gives a number in three ways:
- from its starting shot's own setting when the target sits at the same UGS, with no key and no range check;
- moved by the key otherwise;
- from `grindSuggestionFor`.

The rules here cover each, checked by an independent replay of Mark's database (packages 1, 2 and 10) and Jeff's tablet, every profile switch in time order. A first draft missed 15 same-UGS moves on an uncalibrated grinder (e.g. Cremina → Damian's LRv2, both UGS 0). The same-UGS own-setting tier closes them: 0 prototype-only cases on every package, error unchanged.
- **His reference shot** maps to rating the shot.
- **Non-coffee drinks never count** (cleaning, tea and the rest), as the user decided; the one rated cleaning shot in Mark's data is excluded by that rule, not a coverage gap.
- **An unnamed coffee** (a bag with neither a roaster nor a coffee name) gets `noBean`, and the popup asks for the coffee to be named. The bag dialog already refuses to save such a bag, and none exists in Mark's data or Jeff's, so this is a message for a state normal use can't reach.
- **The weaker source** (same UGS) is named as such in the popup, and every rated shot shows its rating, so the user can judge them.
- **Shots rated under 50 are excluded by the user's rule**, like non-coffee drinks: a prototype number built on one is not a gap.

**Rated shots skip the badges, the 15 g floor and target weight, but not the beverage type.** The calibration's per-shot rejection bundles the badge check with the beverage check and the 15 g floor. The rated list therefore applies only the beverage check, the package, a parsable setting and a UGS-placed profile.

**No key still shows a UGS move.** With `notCalibrated` or `outsideRange`, the suggestion reports the UGS difference from the starting shot, and the popup shows "8 UGS coarser than your Cremina at 21.5", the way the UGS calculator does with no key.

**A profile gets the bag's own setting, key or no key.** That setting is the user's own measured grind, not an extrapolation. The grind suggestion therefore gives it even with no key, while the AI block keeps its rule of no number without a key. Without a key the two therefore differ: the widget shows the bag's own setting and the advisor gives a direction. Accepted (2026-11-21): changing the AI block would change its spec and behaviour for a case the widget already answers.

**`GrindCalibrationStatus` carries the result.** It gains `start` (shot id, profile, setting, timestamp, rating, RPM), `ownSettings` per profile (shot, source), the validated window and `rpmCapable`. `grindSuggestionFor` reads them from the status.

**What the suggestion returns.** The `QVariantMap` carries:
- `setting`, `source` (`rated` | `history` | `sameUgs` | `derived`): `history` is the profile's latest good shot, and `sameUgs` the top-rated 50+ shot at the same UGS on another profile;
- `grindStep`, the grinder's history-derived step;
- `startShotId`, `startProfileName` (the shot's own title), `startShotSetting`, `startTimestamp` (epoch seconds; the QML formats local time), `startRating`, and `startRpm` (0 unless the grinder takes an RPM);
- `reason` when there is no number (an empty map when the history cannot be read or the shot has no grinder), and with `notCalibrated` or `outsideRange`, `ugsDelta` and its direction from the starting shot.

**Cut after review (2026-11-21):** a second "latest shot" reading beside the rated one, an earlier-bag tier for undated coffee, `dbError`/`noGrinder` reasons, and an Apply stop when another client changes the bag. Each was correct, and each added rules and review findings for a case few users would see.

The target profile resolves through the profile catalog (title, editor type, then shape), as saved shots do, so a D-Flow profile with its own name stays on the chart. `EquipmentService.requestGrindSuggestion` returns a request id and echoes it as `requestId`: every request runs on its own worker, so callers match their latest id rather than the query, which a slower older reply also matches.

The wizard is the only existing reader. It moves to the `start*` names in the same change, with no aliases left behind. No `.rep` change is needed: the map passes through `grindSuggestionReady` as is.

**Payload fields for the AI.** `coffeeAnchor` gains `enjoyment0to100` when the starting shot is rated, and `timestamp` (ISO 8601), following the MCP conventions. `profiles[].source` gains `"rated"`. These are output fields only, so `McpSurfaceVersion` stays.

**The widget and popup reuse `requestGrindSuggestion`.** Mark's `requestGeniusSuggestion` and `geniusSuggestionReady` are dropped.

**Refresh triggers.** The widget re-requests when any of these changes:
- the loaded profile, active bag or active equipment;
- `grindCalibrationChanged` (anchor marked or cleared);
- `HistoryService.historyRevision`, compared by value, because `historyChanged` also fires on every replica `notified`.

The revision covers pulling, rating, editing and deleting a shot, which `grindCalibrationChanged` does not.

**Apply.**
- Kept from Mark's popup: the sequence (activate bag → load profile → set grind), each step waiting for its completion signal and matched by request id, with no timers.
- Added: on an RPM-capable grinder, the grind write carries the starting shot's `dyeGrinderRpm` too.
- Added: activating a bag reports done before the bag's row lands and moves the preferences revision, so a grind write refused as stale is resent once. The popup cannot close while Apply runs, because a step already sent cannot be called back.

**The dead `AIManager` grind block goes.** `AIManager::requestGrindCalibration`, `requestMarkGrindAnchor`, `requestClearGrindAnchor` and `requestGrindSuggestion` have no callers in `qml/`, `src/` or `tests/`. The same is true of their five signals, apart from the `main.cpp` `connect` forwarding `grindCalibrationChanged`. They duplicate the `EquipmentService` source path this change reworks, so they are deleted with it.

**Dropped from the fork** (proposal, "Not taken"):
- `markBeanReferenceShot` and `genius_bean_references`;
- the `reference`, `fineFresh` and `coarseFresh` anchor sides;
- the anchors-only key;
- the `*` marker.

## Risks / Trade-offs

- [The AI advisor's and the wizard's numbers change: a rated shot can start the estimate, and a profile's own number is its latest good shot, not the median] → Intended, so every surface agrees. Each surface names the shot, so the change is visible.
- [A rated shot from early in a long undated bag starts the estimate after the coffee has moved on] → It is bounded by the 90-day batch window, and shown with its date.
- [An ungated key on another user's sparse or noisy history] → It needs 10 pairs, which both of Mark's grinders clear by a wide margin. Below that the existing gate applies.
- [The evidence is two users' data, and the order was chosen after looking] → The rating rules are presented as following taste and as at least as good as the prototype, not as more accurate; task 3.0 re-runs the replay against the real code.
- [Apply's three steps can stop partway] → Each failure is reported and the later steps are skipped. A bag activated before a failed profile load stays active.

## Migration Plan

None: no schema change. A database from the trial branch may hold an empty `genius_bean_references` table; nothing reads it, and it can be dropped by hand.
