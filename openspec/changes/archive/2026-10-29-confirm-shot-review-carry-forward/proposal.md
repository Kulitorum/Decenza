# Proposal

## Why

Shot review silently carries edits to the next shot, and choosing beans can also activate a bag, deactivate the recipe and replace the intended yield with a bag or profile default. Users need to correct a completed shot without changing what they have prepared to brew next.

## What Changes

- Keep shot-review edits on the completed shot until the user explicitly accepts a summarized offer to carry eligible corrections to the next shot.
- Offer only for the most recent shot, when the current next-shot setup still matches its baseline; recheck before offering and applying. The baseline is captured at shot start in the running session, or when review opens after restart. An edit changed back to its baseline value is unchanged.
- On Back, auto-close or stepping away from the most recent shot, show the proposed actual changes and persistent effects when eligible; otherwise leave without an offer. Machine operations and loss of the review session never apply an offer automatically.
- Carry only fields actually corrected during the review. Bean corrections select the corrected bag and, when applicable, relink the active recipe with explicit disclosure; they do not import or write through unrelated bag settings. Show derived target changes from a dose correction under a ratio.
- Preserve shot-only measurements and feedback, and protect newer or concurrently changed next-shot setups.

## Capabilities

### New Capabilities

- `shot-review-carry-forward`: Guarded, user-confirmed transfer of eligible shot corrections into the next-shot setup.

### Modified Capabilities

- `change-beans-dialog`: Shot-context selection edits the shot only; next-shot activation is deferred to the confirmed carry-forward.
- `coffee-bag-model`: Shot-review dose and grind corrections no longer write through automatically.
- `recipe-activation`: An explicitly accepted shot-review bean correction relinks the active recipe without a normal ingredient-swap deactivation.
- `shot-page`: Leaving or stepping away from the latest shot participates in the carry-forward confirmation while retaining autosave.

## Impact

Shared native/Wasm shot-review and bean-picker QML, recipe-context services, next-shot state and recipe/bag storage coordination, navigation/auto-close, tests and the wiki manual. Existing uncommitted yield-anchor comparison and its test are prior implementation work to evaluate and integrate during apply; they are not the complete fix.
