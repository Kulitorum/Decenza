## MODIFIED Requirements

### Requirement: A shape-derived match SHALL be presented as a derivation, not as an identity

Where a profile resolves by shape rather than by title, any surface that identifies the profile's knowledge
SHALL name the matched entry using the KB's canonical display name and SHALL word it as a derivation, so the
user can tell the relationship was inferred from the profile's structure rather than read from its name. A
profile that resolved by title SHALL be presented exactly as it is today, with no added label.

This requirement governs the **derivation label** — the short attribution shown beside a profile in the
profile list, and beside a shot on the shot review and shot detail pages. It does not govern the dial-in
difference block defined by the profile-dial-in-diff capability, which is shown inside the knowledge entry
itself and is gated on shape equality rather than on resolution origin. A title-resolved profile therefore
still gains no derivation label, while remaining eligible for that block.

#### Scenario: Shape-resolved profile names its base

- **GIVEN** a user profile that resolved by shape to a KB entry
- **WHEN** the profile is shown in the profile list, or a shot taken with it is shown on the shot review or
  shot detail page
- **THEN** the surface SHALL name the matched entry's canonical display name, worded as a derivation

#### Scenario: Title-resolved profile gains no label

- **GIVEN** a profile that resolved through any title step
- **WHEN** it is shown on any of those surfaces
- **THEN** no derivation label SHALL be added

#### Scenario: Title-resolved profile may still show its dial-in differences

- **GIVEN** a profile that resolved through a title step and is the same shape as the bundled profile its
  entry was authored against
- **WHEN** the user opens its knowledge entry
- **THEN** the dial-in difference block SHALL be shown
- **AND** no derivation label SHALL have been added on the list or shot surfaces
