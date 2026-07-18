## ADDED Requirements

### Requirement: Emoji resolve through bundled, then network, then removal

An emoji SHALL be resolved to a displayable image by trying, in order: the bundled asset set, a
cached copy previously fetched from the network, and a fresh network fetch. When none succeeds the
emoji SHALL be removed from the displayed text.

The application MUST NOT emit an image reference it has not established can be resolved. Doing so
produces a broken-image artefact that is worse than either rendering the emoji or omitting it.

#### Scenario: Emoji is bundled

- **WHEN** text contains an emoji whose asset ships with the application
- **THEN** it renders from the bundled asset with no network request

#### Scenario: Emoji was fetched previously

- **WHEN** text contains an emoji that is not bundled but was fetched on an earlier occasion
- **THEN** it renders from the cached copy with no network request

#### Scenario: Emoji is neither bundled nor cached

- **WHEN** text contains an emoji that is neither bundled nor cached, and the network is available
- **THEN** the emoji is fetched, stored for future use, and displayed

#### Scenario: Emoji cannot be obtained

- **WHEN** an emoji is neither bundled nor cached and the fetch does not succeed — offline, the
  asset does not exist upstream, or the request fails
- **THEN** the emoji is removed from the displayed text
- **AND** the surrounding text renders normally

### Requirement: Text updates when a fetched emoji becomes available

Because fetching is asynchronous, text displayed before a fetch completes SHALL be updated once the
asset is available, without user action.

This is the same class of defect as translation staleness: the displayed value depends on state
that changes later, and a binding that does not record a dependency on that state will never
re-evaluate.

#### Scenario: Emoji arrives after the text is displayed

- **WHEN** text containing an unbundled emoji is displayed, and the fetch completes afterwards
- **THEN** the displayed text updates to include the emoji without the user navigating away and
  back

#### Scenario: Fetch fails after the text is displayed

- **WHEN** the fetch does not succeed
- **THEN** the displayed text settles on the stripped form and does not retry indefinitely

### Requirement: Cached emoji persist across restarts

Emoji fetched from the network SHALL be stored so that they are available on subsequent launches
without refetching.

#### Scenario: Restart after fetching

- **WHEN** the application is restarted after having fetched an emoji
- **THEN** that emoji renders without a network request

#### Scenario: Offline after a previous fetch

- **WHEN** the application has no network connection and displays text containing an emoji it
  fetched previously
- **THEN** the emoji renders from the cache

### Requirement: The application's own interface never depends on the network for emoji

No part of the application's own interface SHALL require a network fetch to render. Every emoji
referenced by app-authored content — interface labels, the emoji picker's set, translated strings —
SHALL ship with the application.

The network path exists only for content the project does not control and cannot know at build
time: release notes, bean names, AI replies, community author names, and text the user types.

An app-authored emoji reaching the network is a defect rather than a slow path: it means the build
failed to bundle an asset, and the field symptom is an interface that renders differently depending
on connectivity.

#### Scenario: An emoji used by the app is not yet bundled

- **WHEN** app-authored content references an emoji with no bundled asset
- **THEN** the build obtains and bundles it

#### Scenario: The invariant is checked, not assumed

- **WHEN** the project is built
- **THEN** a check asserts that every emoji referenced by app-authored content has a bundled asset,
  and fails the build if any does not

#### Scenario: A first run with no network

- **WHEN** the application runs for the first time with no network connection
- **THEN** every emoji in its own interface renders, including every emoji offered by the picker

#### Scenario: User selects an emoji while offline

- **WHEN** the user picks an emoji from the application's picker with no network connection
- **THEN** it renders, because the picker's set ships complete

#### Scenario: Uncontrolled content while offline

- **WHEN** externally-sourced text containing an unbundled emoji is displayed with no network
- **THEN** that emoji is stripped, and this is the only case in which stripping occurs

### Requirement: Newly required emoji are committed to the repository

Emoji obtained by the build SHALL be committed to the project repository as tracked assets, so
that a release is reproducible from a clean checkout without network access to the CDN.

An asset fetched at build time but not committed makes the build non-reproducible and silently
couples release output to a third-party service's availability at build time.

#### Scenario: Build discovers a missing emoji

- **WHEN** the build finds that referenced content needs an emoji not currently in the repository
- **THEN** the asset is added to the repository's tracked emoji set, alongside the manifest the
  resource system reads

#### Scenario: Clean checkout with no network

- **WHEN** a release is built from a clean checkout with no access to the CDN
- **THEN** the build succeeds and the resulting application contains every emoji its own interface
  uses

#### Scenario: Release contains the bundled set

- **WHEN** a release artefact is produced
- **THEN** it ships the committed emoji assets rather than resolving them at first run
