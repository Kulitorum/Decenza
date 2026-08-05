# equipment

An equipment package is a grinder identity plus an optional basket identity, shared by every bag
and shot that references it. `action` is `list`, `select`, `update` or `merge`.

`list` returns each package with `id`, display `name`, grinder `brand`/`model`/`burrs`,
`rpmAdjustable`, `inInventory`, and the last-used grind setting and `rpm`.

`select` sets the ACTIVE BAG's package and applies that package's last grind/rpm to the bag,
per the dual-memory rule. It requires a real package id — unlike `bag` action=select, there is no
clear-with-zero.

## update has reference semantics

An edit applies to every bag and shot referencing the package — it is not a copy. Changing
`grinderBrand`/`grinderModel` re-derives `rpmAdjustable` from the registry.

`update` always needs an existing `packageId`; there is no create-from-nothing path. What looks
like creation is the copy-on-write FORK: changing a component on a package that already has shots
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
