## Why

#1945 changed the `yield-anchor` rule so a ratio carries only within a beverage group (espresso, filter, tea), but the requirement's title still reads "A ratio anchor survives a profile change". A reader skimming the spec gets the old rule.

## What Changes

- Rename the requirement to "A ratio anchor survives a profile change within its beverage group; an absolute one does not". Its body already states the group rule.

## Impact

- Specs: `yield-anchor` (renamed requirement). No code change.
- Not covered: the spec's Purpose paragraph says the same old rule. OpenSpec 1.13 ignores a delta Purpose for a spec that already has one (`specs-apply.js`, "A delta Purpose only seeds a spec that does not exist yet"), so no change can update it.
