# Spec Delta

## MODIFIED Requirements

### Requirement: Legacy web interface is never served remotely
Remote Web Access SHALL serve only the current web interface. No part of the legacy HTML interface SHALL be reachable over the public host: no page, asset, bookmark redirect or alias into `/legacy/`. The safe URL, Network card and remote interface SHALL NOT link to or offer the legacy interface. The removed interface SHALL also be unavailable on the LAN. Existing remote route restrictions on APIs remain in force.

#### Scenario: Old bookmark path over the public host
- **WHEN** an admitted browser requests an old legacy page path
- **THEN** the server returns 404 without redirecting into `/legacy/`

#### Scenario: Legacy asset over the public host
- **WHEN** an admitted browser requests an asset used only by the removed pages
- **THEN** the server returns 404

#### Scenario: Legacy-only API over the public host
- **WHEN** an admitted browser calls an API route that only the legacy pages use
- **THEN** the server returns 404

#### Scenario: LAN unchanged
- **WHEN** a LAN browser opens `/legacy/`
- **THEN** the server returns 404
