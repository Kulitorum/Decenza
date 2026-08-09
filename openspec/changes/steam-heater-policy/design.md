# Design

## The model

```
 PERMISSION — the only two things that can make the heater warm
   "Keep warm when idle"            → permitted whenever the machine is awake
   "Let the recipe decide" + a milk → permitted from the moment that recipe's
     recipe's shot starting            shot starts, until the machine returns to Idle
   (floor) pressing Steam           → always permitted, until back to Idle

 VETO — any one of these forces cold, whatever the STATE permission says
   effective pitcher is "Heater off"   (standing, or the active recipe's override)
   the transient widget toggle
   "Let the recipe decide" + an active recipe that steams nothing
   the active recipe explicitly names "Heater off"   (ungated — it is a selection)
```

Event-granted permission outranks every veto, and that is deliberate rather than an
oversight: a GHC steam press puts the DE1 into Steam in firmware and the app cannot
refuse it, so resolving "off" there would make every readout a lie about a boiler
that is actively heating. The "Heater off" entry carries no values of its own, so
that session runs on the live settings — the last real pitcher's duration, flow and
temperature — and says so in a toast rather than silently substituting them.

The recipe veto needs the recipe intent to be **three** states, not two: "nothing is
active" and "an espresso is active" pull in opposite directions, and collapsing them
turns **Let the recipe decide** into a heater-off switch for anyone who has simply
not activated a recipe.

The effective pitcher is the active recipe's pitcher whenever the recipe names one, otherwise the standing pitcher — **not** gated on *Let the recipe decide*. The distinction that setting draws is between what the app INFERS from a recipe and what the user explicitly PUT IN it: naming a pitcher (real, or the built-in "Heater off") is a selection and always applies; deducing "this drink has no pitcher, so keep the boiler cold" and "this drink steams, so warm at shot start" are inferences, and those are what the setting governs. The steam target temperature is the effective pitcher's own temperature, falling back to the global steam temperature when it has none.

The four states the two settings produce:

| Keep warm when idle | Let the recipe decide | No recipe active | Recipe active |
|---|---|---|---|
| on | off | warm | warm — the app infers nothing from the recipe |
| on | on | warm | milk → warm · no pitcher → **cold** |
| off | on | cold until Steam | milk → warm **at shot start** · no pitcher → cold |
| off | off | cold until Steam | cold until Steam |

A recipe that explicitly names **Heater off** is cold in every row, including the two where *Let the recipe decide* is off — that is a selection the user made, not something the app inferred.

## Why two settings and not one three-way choice

An earlier draft made this a single three-option control (Always warm / When I start a milk drink / Only when I press Steam). It was wrong, and the manual page written against it could not be reconciled with the behaviour: the options are not mutually exclusive. "Keep the heater standing by" and "let the drink decide" are answers to *different questions*, and a user can legitimately want both — warm by default, but cold while an espresso recipe is active. Forcing them onto one axis is what made today's single boolean unable to express any of it.

The two settings do not conflict, because they are scoped rather than competing: one governs the case where nothing else has an opinion, the other governs whether a recipe is allowed to have one.

## Why the recipe warms at shot start, not at selection

Users **park** a recipe. It is the machine's resting state, restored after making something else so that the next person is not surprised — not a per-drink choice. So an active recipe is a standing configuration and its *selection* carries almost no information about what is about to be made: keying warmth on selection turns "let the recipe decide" into "always warm" for anyone who parks a milk recipe, which is most people.

The shot start is the event that carries information, and the parked recipe is accurate at that moment. Hence: selection can *lift or apply a veto* immediately (the effective pitcher changes), but it never grants permission. Permission for a milk recipe arrives when its shot starts.

This is why the existing `hasMilk` hold is deleted rather than kept: it grants permission from a parked state, which is exactly the signal that carries no information.

## Sleep and wake

Sleeping changes none of the persistent inputs — the two settings, the selected pitcher, the active recipe — so waking restores the pre-sleep heater state by definition, with the auto-load recipe (if one is configured) deciding instead, exactly as any activation would.

What makes this need code rather than nothing: **entering Sleep clears the transient steam-off flag** (`maincontroller.cpp:215`) and nothing re-sends afterwards. So a user who toggled the heater off with the widget, then let the machine sleep, wakes to a DE1 still holding the 0 it was last told while the policy now says warm — the state changed and was never asserted. Re-resolving on Sleep → Idle fixes that case and makes every other case correct by construction rather than by whatever the DE1 happened to retain.

Event-granted permission does not survive sleep: a shot that was abandoned long enough for the machine to sleep is not a reason to wake up warming.

## Permission is state or event

- **State-granted** permission (Keep warm when idle) holds as long as the state does. Returning to Idle does not revoke it.
- **Event-granted** permission (shot start, pressing Steam) is revoked when the machine returns to Idle.

This resolves a live conflict in the current code: `qml/main.qml:3557` fires `sendSteamTemperature(0)` on steaming end whenever `keepSteamHeaterOn` is false, including while a milk recipe is holding the heater on, and the next `sendMachineSettings` re-asserts the hold. Two mechanisms undo each other and whichever fires last wins. Under this model the return-to-idle turn-off applies only to event-granted permission, so there is nothing to race.

## The pitcher row is a selection, not a heater switch

The pitcher row answers "what would I steam with", and it keeps answering that whether or not the heater is warm. A user with **Keep warm when idle** off leaves "Small" selected and the machine sits cold — the pill still reads Small, correctly, because Small is still what they would steam with.

"Heater off" is a **one-way** override on top of that: selecting it applies the veto, and selecting a real pitcher only *removes* the veto. It never grants permission. So on a machine that is cold because nothing permitted it, tapping a pitcher changes nothing about the heater — while on a machine where **Keep warm when idle** is on, tapping a pitcher after "Heater off" warms it again, because the permission was there all along and the veto is gone.

An earlier draft had the row reflect heater *state* — highlighting "Heater off" whenever the heater was cold for any reason. That was rejected: it conflates two different questions, and it forces a pill tap to grant permission (otherwise tapping a real pitcher would appear to do nothing and the highlight would snap back). Granting permission from a pill tap in turn makes **Keep warm when idle** meaningless, because selecting any real pitcher would then be the same thing as switching it on.

**How the user knows the heater is off is the readout, not the row.** Every surface showing the steam temperature says so. This must be driven by the resolved target, never by `DE1Device.steamTemperature`, which is the *measured* boiler temperature: a heater turned off five minutes ago still reads 130 °C on its way down, so the number cannot distinguish a hot boiler from a cooling one.

## Steaming while "Heater off" is selected

The firmware enters Steam state on a GHC press regardless of what we command — all we ever set is `TargetSteamTemp`, which is already 0 — so this case cannot be prevented, only handled. It uses the live steam settings (duration, flow, temperature), which hold the last real pitcher's values because selecting a pitcher writes them there. The built-in entry carries no values of its own.

Nothing hot comes out, so the temperature is irrelevant and the duration only decides how long the disappointment lasts. The fix is therefore feedback, not parameter design: say that the heater is off.

## Selecting a recipe can turn the heater on

Not by granting permission — by removing a veto. With **Keep warm when idle** on and the standing pitcher set to "Heater off", the heater is vetoed and cold. Selecting a recipe whose pitcher is a real one replaces the effective pitcher, the veto lifts, and the permission that was already there takes effect. Deactivating unwinds the override back to the standing "Heater off" and it goes cold again on its own.

## One derivation

A single helper returns `(heaterOn, targetTemperature)` from: the two settings, the effective pitcher, the transient flag, the active recipe, and whether an event-granted permission is live. Every caller uses it — `sendMachineSettings`, `uploadCurrentProfile`, `startSteamHeating`, `turnOffSteamHeater`, the recipe activation path, and the MQTT status string.

`ProfileManager::uploadCurrentProfile()` currently derives the value itself and knows only two of the five inputs. That drift is the reported field bug, and a duplicated derivation is the mechanism by which this whole area became inconsistent, so the design constraint is stronger than "fix the copy": there must be exactly one, and a test asserts no second one appears.

## "Heater off" as a built-in entry

One built-in entry everyone has, displayed with a translated name, not creatable or removable by the user.

**Its identity is an off marker, never its name.** Pitchers have no ids; everything addresses them by name (`maincontroller.cpp:1810`, the wizard's tile matching, `steamPitcherNameTaken`). If the built-in's identity were its displayed name, a recipe saved in one language and activated in another would fail to match, fall into the resurrect path, and silently manufacture an enabled pitcher named "Off" with zero duration, flow and temperature — turning a recipe that used to shut the heater off into one that does nothing. The same break would occur on a backup restored across languages, or a recipe shared between users.

Since exactly one off entry can now exist, the marker is unambiguous, and the recipe steam block stores the marker instead of a name.

**Decided: synthetic, appended LAST, selected by the sentinel index `-1`.** The stored `steam/pitcherPresets` array holds only real pitchers; `steamPitcherPresets()` appends the built-in entry when the list is read, with its name translated at that moment.

The two candidates were judged equivalent by the maintainer, so this is chosen on properties rather than preference:

- The displayed name is never persisted, so it cannot go stale when the app language changes — which matters because a stored name is also an *identity* elsewhere in this system.
- `removeSteamPitcherPreset`, `moveSteamPitcherPreset` and `updateSteamPitcherPreset` all address presets by index into the stored array. An entry that is not in that array cannot be reached by them at all, so protecting it is a property rather than a guard that every future mutator has to remember.
- It is never serialised into a settings backup, so an import cannot duplicate it.

The sentinel is `-1` because `getSteamPitcherPreset()` already returns an empty map for out-of-range indices, so there is an existing seam to answer on, and the stored value never shifts when the user adds or deletes a pitcher.

**Appended last, not first.** Rendering it first was the initial choice, on the grounds that a fixed position is simpler. Implementing it showed the opposite: `selectedSteamPitcher` is an index into the STORED array, so putting the built-in at position 0 would make every displayed index one greater than its stored index and create two index spaces for every caller to confuse. Appending it leaves every stored index addressing the same real preset, and only the tail needs special handling.

That hazard is not hypothetical — the suite caught `steamPitcherPresets().size() - 1`, the idiom for "the preset I just added", now resolving to the built-in entry in four production call sites. `steamPitcherCount()` exists so that iterating or indexing the stored list has an obvious right answer.

## Migration

Three parts, all one-time and idempotent, following the existing constructor-migration pattern in `settings_brew.cpp:72` (keyed on the shape of what is stored, no version stamp).

1. **Settings.** `steam/keepHeaterOn` maps to **Keep warm when idle**. **Let the recipe decide** is **on**, for existing installs as well as fresh ones.
2. **Presets.** Every preset marked `disabled` is removed. `selectedSteamPitcher` is remapped so that surviving presets keep their identity and, critically, a user who was sitting *on* an Off preset lands on the built-in. Without the remap, deleting a row renumbers the survivors and a stored selection of index 1 quietly becomes a different pitcher — turning the heater on at upgrade, which is the exact failure class this change exists to remove.
3. **Recipes.** Any recipe whose steam block names a removed preset is rewritten to the off marker. This spans two stores (QSettings and SQLite), and if the preset half runs alone every affected recipe hits the resurrect path and manufactures junk pitchers — so the recipe pass must be driven from the same migration and be safe to re-run.

**Shot history is not rewritten.** A shot's steam snapshot records what actually happened; retro-rewriting user-set historical rows is out of policy. The consequence is that promoting an old shot can carry a pitcher name that no longer resolves, so that path must tolerate an unresolvable name without manufacturing a preset.

### "Let the recipe decide" defaults on, including for existing users

This changes behaviour at upgrade, knowingly. Nearly every espresso recipe in existence has no pitcher, so a user on `keepSteamHeaterOn: true` with a parked espresso recipe will find the steam heater off after updating where it used to be warm.

Defaulting it off for existing installs was considered and rejected: the behaviour is the point of the change, and a setting that ships off reaches only the users who go looking for it — which for a heater policy is nearly nobody. The mitigations are that the state is one tap to inspect and change, the FAQ answers the exact question it provokes ("why did the heater turn off when I picked an espresso recipe?"), and the release notes call it out. This remains the single riskiest decision in the change; if field reports say otherwise, flipping the default for existing installs is a one-line change in the settings migration.

## What this does not solve

- **The warm-up duration is unmeasured.** "5–9 minutes" is asserted in five places (`maincontroller.cpp:1841`, `:2590`, `maincontroller.h:760`, `RecipeWizardPage.qml:3421`, and the `recipe-activation` spec) with no measurement behind it anywhere, and each citation reads as if sourced from the others. It is the entire justification for warming early at all. The design does not depend on the exact figure — shot start is the best available intent signal regardless — but the figure should stop being repeated as fact.
- **de1app's eco idea is not adopted.** It idles the heater at a lower temperature (136 °C) after a delay rather than turning it off, which would shorten every warm-up at a fraction of the standby cost. Worth revisiting separately; it is a different axis (*what temperature does it idle at*) from the one this change settles (*when is it permitted to be warm*).
