# Design: Grind Visibility on Shot Review Surfaces

## Context

Two presentation-only edits, both settled during exploration of [#1413](https://github.com/Kulitorum/Decenza/issues/1413):

- **ShotDetailPage.qml** has a metrics `RowLayout` (~lines 554–685) with five cells — Duration, Dose, Output, Ratio, Rating. Each cell is a `ColumnLayout`: caption label (`Tr`, `Theme.captionFont`, `textSecondaryColor`) over a value (`Theme.subtitleFont`). The Output cell additionally shows a baseline-aligned caption suffix (`38.6g (40g)`) — the pattern to reuse for RPM. Grind currently appears only inside the Equipment card further down, rendered by `EquipmentSummary`'s `lastDialLine` in caption font.
- **PostShotReviewPage.qml** has a 3-column `GridLayout` (line ~1575) ordered: equipment card (`id: equipmentCard`, `Layout.columnSpan: 3`) → Grind setting → RPM → Barista → Preset (read-only) → Shot date (read-only). A comment on the card explains grind/RPM are omitted from it because they are "edited in the fields just below".
- The Shot Plan (`ShotPlanText.qml:70`–77) already renders `Grind 5.5 · 340 rpm`; the new metrics cell should match that format for consistency.

## Goals / Non-Goals

**Goals:**
- Grind (and RPM when recorded) readable at a glance at the top of the shot detail page.
- Post-shot review page ordered editable-first: per-shot fields, then read-only metadata, equipment identity card last.
- Keep visual and accessibility patterns identical to the existing metric cells.

**Non-Goals:**
- No data-model, storage, or Settings changes.
- No history-list changes (`ShotHistoryPage` rows already show `Bean (grind)`).
- No dialed-for-profile stamp (#1413 request 3 — future exploration).
- No change to the Shot Plan (RPM shipped in #1396).

## Decisions

1. **Cell position: after Dose.** Grind is a recipe *input* like Dose, not an outcome like Rating — inputs read left, outcomes right: Duration · Dose · Grind · Output · Ratio · Rating.
2. **RPM as caption suffix, Shot Plan format.** Value text is the grind setting in `subtitleFont`; when `shotData.rpm > 0`, append `· 340 rpm` in `captionFont`/`textSecondaryColor`, baseline-aligned via the Output cell's `Row` + `anchors.baseline` pattern. Alternative (rpm on a second line) rejected: taller cell breaks row rhythm.
3. **Hide the cell when `shotData.grinderSetting` is empty** (`visible:` gate on the `ColumnLayout`; row reflows to five metrics). Alternative (show `-` like Rating) rejected: many users never record grind and a permanent dash is noise.
4. **Truncation guard.** Grind settings are free text ("2.5 turns + 3"); cap the value with `Layout.maximumWidth` + `elide: Text.ElideRight` so a long string cannot crowd the other five cells on narrow screens.
5. **Keep the Equipment card's grind/RPM line.** Intentional duplication: the card tells the grinder's full story (brand, burrs, dial-in, basket) and carries the un-elided value when the metrics cell truncates.
6. **Equipment card to the very bottom of the review-page grid** — after Preset and Shot date (user decision), keeping `Layout.columnSpan: 3`. Pure child reorder; no id/binding changes needed. Rewrite the card's stale "fields just below" comment to reflect the new order (fields above, card as trailing hardware context).
7. **Accessibility.** New cell follows the established row pattern: `Accessible.role: Accessible.StaticText` with a combined `Accessible.name` ("Grind: 5.5, 340 rpm"), inner `Text`/`Tr` items `Accessible.ignored: true`. On the review page, reading/focus order follows visual order automatically — no extra work.
8. **i18n.** New key `shotdetail.grind` for the label; reuse existing rpm wording (`equipment.card.lastRpm` format used by ShotPlanText) for the suffix rather than minting a duplicate key.

## Risks / Trade-offs

- [Six cells may still feel tight on phones even with elide] → cell hides entirely for shots without grind; elide cap keeps worst-case bounded; visual check on a narrow window is part of verification.
- [Reordering grid children can silently break neighboring bindings if something references child order] → the card is addressed by `id`, not index; verify with a compile + page open, and grep for `equipmentCard` consumers during implementation.
- [Manual screenshots/wording drift] → Manual.md touch-up is an explicit task, limited to text that describes the affected ordering (wiki is a separate repo; commit there separately).
