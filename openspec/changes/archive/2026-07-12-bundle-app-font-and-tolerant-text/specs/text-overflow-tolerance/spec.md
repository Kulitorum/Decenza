## ADDED Requirements

### Requirement: No dead elide on rich text

User-visible text SHALL NOT rely on `elide` while using `textFormat: Text.RichText` (or
`Text.StyledText`), because Qt ignores `elide` for non-plain text, causing the text to clip
mid-glyph with no ellipsis. Each such label SHALL instead either wrap, or use plain-text elide, as
appropriate for its container.

#### Scenario: Rich-text label that must stay one line

- **WHEN** a single-line label needs eliding and does not require emoji/rich content
- **THEN** it uses `textFormat: Text.PlainText` with `elide: Text.ElideRight` so it truncates with
  an ellipsis

#### Scenario: Rich-text label that may wrap

- **WHEN** a label requires rich content (for example emoji rendered as inline images) and its
  container can grow vertically
- **THEN** it uses `wrapMode` (which Qt honors for rich text) instead of a non-functional `elide`,
  and the full text is shown across multiple lines

#### Scenario: Equipment card basket line no longer clips

- **WHEN** the Equipment inventory card shows a long basket description
- **THEN** the text is fully visible (wrapped) or ellipsized, never cut mid-word with no ellipsis

### Requirement: StyledText is the default for HTML-ish labels

Read-only `Text` elements that render markup (emoji `<img>`, `<font>`, `<b>`, `<a>`, etc.) SHALL use
`Text.StyledText` (or `Text.AutoText`, which resolves markup to StyledText) rather than
`Text.RichText`. `Text.RichText` SHALL be used only where a `QTextDocument`-only feature (tables,
CSS block formatting) is genuinely required. Inline emoji produced by the shared emoji helper SHALL
carry the StyledText-honored `align="middle"` attribute so they remain vertically centered.

#### Scenario: New HTML-ish label elides correctly

- **WHEN** a read-only label renders emoji/`<font>`/`<b>` markup and needs to truncate
- **THEN** it uses `Text.StyledText` so `elide` functions, instead of `Text.RichText` where it would
  be silently ignored

#### Scenario: Inline emoji stays centered under StyledText

- **WHEN** a StyledText label contains an inline emoji image from the emoji helper
- **THEN** the emoji is vertically centered (via the `align="middle"` attribute), not baseline-dropped

#### Scenario: RichText reserved for document features

- **WHEN** a label needs only inline formatting (bold, color, links, emoji), not tables or CSS blocks
- **THEN** it does not use `Text.RichText`

### Requirement: Wrapping does not overflow fixed-height containers

Where a label is made to wrap, its container SHALL be content-driven in height (or otherwise able
to grow), so the additional lines do not spill outside the container. Labels inside a
fixed-height container SHALL elide instead of wrap.

#### Scenario: Content-sized card grows to fit wrapped text

- **WHEN** a label wraps inside a container whose height derives from its content
- **THEN** the container grows to fit the wrapped lines and nothing is clipped

#### Scenario: Fixed-height row elides

- **WHEN** a label sits in a container with a fixed height that cannot grow
- **THEN** the label elides on a single line rather than wrapping and overflowing

### Requirement: Fixed-size popups tolerate wider text

Popups and dialogs that present multiple text rows SHALL be sized so that wider-than-expected text
(from font-metric variation, fallback fonts, or long translations) cannot overflow the screen —
by being content-driven within a screen-bounded maximum and scrollable when content exceeds that
maximum.

#### Scenario: Search-syntax help dialog stays on screen

- **WHEN** the Shot History search-syntax help dialog renders on a device whose font is wider than
  the design font
- **THEN** the dialog remains within the screen bounds and all rows are reachable (scrolling if
  needed), rather than overflowing off-screen or clipping columns

#### Scenario: Long-translation label does not break the dialog

- **WHEN** a dialog row contains a long translated string
- **THEN** the string wraps or elides within the dialog's bounds without pushing the dialog past
  the screen edge

### Requirement: Tolerance is font-independent

The text-overflow behavior SHALL hold regardless of which font is active (the bundled font or a
platform fallback), so that scripts not covered by the bundled font are also safe from clipping.

#### Scenario: Fallback-font text does not clip

- **WHEN** a label renders in a platform fallback font (for example a CJK script) whose glyphs are
  wider or taller than the bundled font's
- **THEN** the label wraps, elides, or grows its container as specified, and does not clip
