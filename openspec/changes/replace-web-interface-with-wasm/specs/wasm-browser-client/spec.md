## Purpose

Provides the Decenza WebAssembly interface at the root route for remote computers, phones, and tablets, while retaining `/wasm/` as an alias and the existing HTML implementation beneath `/legacy/` during validation.

## ADDED Requirements

### Requirement: Complete application QML coverage
The Wasm interface SHALL cover all native app QML workflows: shell, pages, settings, dialogs, controls, overlays, layout widgets, screensavers and simulator UI, including supporting scripts/assets. Both targets SHALL use the same presentation files with full look, feel and action parity. Only necessary platform adapters/bootstrap may differ. Browser Quit SHALL remain ignored as specified separately.

#### Scenario: A rarely used widget opens a dependent screen
- **WHEN** a user selects any supported saved-layout widget, gesture or custom action in Wasm
- **THEN** its existing shared destination, controls, dialogs and actions are reachable
- **AND** it does not silently do nothing or substitute a limited browser-only page

#### Scenario: A new app QML file is introduced
- **WHEN** application QML or its supporting scripts are added, removed or renamed
- **THEN** the change's file inventory and implementation/validation ownership are reconciled
- **AND** an unassigned file prevents full-parity acceptance

#### Scenario: An included page has an unavailable action
- **WHEN** a QML file compiles in Wasm but one of its actions, dependent dialogs or routes is unavailable
- **THEN** that workflow remains incomplete in the parity inventory
- **AND** build inclusion is reported separately from validated functionality

### Requirement: Explicit accounting for platform dependencies
Host-only producers SHALL remain tracked in the coverage inventory without excluding their user-facing consumers. Browser capability limitations SHALL require an explicit maintainer decision before a feature is omitted or counted as accepted. Inert controls or copied browser replacements SHALL NOT satisfy parity.

#### Scenario: A platform dependency prevents parity
- **WHEN** a native UI capability cannot be supported by the browser through a host or platform adapter
- **THEN** the exact limitation and affected inventory rows are recorded for a maintainer decision
- **AND** the feature is not silently excluded or counted as complete

### Requirement: Root Wasm entry and legacy HTML namespace
The Wasm app SHALL be served at `/` and `/wasm/`. Existing HTML pages SHALL be served below `/legacy/`, and existing non-root page bookmarks SHALL remain usable through redirects or compatible aliases. This change SHALL retain the HTML implementation and preserve API, authentication, upload, download, and file contracts.

#### Scenario: Open the root entry
- **WHEN** a user opens `/`
- **THEN** the host serves the Wasm interface
- **AND** a user opening `/wasm/` reaches the same application

#### Scenario: Open the legacy index bookmark
- **WHEN** a user opens `/index.html`
- **THEN** the host redirects to `/legacy/` and serves the retained HTML shot history

#### Scenario: Open a legacy HTML bookmark
- **WHEN** a user opens an existing non-root HTML page path such as `/beans`, `/recipes`, `/equipment`, or `/shot/<id>`
- **THEN** ShotServer serves the matching `/legacy/...` page, either directly or through a compatible alias, while preserving its identifier and supported query parameters
- **AND** the legacy page retains its previous workflow behavior

#### Scenario: Preserve API and file endpoints
- **WHEN** a Wasm or legacy page requests an existing `/api/...` route or file/download endpoint
- **THEN** ShotServer returns the existing response contract
- **AND** it does not redirect the request to an HTML page or Wasm shell

### Requirement: Remote browser access
The browser interface SHALL load from a reachable Decenza host URL on another machine without installing Decenza on the client. It SHALL obtain machine state and stored data from that host and send user actions to that host. It SHALL work without client Bluetooth or USB access and without a public internet connection when the host is reachable on the local network.

#### Scenario: Remote phone controls a connected host
- **WHEN** a phone on the local network opens the URL of a Decenza tablet connected to the DE1
- **THEN** it displays that host's data and sends supported actions to that host
- **AND** the phone needs no connection to the DE1 or scale and no native Decenza installation

#### Scenario: Internet is unavailable
- **WHEN** the client and host can communicate locally but the internet is unavailable
- **THEN** the interface and local workflows load using assets served by the host
- **AND** internet-dependent integrations report their own unavailability without preventing local use

### Requirement: Trusted transport for remote access
The remote Wasm interface, its APIs, and live updates SHALL use a browser-trusted HTTPS origin. Local development MAY use HTTP on localhost. The interface SHALL rely on normal browser certificate validation.

#### Scenario: Open the remote interface securely
- **WHEN** a remote browser opens the Wasm interface on a configured trusted HTTPS host
- **THEN** the browser loads its assets, API requests, and live updates through that HTTPS origin
- **AND** it validates the host certificate normally

#### Scenario: Run a local development build
- **WHEN** a developer accesses the interface through localhost during development
- **THEN** the local probe may use HTTP without being treated as remote-deployment acceptance

### Requirement: Browser and mobile usability
The interface SHALL support current stable desktop Chrome, Edge, Firefox, and Safari, Android Chrome, and iPhone/iPad Safari. Existing web workflows SHALL remain usable with keyboard/mouse and touch, in phone and tablet portrait and landscape layouts. Controls SHALL expose accessible names, roles, and actions.

#### Scenario: Phone editing with the software keyboard
- **WHEN** a user edits a recipe on a phone in portrait orientation
- **THEN** fields, validation messages, and Save remain reachable while the keyboard is visible
- **AND** rotating the device preserves the edit and leaves navigation usable

#### Scenario: Keyboard and screen reader access
- **WHEN** a user navigates the management workflow using keyboard or a supported screen reader
- **THEN** navigation, fields, actions, and errors are identifiable and operable
- **AND** focus is restored meaningfully after a dialog closes

### Requirement: Wasm workflow parity
Each workflow exposed by the Wasm interface SHALL preserve the fields, actions, searches, imports, exports, errors, and recovery paths recorded for that workflow in the web inventory. Shared native/Wasm screens SHALL expose consistent domain behavior. A feature absent from the native screen SHALL remain reachable in the Wasm interface when that workflow is included. The retained HTML implementation remains independently usable under `/legacy/` and is not required to move to Wasm.

#### Scenario: Web-specific editor features
- **WHEN** a layout or theme workflow is implemented in Wasm
- **THEN** its existing browser-only editing and preview features remain available
- **AND** Wasm parity is not accepted solely because the native screen has been reproduced

#### Scenario: Round-trip a file
- **WHEN** a user exports data and imports it through the Wasm interface
- **THEN** the browser offers the download and file-selection workflows
- **AND** the host applies the same validation and persistence rules as before

### Requirement: Stable navigation and external contracts
HTML bookmarks SHALL remain usable through `/legacy/` or compatible aliases, preserving record and query context. `/` and `/wasm/` SHALL open the same app. Browser Back, Forward, and reload SHALL preserve internal navigation and draft guards. REST, MCP, MQTT, relay, upload, and download contracts SHALL remain compatible.

#### Scenario: Open an existing saved shot link
- **WHEN** a user opens or reloads an existing `/shot/<id>` bookmark
- **THEN** the existing HTML interface opens that shot under `/legacy/shot/<id>`
- **AND** the shot's JSON and profile downloads still return files rather than an application shell

#### Scenario: Leave Home with browser Back
- **WHEN** the user presses browser Back from Wasm Home and a previous website exists
- **THEN** the browser returns to that website
- **AND** Decenza's own Back action at Home remains a no-op

#### Scenario: Reload a Wasm deep link
- **WHEN** a user reloads a supported deep link inside `/wasm/`
- **THEN** the Wasm interface restores that route and its record/query context
- **AND** file and API requests continue to return their existing responses

#### Scenario: Existing restock link retains context
- **WHEN** the user follows a `/beans?restock=<bag>&recipe=<id>` link
- **THEN** the existing HTML interface opens the restock workflow for that bag and recipe
- **AND** the `/legacy/beans` route or compatible bookmark alias preserves both query values

### Requirement: Recoverable delivery and rollout
The Wasm interface SHALL report loading progress and actionable loading or compatibility failures. If the root Wasm entry is unavailable, the HTML implementation SHALL remain accessible at `/legacy/` and stored data SHALL remain available through its existing APIs.

#### Scenario: Wasm application fails to load
- **WHEN** a browser cannot initialize the Wasm application
- **THEN** a lightweight page offers the failure reason and retry
- **AND** the existing HTML interface remains available at `/legacy/`

#### Scenario: Wasm bundle is unavailable
- **WHEN** the Wasm bundle is disabled, missing, or incompatible
- **THEN** users can continue to open the existing HTML pages at `/legacy/` and use their current workflows
- **AND** API, authentication, file, and download routes do not require the Wasm runtime

### Requirement: Compatible application and host versions
The Wasm client and native host SHALL ship together. The browser SHALL use only its serving app origin and require an exact package identity before accepting live state or enabling mutations. Missing identity or required features SHALL fail explicitly, without older-host compatibility. Stale cached assets SHALL require a matching reload or report an actionable error.

#### Scenario: Mismatched or incomplete package
- **WHEN** a session reports a missing or different package identity, or lacks a required packaged feature
- **THEN** the client rejects the session and keeps reads and mutations disabled
- **AND** it reports that the packaged interface must be reloaded

#### Scenario: Old browser tab meets a new host
- **WHEN** an existing tab reconnects after the host updates to an incompatible protocol version
- **THEN** editing and machine commands remain disabled
- **AND** the user can reload a matching client without losing an unsent edit without warning

### Requirement: Host authentication remains effective
The Wasm interface SHALL honor the host's configured web authentication, session expiry, and cross-site request protections. Loading browser assets SHALL NOT expose protected data or reusable integration credentials. New state subscriptions and commands SHALL enforce the same access policy as their HTTP counterparts.

#### Scenario: Session expires during editing
- **WHEN** the host expires the user's authenticated session
- **THEN** protected reads, subscriptions, and mutations cease to be authorized
- **AND** the interface requests authentication and reports unsent changes rather than silently discarding or submitting them

#### Scenario: Unrelated website attempts a connection
- **WHEN** an unrelated origin attempts a protected state subscription or command
- **THEN** the host rejects it under the existing web access policy
