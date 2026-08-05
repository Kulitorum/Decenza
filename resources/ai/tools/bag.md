# bag

Coffee (and tea) bags. `action` is `list`, `create`, `update` or `select`.

`select` makes a bag active, so new shots record against it. `bagId: 0` clears the selection.

## kind

`create` takes `kind`: `coffee` (default) or `tea`. It is set once and cannot be changed
afterwards. Several fields are gated on it — `roastLevel`, `grinderSetting` and `rpm` are coffee
only; `teaType`, `garden`, `cultivar`, `flush`, `brewTempC`, `leafGramsPer100Ml` and `steepTime`
are tea only. Sending a gated field for the wrong kind is an error, not a silent drop.

## Dates and freezing

`roastDate`, `frozenDate`, `defrostDate` and `openedDate` are all `YYYY-MM-DD`, and `''` clears
them. `openedDate` is when the current portion left airtight storage and is independent of the
freeze dates. `storageHint` (counter / airtight / vacuum-sealed / fridge) is valid in any freeze
state.

## The yield anchor

A bag holds ONE yield anchor. `yieldG` is an absolute gram target; `yieldRatio` is a multiple of
the dose (2.0 = 1:2, clamped 0.5-6.0) so the gram target follows the dose actually weighed.
Writing either replaces the other — no separate clear is needed — and sending both keys in one
call is rejected. `0` clears the yield entirely.

## Grind memory

`grinderSetting` and `rpm` are bean-scoped: they are this bag's dial, paired. `inInventory:
false` marks the bag empty; `list` hides those unless `includeEmpty` is set.

Parsing a photographed bag label is a different tool: `bag_extract_details`.
