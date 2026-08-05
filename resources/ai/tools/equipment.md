# equipment

An equipment package is a grinder identity plus an optional basket identity, shared by every bag
and shot that references it. `action` is `list`, `select`, `update` or `merge`.

`list` returns each package with `id`, display `name`, grinder `brand`/`model`/`burrs`,
`rpmAdjustable`, `inInventory`, and the last-used grind setting and `rpm`.

`select` sets the ACTIVE BAG's package (or clears it with `0`) and applies that package's last
grind/rpm to the bag, per the dual-memory rule.

## update has reference semantics

An edit applies to every bag and shot referencing the package — it is not a copy. Changing
`grinderBrand`/`grinderModel` re-derives `rpmCapable` from the registry. `update` also creates a
package when given no existing id.

`puckPrep` carries the technique flags (`wdt`, `shaker`, `puckScreen`, `paperFilter`, `rdt`);
flags you pass override, and the ones you omit keep their current value.

Omitting the basket leaves a package grinder-only. Basket edits follow the same package-identity
dedup/fork rules as grinder edits.

## merge

Folds `sourcePackageId` into `targetPackageId`: the source's history moves across and the source
package is deleted. This is the repair for a package that was wrongly forked into two — the
`#1713` symptom, where an edit that only ADDED information created a second grinder identity.
Irreversible, so it confirms first.
