# emoji-asset-resolution Specification

## Purpose
Governs how emoji are turned into renderable assets: the complete set ships with the application and resolves locally, with no network access on any path. This is not a progressive enhancement — an emoji that reaches the platform text renderer as a colour glyph crashes the render thread on macOS, so resolution must be unconditional rather than dependent on connectivity, a cache warm-up, or a prior fetch. Also covers what happens to an emoji with no bundled asset, how variation-selector sequences resolve, and how the bundled set stays pinned and reproducible.

## Requirements
### Requirement: Every emoji the application can display ships with it

The application SHALL bundle the complete emoji asset set, so that displaying any emoji requires
no network access at any time.

Emoji rendering is not an enhancement that may degrade: an emoji reaching the platform text
renderer as a colour glyph crashes the application on macOS. Resolution must therefore be local and
unconditional, not dependent on connectivity, a cache warm-up, or a prior successful fetch.

#### Scenario: Any emoji, any connectivity

- **WHEN** text containing an emoji is displayed, with or without a network connection, on a first
  run or a hundredth
- **THEN** the emoji renders from a bundled asset

#### Scenario: No network access for rendering

- **WHEN** the application renders any text containing emoji
- **THEN** it makes no network request to obtain the asset

#### Scenario: User selects an emoji offline

- **WHEN** the user picks any emoji from the application's picker with no network connection
- **THEN** it renders

### Requirement: An emoji with no asset is removed, never left broken

Before emitting an image reference for an emoji, the application SHALL establish that the asset
exists. When it does not, the emoji SHALL be removed from the displayed text.

The application MUST NOT emit an image reference it has not established can be resolved. An
unresolvable reference produces a broken-image artefact, which is worse than either rendering the
emoji or omitting it, and worse than the crash it was avoiding is visible.

#### Scenario: Codepoint outside the bundled set

- **WHEN** text contains an emoji sequence with no bundled asset — a newer Unicode revision than
  the bundled set covers, or a sequence upstream does not provide
- **THEN** the emoji is removed from the displayed text
- **AND** the surrounding text renders normally, with no broken-image artefact

#### Scenario: Resolution is synchronous

- **WHEN** an emoji is resolved
- **THEN** the result is final at the point of first render, and does not change later

### Requirement: Variation-selector sequences resolve to their asset

Emoji whose colour presentation is requested by a trailing variation selector SHALL be rewritten to
image references, and the variation selector SHALL NOT form part of the asset key.

Matching emoji by codepoint range alone misses these: a keycap begins with an ASCII digit, and
`©️` is a Latin-1 codepoint. Both are rendered from the platform colour font solely because of the
trailing selector, which is the crashing path.

#### Scenario: Keycap sequence

- **WHEN** text contains a keycap sequence such as digit + variation selector + combining enclosing
  keycap
- **THEN** the whole sequence resolves to a single image, not a literal digit beside an image of
  the combining character

#### Scenario: Symbol with emoji presentation

- **WHEN** text contains a symbol such as `©`, `®` or `™` followed by a variation selector
- **THEN** it resolves to an image rather than reaching the platform text renderer

#### Scenario: Variation selector is excluded from the key

- **WHEN** an asset key is derived from an emoji sequence containing a variation selector
- **THEN** the selector is omitted from the key

#### Scenario: Stray variation selector after ordinary text

- **WHEN** a variation selector follows a character that has no emoji presentation, such as a
  letter
- **THEN** that character is not converted to an image reference

### Requirement: The bundled set is reproducible and pinned

The emoji asset set SHALL be committed to the repository and generated from a pinned upstream
version, so that a release is reproducible from a clean checkout with no network access.

A build that fetches assets at build time couples release output to a third party's availability
that morning, and an unpinned source lets upstream artwork change with no commit explaining why a
rebuild renders differently.

#### Scenario: Clean checkout with no network

- **WHEN** a release is built from a clean checkout with no network access
- **THEN** the build succeeds and the application contains the complete emoji set

#### Scenario: Upstream publishes a new version

- **WHEN** the upstream emoji project publishes a newer release
- **THEN** the application's rendering is unchanged until the pinned version is deliberately updated

#### Scenario: Refreshing the set

- **WHEN** a maintainer updates the pinned upstream version
- **THEN** a single documented command regenerates the asset set and its resource manifest
