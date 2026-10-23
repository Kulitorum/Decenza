## Context

The wizard requests its bean/same-roast grind hint from shot history after entering details. Shot rows already hold an `equipment_id` that identifies a complete immutable package, but the current lookup only filters on bean identity or roast level. Equipment selection can also change while the lookup runs on a background thread, so a result must be tied to the package that originated it.

## Goals / Non-Goals

**Goals:**

- Prefer a full package match, then safely reuse history whose grinder and basket match while puck prep differs.
- Preserve a useful hint for recipes intentionally configured with no package.
- Ensure that an asynchronous reply cannot repaint the hint for an outdated equipment selection.

**Non-Goals:**

- Do not derive or translate numeric dial values between equipment packages.
- Do not change the recipe-owned bag-dial default, recipe activation, or the history source used for dose, yield, and temperature.
- Do not add a data migration; `shots.equipment_id` already contains the necessary identity.

## Decisions

### Use a full-package lookup followed by a grinder-and-basket fallback

The lookup will first accept the selected package ID and add it to both its exact-bean and same-roast query lanes. If those lanes find no hint, it will repeat the same lookup over packages whose grinder and basket identities match the selected package, deliberately ignoring puck-preparation flags. Grinder identity includes brand, model, and burrs; basket identity includes brand and model.

An ID of zero remains an explicit no-equipment case and retains the existing unfiltered behavior. A positive ID has no fallback beyond a matching grinder-and-basket identity: absence of both tiers yields no hint.

Alternative considered: compare only grinder brand/model. Rejected because burrs are part of the grinder's identity and two packages can share a grinder while differing in basket. Puck preparation is intentionally the sole ignored difference in the fallback.

### Re-request and correlate on equipment selection

The wizard will request the hint whenever it enters details and whenever the equipment tile changes. The request and reply will carry the queried package ID; the QML reply guard will require that it still equals the selected package, alongside the existing bean and roast checks.

Alternative considered: invalidate the hint with a timer after selection. Rejected because event correlation provides deterministic behavior without delaying the UI or depending on device speed.

### Cover the selection rule at the storage boundary

Tests will seed multiple real equipment packages with otherwise comparable shots. They will prove full-package priority, the same-grinder-and-basket fallback across a puck-prep change, and rejection of a different grinder or basket. The existing unscoped test remains the compatibility case for deliberate no-equipment recipes.

## Risks / Trade-offs

- **[Sparse package-specific history can remove a previously visible hint]** → The grinder-and-basket fallback recovers useful history across puck-prep changes; an absent hint remains safer than a setting from a different grinder or basket, and the bag's editable dial remains the recipe default.
- **[An equipment package can be renamed or retired]** → Matching uses the stable package ID stored on the shot and recipe, not display fields or inventory status.
- **[Background replies can arrive out of order]** → The package ID is included in the stale-result guard, so an old result is discarded.

## Migration Plan

No schema or data migration is required. The change only narrows reads of existing `shots.equipment_id` data. Rolling back restores the prior equipment-agnostic hint behavior without affecting stored recipes or shots.
