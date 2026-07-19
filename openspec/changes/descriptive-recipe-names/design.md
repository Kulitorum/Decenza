## Context

`RecipeWizardPage.suggestName()` (`qml/pages/RecipeWizardPage.qml:316`) builds the auto-name from two inputs: the bean (`fCoffee` else `fRoaster`) and `DrinkType.shortLabel(fDrinkType)`. It applies the result only while the field is empty or still equals the last suggestion (`_autoName` guard), and drops the type word when the bean already ends with it. It has no knowledge of the profile or of other recipes.

The profile is already in wizard state as `fProfileTitle`. Profile titles carry an editor-membership prefix for editor-generated profiles (`D-Flow/…`, `A-Flow/…`) — see the project note "D-Flow/A-Flow editor membership = title prefix". Existing recipes are reachable via `MainController.recipeStorage`.

The collision case is real and common (issue #1548): one bean brewed many ways, differing mainly by profile. The user hunting for a recipe types the profile ("Crem Yir"), so the profile must be in the name for the shipped search/sort to match.

## Goals / Non-Goals

**Goals:**
- Auto-name includes the cleaned profile from the first recipe: `bean + short type + profile`.
- Names stay clean: strip the `D-Flow/`/`A-Flow/` prefix; de-stutter a profile whose trailing word repeats the type.
- Same-name collisions (same bean+type+profile) get a meaningful dial-in qualifier (yield, then dose), never a counter.
- Preserve every existing guard (no clobbering user edits; existing bean-stutter and short-label rules).

**Non-Goals:**
- No name-uniqueness enforcement at save (names stay free-form).
- No storage/migration/MCP/web/activation change.
- No change to the recipe list pills, swipe, or the search/sort UI (separate #1548 work).
- Renaming existing recipes — this only affects newly-suggested names.

## Decisions

**1. Name shape: `<bean> <shortType> · <cleanProfile>`.** Middle-dot separator before the profile matches the card/summary idiom and reads as a qualifier rather than more of the name. The bean+type prefix and its existing stutter/short-label handling are unchanged; the profile is appended after.

**2. Profile cleanup helper.** A small pure function `cleanProfileForName(title, typeWord)`:
- Strip a leading `D-Flow/` or `A-Flow/` segment (take the text after the first `/` when the title starts with a known editor prefix; leave other titles untouched).
- Trim.
- If the cleaned profile, lowercased, ends with `" " + typeWord` (or equals it), drop that trailing word — the same rule the bean already uses. If dropping it empties the token, append no profile.

Home: co-locate with `suggestName()` in the wizard (it is the only caller and needs `typeWord` already computed there), or add to the `DrinkType` singleton if a second caller appears. Default to the local wizard function to keep scope minimal.

**3. Collision qualifier — query at suggest time.** When the composed name matches an existing non-archived recipe's name for the same bean+type+profile, append the first differing dial-in axis:
- Yield: a ratio renders as "1:2.5"; an absolute/target as "40g".
- Else dose: "20g" (dose only, disambiguated from a weight yield by context; if both a yield and dose would read identically, prefer the yield).

Fetch the existing-name set via `MainController.recipeStorage`. If the storage read is async and not readily available synchronously in the wizard, the collision qualifier degrades gracefully: with no name set available, emit the base `bean + type + profile` name (still a strict improvement over today). The base name is never worse than the current behaviour.

**4. Call-site parity.** `suggestName()` is invoked from the four existing sites (drink/bean/profile/details step changes). Profile-change must now also re-trigger it (a profile switch changes the suggestion), so the profile-selection handler calls `suggestName()` — the `_autoName` guard still protects a user-typed name.

## Risks / Trade-offs

- **Staleness of a dial-in qualifier.** A yield/dose qualifier can go stale if the user later changes that value. Acceptable because (a) it only appears in the rare same-profile collision, (b) it is only ever auto-applied while the field is untouched, and (c) profile — the common differentiator — is stable. Not worth a live-rewrite mechanism.
- **Longer names.** `"Yirgacheffe Latte · Cremina"` is longer than `"Yirgacheffe Latte"`. Cards and the summary hero wrap (never elide the name-bearing lines per `shot-recipe-card`), and the pills already accommodate longer labels; longer-but-searchable is the explicit intent of #1548.
- **Editor-prefix detection.** Stripping keys on the literal `D-Flow/` / `A-Flow/` prefixes. If a future editor family adds a new prefix, the helper needs the new token — a one-line addition, and an unrecognised prefix simply passes through (a slightly longer name, not a wrong one).
- **Async recipe read.** If the existing-name lookup can't be made synchronous, the collision branch is best-effort; the base descriptive name always ships. This keeps the change from depending on a storage-API change.
