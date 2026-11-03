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

## Added water

For fixed water, use `hotWater.volumeMl` and `mode` (`weight` for grams,
`volume` for ml). For recipe-only ratio water, set `amountMode: "ratio"` and
`waterToEspressoRatio`, for example 2.5 for 100 g added water at a 40 g espresso
target. Omit `volumeMl` in ratio mode. The ratio is editable and follows target
yield changes, never measured shot output; standalone vessel presets stay fixed.
