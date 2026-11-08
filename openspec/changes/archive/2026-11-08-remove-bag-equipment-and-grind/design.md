# Design

## Context

See the proposal for motivation. Bags currently store an equipment link and a last dial. Selecting a bag applies those values, and live adjustments and shot review can write them back. Recipes have their own grind, but activation falls back to the linked bag for equipment. Equipment packages also remember their last grind. Active selections and the live grind are stored in settings, while equipment and bags live in the shot-history database.

## Goals / Non-Goals

**Goals:** Give all bags the same behavior, preserve the currently active setup on upgrade, remove bag equipment/grind data, and retain existing shot history and other bag data. Keep recipe-free use and manual equipment switching available.

**Non-Goals:** Maintaining separate behavioral generations of bags; preserving removed bag equipment/grind values as history; creating a recipe or equipment for every old bag; rewriting recipe choices or shot records; changing bag dose or yield; requiring equipment on a recipe; removing equipment-package last-dial memory.

## Decisions

### One bag behavior

Remove equipment, grind and RPM from the bag model, storage and editors for all bags, including existing bags during upgrade. Selecting, restoring or restocking any bag changes bean context without selecting equipment or applying a bag grind. Remove bag equipment/grind write-through from live changes, equipment switching, shot saves, review carry-forward and other shared paths. Continue recording grind and equipment on shots and updating recipes and equipment where applicable. Do not create a bag-history archive: shot history already records the grind used. Retain existing dose, yield and other bag information.

Remove the fields from supported bag writes on native, web and MCP surfaces, with explicit handling for older clients: reject a non-empty obsolete field with a useful error rather than silently accepting it; harmless empty legacy fields may be ignored. Do not add a permanent discriminator or per-bag compatibility branches. This replaces the earlier proposal to keep old bag behavior indefinitely, which would have required every surface to maintain two rules.

Equipment package deletion, merging and identity changes currently count or repoint bag equipment references in SQL. Remove those bag references as part of the same schema change; preserve the existing rules for shot, recipe and package-lineage references. Verify delete eligibility, merge, identity enrichment and identity forks both for packages referenced by shots/recipes and for unreferenced packages. The migration itself must not rewrite shot history; existing explicit equipment operations retain their own established semantics.

### Remove bag fields in one database migration

Migration 52 removes `coffee_bags.grinder_setting`, `equipment_id` and `rpm` in one transaction, together with recording notice eligibility and stamping the schema version. It reads the active recipe, equipment and bag ids from settings but never writes settings, and it creates no equipment.

Nothing needs moving first: the active recipe, active equipment, live grind and live RPM all live in settings, and no revised code reads or writes the bag columns. Removing the columns cannot change the active setup. Rejected: seeding a "Current grinder" package, whose settings write cannot share the database transaction, for users who never had equipment memory, only the per-bag recall this change removes.

A failure at any step rolls the transaction back and logs the step and its error. The database stays at version 51 with the old columns, which the revised code ignores, so the app starts normally and retries on the next launch, like every other migration. Earlier migrations that defer themselves keep their existing retry behavior. Settings that cannot be read yield default ids, which mean "no notice", the conservative outcome. A fresh database reaches this migration with no bags and records no notice.

### Explain the change to affected multi-bag users

At the upgrade, evaluate the database being upgraded conservatively. The notice is for users with no active recipe and no active equipment, no equipment package defined in their current inventory, and at least two in-inventory coffee bags with non-empty saved grinds that are not linked to any non-archived recipe. The active bag must be one of those bags. An equipment package defined in inventory excludes the notice even if it is not selected; a recipe linked to a bag excludes that bag even if the recipe is not active. Finished bags, tea bags and bags used by recipes do not count towards the threshold. A saved grind or multiple bags alone is insufficient. These links and equipment inventory are the available evidence; do not require the live grind to equal the bag's saved value or invent a separate provenance-tracking system. When the required state cannot be established, do not show the notice. Record the pending notice in the database, in the same transaction that removes the columns.

At a suitable startup point after the UI is ready, present a one-time dialog explaining that grind settings are no longer recalled when changing bags, that the current grind is retained, and that equipment or recipes can manage settings going forward. Make clear that recipes can retain separate grind settings; an equipment package remembers its last-used grind rather than a separate grind for each bean. Acknowledging deletes the pending row, so a restart does not repeat the notice; the dialog always closes, and a failed delete only means the notice shows again next launch. A pending notice survives interruption until it can be shown. Do not alter historical shots or create recipes in response to the notice. Users outside the eligibility conditions and fresh installs receive no notice.

### Recipes apply their own choices

Remove activation's fallback from recipe equipment to bag equipment. Explicit recipe equipment applies as before; an equipment-less recipe leaves the current equipment selected. Its own grind and RPM still apply under existing rules, even without an equipment selection. Do not infer or copy equipment into existing recipes during upgrade, activation or relinking. Automatic recipe relinking on finish/restock continues without changing recipe grind or equipment.

### Default a new recipe from shot history, never from the bag

For interactive recipe creation, offer the last known non-empty grind for the same bean from shot history as the editable default. Pair RPM with that same shot rather than combining values from different shots; if no match exists, leave the grind blank. Show the shot's profile and equipment when known, without inventing missing context. Similar-bean/roast hints must remain distinguishable from the same-bean default.

Give the latest-same-bean lookup sole responsibility for defaulting grind and RPM across all profiles and equipment. The existing latest-bean-and-profile lookup may still prefill dose, yield and temperature, but must not write grind or RPM, even if it returns after the grind lookup. Apply a result only if it belongs to the current creation request and bean selection and the user has not edited the grind; a bean change invalidates the prior request/default without erasing a user edit. Cover both callback arrival orders, empty results, explicit clears, and edits made while requests are in flight.

Match shots as follows: when both the selected bean and a shot have non-empty canonical bean IDs, they match only if those IDs are equal; conflicting IDs never match on names. When at least one ID is absent, accept a fallback only if both roaster and coffee names are non-empty and match case-insensitively after trimming, and the available identities do not conflict. If those names correspond to multiple known non-empty canonical IDs, treat the fallback as ambiguous and do not use it. Select the most recent qualifying shot with a non-empty grind across the accepted matches by timestamp descending, breaking timestamp ties deterministically by shot ID descending. Return that shot's identity, timestamp, RPM, profile and equipment context when available. A shot without a canonical ID can therefore match a linked bean by unambiguous names, but neither missing nor conflicting identity should be invented. This is an editable default, not proof that the prior grinder or profile is suitable.

Remove silent bag-grind adoption on all creation surfaces. Explicitly supplied grind, including an explicitly empty value, wins. Web and MCP must not silently copy the bag's grind; use a history default only where the creation flow can make its provenance and resulting value clear. Existing recipe edits and relinks do not re-default.

For legacy backups, recover genuinely inherited recipe grinds directly from the source database before bag fields are discarded. Sources with a bag ID use the source bag; sources predating bag IDs resolve the recipe's bean identity against source open bags using the existing migration-29 matching and ordering rules. If source data lacks a usable match, retain an empty recipe grind rather than guessing. Perform the conversion only for newly imported recipes from the legacy inherit era, never for matched destination recipes or current-era recipes with a deliberately empty grind. Preserve the source RPM with its grind. Replace the current post-import backfill that reads destination bag columns; do not recreate bag grind storage. Test pre-bag-ID, bag-ID and current source schemas, matching and missing bags, and merge deduplication. Existing shot records are not modified.

## Risks / Trade-offs

- [Existing inactive bags stop recalling equipment/grind] -> Historical shots remain intact, notify likely affected multi-bag users at startup, document the changed behavior, and support explicit equipment switching and recipes. Eligibility is a proxy rather than a claim to know usage.
- [A failed migration leaves the old columns in place] -> The revised code ignores them, the app runs normally, and each launch retries and logs the failing step.
- [A history grind may not suit a different profile or grinder] -> Present available context, leave it editable, and protect user edits against delayed results.
- [Equipment-less recipes relied on the bag fallback] -> Do not silently migrate them; explain that equipment can be explicitly selected in the recipe.
- [Equipment operations and old imports depend on removed fields] -> Remove bag SQL from equipment lifecycle operations and perform legacy recipe conversion from source data before discarding fields. Existing shot history is preserved. A downgrade may not understand the revised database schema.

## Migration Plan

1. Remove bag equipment/grind/RPM columns in one database migration that also records notice eligibility, and remove the behavior across shared paths and surfaces, including equipment delete/merge/identity SQL; update recipe activation and creation defaults in the same release. Preserve shots, recipe data and other bag data. Run any older recipe conversion before its source bag fields disappear.
2. Test eligibility rows, fresh installs, rollback on a failed step and the retry that follows.
3. Verify startup presentation, acknowledgment and interruption; also verify equipment lifecycle operations, competing history callbacks and identity matches, shot capture/review, manual equipment switches, active setup after upgrade, inactive bags, restock/restore/relink, and mixed-version backup/transfer. Update the manual.
4. Shot history remains unchanged, but removed bag fields cannot be restored by downgrading. Verify downgrade handling and restore from a pre-upgrade backup if reversal is needed.
