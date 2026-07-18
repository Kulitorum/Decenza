## ADDED Requirements

### Requirement: Bundled family name is unique to the application

The bundled font SHALL be registered under a family name that no host-installed font is likely to
claim (`Decenza Sans`), so that a font of the same name already present on the host operating
system cannot be selected in place of the bundled one.

Registering under a widely-distributed family name (for example `Roboto`, commonly installed on
Windows by third-party applications) makes family-name lookup ambiguous: the host font and the
bundled font both match, and the resolution is not guaranteed to favour the bundled font.

#### Scenario: Host has a same-purpose font installed

- **WHEN** the host operating system already has a font installed that the bundled font was derived
  from or shares a design with
- **THEN** the bundled font is still selected, because its registered family name does not collide
  with the host font's family name

#### Scenario: Glyph indices and outlines come from the same font

- **WHEN** text containing a ligating pair (for example `fi`) is shaped and rasterized
- **THEN** the glyph index chosen during shaping and the outline drawn during rasterization come
  from the same font file, so the rendered glyph is the intended one

### Requirement: Font resolution is observable at startup

The application SHALL log enough at startup to determine which font is actually in use, not merely
that registration was attempted. Registration returning a valid identifier SHALL NOT be treated as
evidence that the bundled font won family resolution.

#### Scenario: Resolved family is logged

- **WHEN** the application starts
- **THEN** the log records the resolved font family and whether it is an exact match for the
  requested family

#### Scenario: Competing host families are logged

- **WHEN** the application starts and the host font database already contains families whose names
  could collide with the bundled family
- **THEN** those family names are logged before registration, so a name collision is visible in the
  log without access to the machine

## MODIFIED Requirements

### Requirement: Bundled default application font

The application SHALL bundle its own UI font (Roboto, SIL OFL 1.1 licensed, renamed to the
application-specific family `Decenza Sans`, in Light, Regular, Medium and Bold weights) as a
compiled-in resource and register it at startup, so that text glyph metrics are determined by the
bundled font rather than the host operating system's font.

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

### Requirement: Deterministic metrics across platforms and OS versions

The application SHALL render text covered by the bundled font's glyph set with an advance width and
line height that depend only on the string and pixel size, and MUST NOT vary with the host OS, OEM,
OS version, any user-selected system font style, or any font installed on the host that shares a
name or lineage with the bundled font.

#### Scenario: Same string renders at the same size on different devices

- **WHEN** the same string is rendered at the same pixel size and UI scale on two devices running
  different OS versions or OEM font configurations
- **THEN** the measured advance width and height match (within font-rasterization rounding),
  because both use the bundled font rather than each device's system font

#### Scenario: Host-installed same-name font does not change metrics

- **WHEN** one device has a font installed under the same name as the font the bundled family was
  derived from, and another device does not
- **THEN** both devices measure the same advance width for the same string at the same pixel size,
  because neither resolves to the host-installed font
