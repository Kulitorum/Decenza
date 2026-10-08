# bag

Coffee (and tea) bags. `action` is `list`, `create`, `update` or `select`.

`select` makes a bag active, so new shots record against it. `bagId: 0` clears the selection —
but `bagId` itself is required: omitting it is an error, not a clear.

## kind

`create` takes `kind`: `coffee` (default) or `tea`. It is set once and cannot be changed
afterwards. Several fields are gated on it — `roastLevel`, `grinderSetting` and `rpm` are coffee
only; `teaType`, `garden`, `cultivar`, `flush`, `brewTempC`, `leafGramsPer100Ml` and `steepTime`
are tea only. Sending a gated field for the wrong kind is an error, not a silent drop.

## Dates and freezing

`roastDate`, `frozenDate`, `defrostDate` and `openedDate` are `YYYY-MM-DD`, never in the future;
`''` clears them. Beans are frozen in portions: `frozenDate` says the bag is kept frozen,
`defrostDate` is when the current portion came out, and clearing `frozenDate` clears it.
`openedDate` is when the current portion was first used, and the first shot from it sets it, so
don't write it by hand. A new bag has no portion in use: `defrostDate` and `openedDate` are
update-only. `storageHint` (counter / airtight / vacuum-sealed / fridge) is valid in any freeze
state.

## The yield anchor

A bag holds ONE yield anchor. `yieldG` is an absolute gram target; `yieldRatio` is a multiple of
the dose (2.0 = 1:2, clamped 0.5-100) so the gram target follows the dose actually weighed.
Writing either replaces the other — no separate clear is needed — and sending both keys in one
call is rejected. `0` clears the yield entirely.

## Grind memory

`grinderSetting` and `rpm` are bean-scoped: they are this bag's dial, paired. `inInventory:
false` marks the bag finished and `true` restores it; `list` hides finished bags unless
`includeEmpty` is set. To restock a finished bag (a new bag of the same coffee), `create` one
with its roaster, coffee, roast level and details and the new roast date; don't restore the old
bag, whose dates and shots belong to the earlier bag.

Parsing a photographed bag label is a different tool: `bag_extract_details`.
