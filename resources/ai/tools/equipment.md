# equipment

An equipment package is a grinder identity plus an optional basket identity, shared by every bag
and shot that references it. `action` is `list`, `create`, `select`, `update` or `merge`.

`create` adds a package from a grinder and/or basket identity, with optional `name` and
`puckPrep`. The same gear already in inventory is returned rather than duplicated, and a name
another package holds is refused. It does not select the package; use `select`.

`list` returns each package with `id`, display `name`, grinder `brand`/`model`/`burrs`,
`rpmAdjustable`, `inInventory`, and the last-used grind setting and `rpm`.

`select` sets the ACTIVE BAG's package and applies that package's last grind/rpm to the bag,
per the dual-memory rule. It requires a real package id — unlike `bag` action=select, there is no
clear-with-zero.

## update has reference semantics

An edit applies to every bag and shot referencing the package — it is not a copy. Changing
`grinderBrand`/`grinderModel` re-derives `rpmAdjustable` from the registry.

`update` always needs an existing `packageId`. An update can still produce a new id through the
copy-on-write FORK: changing a component on a package that already has shots
leaves those shots on the old identity and returns a new `package.id`, while filling in a
component that was EMPTY is enrichment and edits in place.

`puckPrep` carries the technique flags (`wdt`, `shaker`, `puckScreen`, `paperFilter`, `rdt`);
flags you pass override, and the ones you omit keep their current value.

Omitting the basket leaves a package grinder-only. Basket edits follow the same package-identity
dedup/fork rules as grinder edits.

## merge

Folds `sourcePackageId` into `targetPackageId`: the source's history moves across and the source
package is deleted. This is the repair for a package that was wrongly forked into two — the
`#1713` symptom, where an edit that only ADDED information created a second grinder identity.
Irreversible, so it confirms first.
