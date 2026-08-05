# recipe_create

Creates a recipe. Only `name` is always required. `profileTitle` is required unless the recipe
carries a hot-water block with `hasWater` true — that is a profile-less hot-water tea.

Bag link, equipment and every parameter are optional: a recipe works with whatever the user
actually tracks.

## Linking a bag

Pass `bagId` (from the `bag` tool's `list` action) to link a specific bag. Passing only bean
identity fields instead resolves them to that bean's open bag ONCE, at save time.

## Grind lives on the recipe

Omit `grindPinned` to adopt the linked bag's current dial as this recipe's own starting grind —
this is the recommended path. Set it explicitly to pin a different value. An explicitly empty
string stores no grind at all.
