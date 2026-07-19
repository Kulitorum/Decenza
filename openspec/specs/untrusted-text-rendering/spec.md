# untrusted-text-rendering Specification

## Purpose
Governs how text the application did not author — user-entered names, community translations, remote metadata — is rendered into markup-bearing surfaces. Two hazards are in scope: colour emoji reaching the platform text renderer, which crashes on macOS, and unescaped external text reaching `Text.StyledText` or `RichText`, which parses it as markup. Community translations are fetched from the backend and uploading one is unauthenticated, so translated strings are attacker-influenceable and are treated as untrusted alongside user data. Also requires that omitting the markup opt-in fails visibly rather than silently permitting markup.

## Requirements
### Requirement: Colour emoji do not reach the platform text renderer

Text displayed by the application SHALL NOT cause a colour emoji glyph to be rasterised by the
platform font engine. Emoji SHALL be rendered as bundled image assets instead.

On macOS this is a crash, not a cosmetic issue: a colour glyph reaching the scene graph leads to an
image decode on the render thread that faults, taking the application down.

#### Scenario: Externally-sourced text containing emoji

- **WHEN** the application displays a string obtained from outside the application — release notes,
  a bean name, an AI reply, a community author name — that contains one or more emoji
- **THEN** the emoji render as bundled image assets and the application does not crash

#### Scenario: Release notes containing emoji

- **WHEN** the user opens the update view and the fetched release notes contain emoji
- **THEN** the notes display with the emoji rendered as images, and the application does not crash

#### Scenario: An emoji with no bundled asset

- **WHEN** a string contains an emoji for which no bundled asset exists
- **THEN** the application does not fall back to rendering it with a platform colour font
- **AND** resolution proceeds as specified in the `emoji-asset-resolution` capability, rather than
  emitting an image reference that cannot be resolved

### Requirement: Externally-sourced text is escaped before markup rendering

Text originating outside the application SHALL be escaped before it is passed to a markup-aware
text renderer, so that it cannot introduce markup into the rendered output.

#### Scenario: Untrusted text containing markup characters

- **WHEN** an externally-sourced string containing markup characters is displayed in a
  markup-aware text element
- **THEN** those characters render as literal text rather than being interpreted as markup

#### Scenario: A caller that deliberately supplies markup

- **WHEN** a caller has escaped its own input and deliberately emits markup, such as hyperlinks or
  emphasis
- **THEN** it can opt in to markup being preserved

#### Scenario: Escaping is the default

- **WHEN** a new call site passes text for emoji replacement without stating an escaping preference
- **THEN** the text is escaped

### Requirement: A missing markup opt-in fails visibly

Where a caller genuinely required markup and did not opt in, the result SHALL be visibly wrong
rather than silently altered.

The asymmetry is deliberate: a caller wrongly denied markup shows raw tags on screen and is
reported immediately, whereas a caller wrongly granted markup is invisible until the content
happens to contain markup characters.

#### Scenario: Markup-bearing caller without the opt-in

- **WHEN** a caller emits markup but does not opt in
- **THEN** the markup appears as literal text on screen rather than being silently stripped or
  interpreted
