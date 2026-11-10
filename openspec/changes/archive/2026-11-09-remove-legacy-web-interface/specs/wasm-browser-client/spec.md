# Spec Delta

## MODIFIED Requirements

### Requirement: Root Wasm entry and legacy HTML namespace
The Wasm app SHALL be served at `/` and `/wasm/`. The legacy HTML interface, its namespace and page bookmark aliases SHALL no longer be served. Authentication, device migration and independently used API and file contracts SHALL remain compatible; endpoints used only by the removed interface SHALL be removed.

#### Scenario: Open the root entry
- **WHEN** a user opens `/` or `/wasm/`
- **THEN** the same Wasm application opens

#### Scenario: Open the legacy index bookmark
- **WHEN** a user opens `/index.html` or `/legacy/`
- **THEN** the server returns 404 without redirecting to the removed interface

#### Scenario: Open a legacy HTML bookmark
- **WHEN** a user opens an old HTML page bookmark such as `/beans`, `/recipes`, `/equipment` or `/shot/<id>`
- **THEN** the server returns 404 without redirecting to the removed interface

#### Scenario: Preserve API and file endpoints
- **WHEN** an existing client calls an independently used API or file endpoint
- **THEN** its existing contract remains available independently of the removed pages

### Requirement: Wasm workflow parity
Each workflow exposed by the Wasm interface SHALL preserve the fields, actions, searches, imports, exports, errors, and recovery paths recorded for that workflow in the web inventory. Shared native/Wasm screens SHALL expose consistent domain behavior. A feature absent from the native screen SHALL remain reachable in the Wasm interface when that workflow is included.

#### Scenario: Web-specific editor features
- **WHEN** a layout or theme workflow is implemented in Wasm
- **THEN** its existing browser-only editing and preview features remain available
- **AND** Wasm parity is not accepted solely because the native screen has been reproduced

#### Scenario: Round-trip a file
- **WHEN** a user exports data and imports it through the Wasm interface
- **THEN** the browser offers the download and file-selection workflows
- **AND** the host applies the same validation and persistence rules as before


### Requirement: Stable navigation and external contracts
`/` and `/wasm/` SHALL open the same app. Browser Back, Forward, and reload SHALL preserve internal navigation and draft guards. Independently used REST, MCP, MQTT, relay and file-transfer contracts SHALL remain compatible.

#### Scenario: Open an existing saved shot link
- **WHEN** a client requests the legacy shot JSON or profile download for a saved shot
- **THEN** the server returns 404

#### Scenario: Leave Home with browser Back
- **WHEN** the user presses browser Back from Wasm Home and a previous website exists
- **THEN** the browser returns to that website
- **AND** Decenza's own Back action at Home remains a no-op

#### Scenario: Reload a Wasm deep link
- **WHEN** a user reloads a supported deep link inside `/wasm/`
- **THEN** the Wasm interface restores that route and its record/query context
- **AND** file and API requests continue to return their existing responses


#### Scenario: Existing restock link retains context
- **WHEN** a user follows an old `/beans?restock=<bag>&recipe=<id>` page link
- **THEN** the server returns 404 without redirecting to the removed interface

### Requirement: Recoverable delivery and rollout
The Wasm interface SHALL report loading progress and actionable loading or compatibility failures. Authentication and independently used API and file routes SHALL remain available independently of the Wasm runtime.

#### Scenario: Wasm application fails to load
- **WHEN** a browser cannot initialize the Wasm application
- **THEN** a lightweight page offers the failure reason and retry
- **AND** it does not offer the removed HTML interface

#### Scenario: Wasm bundle is unavailable
- **WHEN** the Wasm bundle is disabled, missing or incompatible
- **THEN** the failure is reported and stored data remains intact
- **AND** authentication and independently used file routes do not require the Wasm runtime
