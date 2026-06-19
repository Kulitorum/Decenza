# Change: Add Basket Settings

**Status: OBSOLETE — superseded by `add-basket-equipment` (2026-06-19).** This
proposal framed the basket as the home for *dose*. That framing predates the
equipment-packages work; the basket is now modeled as an equipment component
(identity, for reproducibility + AI dialing), and dose ownership stays
bean/recipe-scoped. The open questions below were resolved there: users do switch
baskets (two baskets = two packages), and the basket lives in the Switch Equipment
dialog. Archived without implementation.

## Why

The "dose" (input coffee weight) currently has no proper home in the settings hierarchy:
- **Profile** has `targetWeight` (yield) but no dose
- **Bean presets** store roaster, coffee, grinder info - but not dose
- `dyeBeanWeight` is just "whatever you weighed in your last shot" (DYE metadata for Visualizer)

The Brew Settings dialog's "Clear" button has no authoritative source to reset dose to.

## The Insight

Dose is fundamentally tied to the **physical basket**, not the beans or profile:
- Same beans, different basket → different dose
- Same profile, different basket → different dose
- Same basket, different beans → same dose (usually)

## What Changes

Introduce "Basket" as a first-class settings concept alongside Profile, Beans, Hot Water, Flush, Steam.

A basket setting would hold:
- Basket name (e.g., "VST 18g", "Stock double", "IMS 20g", "Decent 15g")
- Target dose (e.g., 18.0g)
- Possibly dose tolerance/range?

## Open Questions (for user feedback)

1. **Switching frequency**: Do users actually switch baskets often enough to warrant multiple presets? Or is it usually just one basket per machine?

2. **Ratio ownership**: Should the brew ratio live with the basket (since dose × ratio = yield)? Or keep ratio separate/in profile?

3. **Profile interaction**: How should this interact with profiles that have a fixed target weight vs. ratio-based yields?

4. **UI placement**: Where should basket selection live in the UI?
   - Alongside bean presets on BeanInfoPage?
   - In Brew Settings dialog?
   - Separate settings page?

5. **Minimal viable version**: Could we start with just a single "default dose" setting without full basket presets?

## Impact

- Affected code: `Settings`, `BrewDialog`, `BeanInfoPage`, possibly `MainController`
- New UI needed for basket selection/configuration
- Migration: existing `dyeBeanWeight` values need consideration

## Related Discussion

This came from investigating the Brew Settings "Clear" button behavior - it was resetting dose to hardcoded 18g instead of any configured value, because no configured value exists.
