# water_vessel

Hot water vessel presets. `action` is `list`, `add`, `update`, `delete` or `select`.

A preset carries `name`, `volumeMl`, `mode` (`weight` or `volume`), `flowMlPerSec` and
`temperatureC` (a per-vessel hot water temperature). `select` makes one active and sends its
values to the machine; `add` also selects what it just added.

`add` defaults: 200 mL, `weight` mode, 4.0 mL/s, and the current global hot water temperature.

Indexes come from `list` and are positional, so they shift after a `delete`. `update`, `delete` and `select` all require `index` explicitly — an omitted one is an error, never index 0. A name is required
and cannot be blanked by an update: recipes snapshot the vessel BY NAME, so a nameless preset is
one nothing can refer to afterwards.
