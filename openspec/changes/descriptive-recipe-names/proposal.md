## Why

The wizard's auto-suggested recipe name is built from only the bean and drink-type short label — `"Yirgacheffe Latte"`. Users who make one bean many ways (the explicit pain in [#1548](https://github.com/Kulitorum/Decenza/issues/1548): fredphoesh runs Ethiopian + S-American beans across DFlow + Cremina profiles) get recipes with identical names that can't be told apart at a glance and — crucially — that the newly-shipped recipe search/sort cannot disambiguate. That user's own mental handle for a recipe is *"Crem Yir"* (Cremina + Yirgacheffe), i.e. **profile + bean** — but the profile is exactly what the current name omits, so his search finds nothing. The profile is both the axis users vary most and the token they already type when hunting; putting it in the default name makes recipes distinguishable and makes the existing search actually work.

## What Changes

- The wizard's auto-suggested recipe name gains the **profile**, applied from the first recipe (not only on collision): `bean + drink-type short label + profile`, e.g. `"Yirgacheffe Latte · Cremina"`.
- The profile token is **cleaned** before use: the `D-Flow/` / `A-Flow/` editor-membership title prefix is stripped, and a profile whose trailing word repeats the drink type is de-stuttered (mirroring the existing bean stutter guard) so the name never reads `"… Espresso · Blooming Espresso"`.
- **Residual collisions** (same bean + drink type + profile — rare) append a qualifier from the first differing dial-in axis: yield/ratio, then dose. Never a numeric counter.
- The existing guards are preserved: the suggestion is applied only while the name field is empty or still holds the previous suggestion (never over a user edit), and the type word is dropped when the bean already ends with it.
- Manual (wiki) documentation of the auto-name behaviour is updated to match.

Out of scope (belongs to the broader #1548 findability work): the 5-recipe pill limit, swipe-for-more, and the search/sort UI itself — all untouched here.

## Capabilities

### New Capabilities
- (none)

### Modified Capabilities
- `recipe-wizard`: the "Name auto-suggestion from bean and drink type" requirement changes to include the cleaned profile token and the collision qualifier.

## Impact

- **Code**: `qml/pages/RecipeWizardPage.qml` — `suggestName()` and its call sites; a small profile-title cleanup helper (candidate home: the `DrinkType` QML singleton or a local wizard function). Collision detection needs the set of existing recipe names for the same bean + drink type, available from `MainController.recipeStorage`.
- **Behaviour**: only the auto-suggested string changes. No storage, migration, activation, MCP, or web changes — names remain free-form and uniqueness is still not enforced.
- **Docs**: wiki Manual entry for recipe naming; `docs/CLAUDE_MD/RECIPES.md` `suggestName()` note.
- **Tests**: QML is tested manually in this project (no QML test harness); the naming logic stays in QML and is verified by the manual walk, not an automated test.
