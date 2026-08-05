# steam_pitcher

Steam pitcher presets. `action` is `list`, `add`, `update`, `delete` or `select`.

A preset carries `name`, `durationSec`, `flowMlPerSec` and `temperatureC` (a per-pitcher steam
temperature). `select` makes one active and sends its values to the machine; `add` also selects
what it just added.

## Fields with history

- `pitcherWeightG` — the saved empty-pitcher weight, used for net-milk capture.
- `calibMilkG` — a legacy per-pitcher reference weight. It no longer scales anything:
  weight-timed steaming uses one global rate (`steamSecondsPerGram` in settings), not
  per-pitcher calibration.

## "Off" presets

`add` with `disabled: true` creates an "Off" preset that turns the steam heater off; the other
fields are ignored. A disabled preset reports only `name` and `disabled`, and CANNOT be edited —
delete it and add a new one.

## Indexes

Indexes come from `list` and are positional, so they shift after a `delete`. Names must be
unique: recipes snapshot a pitcher BY NAME, so a duplicate is refused rather than silently
dropped.
