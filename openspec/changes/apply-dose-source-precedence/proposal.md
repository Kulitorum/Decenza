## Why

The dose has three possible owners — the active recipe, the active bag, and the profile — and no
rule saying which wins. `yield-anchor` already defines exactly that ladder for the *yield*
(recipe → bag → profile), and `coffee-bag-model` enforces it explicitly for a bag's yield spec
rather than letting it "emerge from the order in which signals happen to arrive". The dose has no
such ladder. It is the same problem, one field over.

The gap is live. `ProfileManager::loadProfile` applies the profile's recommended dose straight to
the active dose:

```cpp
if (m_currentProfile.hasRecommendedDose() && m_currentProfile.recommendedDose() > 0)
    m_settings->dye()->setDyeBeanWeight(dose);
```

No check for an active recipe or bag. Loading a profile therefore overwrites a recipe's dose with
the profile's — the ladder inverted. The code predates this change (`6293e137`), but it was nearly
unreachable: `has_recommended_dose` shipped false on every built-in and only the advanced editor's
toggle could set it.

`replace-recipe-block-with-recommended-dose` widened that population considerably. The flag is now
set by both recipe editors' Dose controls, the MCP `dose` parameter, the retired block's dose
promotion, and de1app's `profile_grinder_dose_weight` on `.tcl` import. A latent inversion became
an easy one to hit.

A dose belonging to a profile is correct and stays. What is missing is the rule for *picking* one.

## What Changes

- A dose resolution ladder — **recipe → bag → profile** — mirroring `yield-anchor`'s, enforced
  explicitly at every point that resolves a dose rather than inferred from signal ordering.
- **BREAKING (behaviour):** a profile's recommended dose is applied to the active dose only when
  neither a recipe nor a bag supplies one. Loading a profile no longer overwrites a higher-priority
  source.
- Editing the dose in Brew Settings already follows the ladder for its top two rungs — the existing
  write-through (Settings.dye, the active bag, the recipe's `doseG`) is unchanged. The profile does
  NOT join as a write target: the only call that writes it marks the profile modified, and this
  dialog commits on every OK, so a dose nudge would dirty the loaded profile and ask to be saved.
  The ladder governs which source is read; the two rungs that persist already have write targets.
- The startup profile load applies no dose at all. At launch the bag and recipe rows are still
  loading so the ladder cannot be answered, and the live dose is already persisted from whichever
  source won it last session — the same rule the yield already follows on the launch load.
- The MCP dose surface collapses to a single spelling. `dose` sets the profile's dose and enables
  it; `recommended_dose` / `has_recommended_dose` leave the edit surface. This removes the
  conflict rule added by the previous change along with its half-executed outcome — the two
  spellings had different semantics (set-and-enable vs set-only), so resolving a collision toward
  the "canonical" one stored a dose with the recommendation left disabled, which nothing reads.

## Capabilities

### New Capabilities

- `dose-source-precedence`: which source supplies the dose for the next shot, who may write it,
  and what a profile load may and may not overwrite.

### Modified Capabilities

- `coffee-bag-model`: "The bag's dose continues to apply unconditionally, as today" becomes gated
  on no recipe being active, matching the treatment its own paragraph already gives the yield spec.
- `recipe-aware-brew-settings`: the dose write-through is restated as the ladder's top two rungs,
  with the profile explicitly excluded as a write target and the reason recorded.
- `recipe-block-retirement`: the MCP surface requirement narrows to one spelling; the
  competing-spellings scenario is removed with it.

## Impact

- `src/controllers/profilemanager.cpp` — the unconditional `setDyeBeanWeight` on profile load
- `src/core/settings_dye.{h,cpp}` — the ladder resolver and its two caches; the bag's dose write
- `src/controllers/maincontroller.cpp` — push the recipe's dose into the cache; keep it in step with
  the dose stamp
- `src/mcp/mcptools_profiles.cpp` — one dose spelling; the conflict rule and `conflictedFields` go
- Tests owed from the previous change's review, folded in here: `conflictedFields` firing on
  advanced (now moot — the rule is removed), `dose` on an advanced profile, and the ladder itself
- No change to how a dose is *stored* on a profile, bag or recipe — only to which one is picked
