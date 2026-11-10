# steam_pitcher

Steam pitcher presets. `action` is `list`, `add`, `update`, `delete` or `select`.

A preset carries `name`, `durationSec`, `flowMlPerSec`, `temperatureC` (a per-pitcher steam
temperature) and `secPerGramMilk`. `select` makes one active and sends its values to the machine; `add` also selects
what it just added.

## Fields

- `pitcherWeightG` — the saved empty-pitcher weight, used for net-milk capture.
- `secPerGramMilk` — this pitcher's weight-timed steam rate: seconds of steam per gram of
  milk, 0 to 2. Each pitcher has its own, so presets for different fluids or target
  temperatures steam for different times. 0 means uncalibrated: the pitcher steams for
  `durationSec`. `add` without it copies the selected pitcher's rate.
- `calibMilkG` — a legacy per-pitcher reference weight, reported when stored. It no longer
  scales anything.

## Heater off

Every machine has one built-in "Heater off" entry, last in `list`, reporting only `name` and
`disabled`. `select` it to turn the steam heater off. It cannot be added, edited or deleted.

## Indexes

Indexes come from `list` and are positional, so they shift after a `delete`. `update`,
`delete` and `select` all require `index` explicitly — an omitted one is an error, never
index 0. Names must be
unique: recipes snapshot a pitcher BY NAME, so a duplicate is refused rather than silently
dropped.
