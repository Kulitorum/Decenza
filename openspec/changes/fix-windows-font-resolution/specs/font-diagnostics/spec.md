## ADDED Requirements

### Requirement: Startup font resolution diagnostics

The application SHALL record, at startup, the information needed to determine which font is
rendering the UI and whether its metrics match a known-good reference, so that a font problem on a
remote user's machine can be diagnosed from a submitted debug log alone.

The diagnostics SHALL cover: host font families that could collide with the bundled family
(recorded before registration), the resolved family name and exact-match status (recorded after
registration), and a probe advance width measured from a fixed string at a fixed pixel size.

#### Scenario: Probe metric is comparable across machines

- **WHEN** debug logs from two machines running the same build are compared
- **THEN** the probe advance width can be read from each log and compared directly, so a
  metrics discrepancy is identifiable without screenshots or remote access

#### Scenario: Collision is visible in the log

- **WHEN** the host font database contains a family whose name could be selected in place of the
  bundled family
- **THEN** that family name appears in the log before the registration result, so the collision is
  attributable

#### Scenario: Registration failure remains distinguishable from resolution failure

- **WHEN** the bundled font fails to register
- **THEN** the log distinguishes that case from the case where registration succeeded but the
  resolved family is not the bundled one

### Requirement: Non-default font size overrides are logged

The application SHALL log, at startup, the font size roles the user has changed from their default
value, together with the default they replaced, so that a layout report can be evaluated against
the user's actual text sizes.

Only roles whose effective value differs from the default SHALL be logged. A stored value equal to
the default is not an override and SHALL NOT be logged.

#### Scenario: User has changed some font sizes

- **WHEN** the application starts and one or more font size roles differ from their defaults
- **THEN** the log records each differing role with its current value and its default value

#### Scenario: All font sizes are default

- **WHEN** the application starts and every font size role is at its default value
- **THEN** nothing is logged about font sizes, so the common case adds no noise to the log

#### Scenario: Stored value equal to the default

- **WHEN** a font size role has a stored value that happens to equal its default (for example the
  user moved a slider and returned it)
- **THEN** that role is not reported as an override
