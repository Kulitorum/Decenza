## ADDED Requirements

### Requirement: Bundled default application font

The application SHALL bundle its own UI font (Roboto, SIL OFL 1.1 licensed, Regular and Bold
weights) as a compiled-in resource and register it at startup, so that text glyph metrics are
determined by the bundled font rather than the host operating system's font.

#### Scenario: Font registered at startup

- **WHEN** the application launches
- **THEN** the bundled font family is added to the font database via
  `QFontDatabase::addApplicationFont` before the QML engine loads
- **AND** if registration fails, the failure is logged and the application continues with the
  platform default font (no crash)

#### Scenario: Font files are compiled into the binary

- **WHEN** the application runs on any platform with no matching font installed on the device
- **THEN** the bundled font is still available because it is embedded in the Qt resource system,
  not read from the host filesystem

### Requirement: Application-wide default family

The application SHALL set the bundled font as the default application font via
`QGuiApplication::setFont`, and QML text roles SHALL inherit this default family unless a specific
element explicitly overrides it.

#### Scenario: Default text inherits the bundled family

- **WHEN** a QML `Text` element specifies a pixel size but no explicit `font.family`
- **THEN** it renders using the bundled font family

#### Scenario: Explicit per-element override still honored

- **WHEN** a QML element explicitly sets a different `font.family` (for example the monospace
  debug fields)
- **THEN** that element renders in the explicitly requested family, not the bundled default

### Requirement: Deterministic metrics across platforms and OS versions

The application SHALL render text covered by the bundled font's glyph set with an advance width and
line height that depend only on the string and pixel size, and MUST NOT vary with the host OS, OEM,
OS version, or any user-selected system font style.

#### Scenario: Same string renders at the same size on different devices

- **WHEN** the same string is rendered at the same pixel size and UI scale on two devices running
  different OS versions or OEM font configurations
- **THEN** the measured advance width and height match (within font-rasterization rounding),
  because both use the bundled font rather than each device's system font

### Requirement: Non-covered scripts fall back safely

Scripts not covered by the bundled font (for example CJK, Arabic, Hebrew, Devanagari, Thai) SHALL
continue to render via the platform's font fallback, and their rendering is not required to be
metric-deterministic.

#### Scenario: CJK text still renders

- **WHEN** the UI displays text in a language whose script the bundled font does not contain
- **THEN** the text renders using the platform fallback font (no missing-glyph boxes)
- **AND** the layout does not clip that text (guaranteed by the text-overflow-tolerance capability)

### Requirement: Font license shipped

The bundled font's license file (SIL OFL 1.1) SHALL be included in the repository alongside the
font files.

#### Scenario: License present

- **WHEN** the font asset is added to the repository
- **THEN** the corresponding license file is committed in the same location
