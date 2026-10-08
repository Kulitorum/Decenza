# mcp-server Specification

## Purpose
Decenza's MCP server: protocol version negotiation and the `MCP-Protocol-Version` header, `Origin` validation, structured tool output, resource-link content blocks, tool/resource titles and icons, and JSON Schema 2020-12 input schemas, plus the domain tool surface for scale connection-priority mode and diagnostics, equipment package (grinder/basket/puck-prep) reads and writes, and bag bean-detail fields.

## Requirements

### Requirement: Latest Protocol Version Support

The MCP server SHALL declare `2025-11-25` as its preferred protocol version and
SHALL also accept `2025-06-18`. It SHALL NOT accept `2025-03-26` or
`2024-11-05`, which no observed client requests and for which the protocol's own
conformance suite has no scenarios. During `initialize`, the server SHALL respond
with the client-requested version when it is in the supported set, otherwise
SHALL respond with the preferred version.

The server SHALL NOT advertise a revision it does not serve.

#### Scenario: Client requests current version
- **WHEN** a client sends `initialize` with `protocolVersion: "2025-11-25"`
- **THEN** the server responds with `protocolVersion: "2025-11-25"`

#### Scenario: Client requests prior version
- **WHEN** a client sends `initialize` with `protocolVersion: "2025-06-18"`
- **THEN** the server responds with `protocolVersion: "2025-06-18"` and SHALL serve subsequent requests under that version

#### Scenario: Client requests a dropped version
- **WHEN** a client sends `initialize` with `protocolVersion: "2025-03-26"`
- **THEN** the server responds with `protocolVersion: "2025-11-25"` (its preferred version), the same as for any other version it does not support

#### Scenario: Client requests unsupported version
- **WHEN** a client sends `initialize` with `protocolVersion: "2023-01-01"`
- **THEN** the server responds with `protocolVersion: "2025-11-25"` (its preferred version)

### Requirement: MCP-Protocol-Version Request Header

For every HTTP request other than `initialize`, the server SHALL accept `MCP-Protocol-Version`. A supported version SHALL be served under that version even when it differs from the negotiated one, without re-versioning the session. An unsupported version SHALL get HTTP 400. `2025-03-26` SHALL be treated as an absent header. An absent header SHALL assume the lowest supported revision.

#### Scenario: Header matches negotiated version

- **WHEN** a client POSTs `tools/call` with `MCP-Protocol-Version: 2025-11-25` after negotiating `2025-11-25`
- **THEN** the server processes the request normally

#### Scenario: Header mismatch

- **WHEN** a client POSTs `tools/call` with `MCP-Protocol-Version: 2024-11-05` after negotiating `2025-11-25`
- **THEN** the request is served, and the response carries only the fields the header's version defines

Note the outcome reverses the previous version of this scenario, which required
HTTP 400 here. The name is kept so the change is visible as a reversal rather
than as one scenario disappearing and an unrelated one appearing.

#### Scenario: Header names an unsupported version

- **WHEN** a client POSTs `tools/call` with an `MCP-Protocol-Version` the server does not support
- **THEN** the server returns HTTP 400 naming the unsupported version

#### Scenario: A supported header does not re-version the session

- **WHEN** a client sends one request under a supported header differing from its negotiated version, then a further request with no header
- **THEN** the second request is answered under the originally negotiated version

#### Scenario: Compatibility sentinel in the header

- **WHEN** a client that negotiated a newer revision POSTs with `MCP-Protocol-Version: 2025-03-26`
- **THEN** the request is served under the negotiated version — neither rejected nor answered under the header

#### Scenario: The sentinel does not make the revision negotiable

- **WHEN** a client sends `initialize` with `protocolVersion: "2025-03-26"`
- **THEN** the server still answers with its preferred version, as for any unsupported revision

#### Scenario: Header absent on legacy client

- **WHEN** a client negotiates a supported revision and POSTs subsequent requests without an `MCP-Protocol-Version` header
- **THEN** the server processes the request normally, under the version that session negotiated

### Requirement: Origin Header Validation

The server SHALL validate the `Origin` request header on every `/mcp` HTTP request before any JSON-RPC parsing. Empty or absent `Origin` SHALL be accepted. Non-empty `Origin` SHALL be matched against an allowlist that includes loopback addresses and the host's own LAN IP addresses; non-matching origins SHALL receive HTTP 403. CORS responses SHALL echo the validated origin back via `Access-Control-Allow-Origin` rather than wildcarding.

#### Scenario: Loopback browser request
- **WHEN** a browser at `http://localhost:3000` requests `/mcp` with `Origin: http://localhost:3000`
- **THEN** the server processes the request and responds with `Access-Control-Allow-Origin: http://localhost:3000`

#### Scenario: LAN browser request
- **WHEN** a browser on the same LAN requests `/mcp` with `Origin: http://192.168.1.50:5173` and the server's listener IP is on `192.168.1.0/24`
- **THEN** the server processes the request and echoes the origin

#### Scenario: Foreign origin
- **WHEN** any client requests `/mcp` with `Origin: http://evil.example`
- **THEN** the server returns HTTP 403 without parsing the request body

#### Scenario: CLI client without Origin
- **WHEN** `mcp-remote` POSTs `/mcp` with no `Origin` header
- **THEN** the server processes the request normally

### Requirement: Structured Tool Output

Every successful `tools/call` response SHALL include a `structuredContent` field carrying the tool's result payload as a JSON object, at every negotiable revision. The `content` array with a text content block SHALL also be emitted, because `content` is required on a tool result and the protocol asks for serialized JSON in a text block for backwards compatibility.

#### Scenario: Tool returns structured payload
- **WHEN** a client calls a tool that returns a JSON payload
- **THEN** the response includes both `content[]` (with at least one text block) and `structuredContent` (the same payload as a JSON object)

#### Scenario: Legacy client receives identical text
- **WHEN** a client negotiating the lowest supported revision calls the same tool
- **THEN** the response still includes the text content block

This scenario previously named `2025-03-26`, a revision no longer served. Its
point is unchanged and was never about that revision: the text block is emitted
at every revision because `content` is required on a tool result, not as a
concession to old clients.

### Requirement: Resource Link Content Blocks

Tools that return data backed by an MCP resource SHALL emit a `resource_link` content block referencing the canonical resource URI in addition to any inline payload. List-style tools SHALL emit one `resource_link` per item.

#### Scenario: Shot detail tool emits link
- **WHEN** a client calls `shots_get_detail` for shot `abc123`
- **THEN** the response `content[]` includes a `{"type":"resource_link","uri":"decenza://shots/abc123","title":"Shot abc123",...}` block alongside the inline shot payload

#### Scenario: Profile list tool emits links
- **WHEN** a client calls `profiles_list`
- **THEN** the response `content[]` includes one `resource_link` per profile, each pointing to `decenza://profiles/{filename}`

#### Scenario: Machine state read emits link
- **WHEN** a client calls a machine-state read tool
- **THEN** the response includes a `resource_link` referencing `decenza://machine/state`

### Requirement: Title Field on Tools and Resources

Every tool record returned by `tools/list` and every resource record returned by `resources/list` SHALL include a non-empty `title` field providing a human-readable display name distinct from the programmatic `name`/`uri`.

#### Scenario: Tool listing includes titles
- **WHEN** a client calls `tools/list`
- **THEN** every tool record includes both `name` (programmatic ID) and `title` (human-readable label)

#### Scenario: Resource listing includes titles
- **WHEN** a client calls `resources/list`
- **THEN** every resource record includes both `uri` and `title`

### Requirement: Icons on Tools and Resources

Every resource record returned by `resources/list` SHALL include an `icons` array with at least one entry. Each entry SHALL contain `src` (a `data:image/svg+xml;base64,...` URI), `mimeType: "image/svg+xml"`, and `sizes`, and icon assignment SHALL be driven by resource kind. Tool records returned by `tools/list` SHALL NOT include icons.

#### Scenario: Tool list omits icons
- **WHEN** a client calls `tools/list`
- **THEN** no tool record includes an `icons` key

#### Scenario: Resource list includes icons
- **WHEN** a client calls `resources/list`
- **THEN** every resource record includes an `icons` array with at least one SVG entry

### Requirement: JSON Schema 2020-12 Tool Input Schemas

Every tool's `inputSchema` SHALL be a valid JSON Schema 2020-12 document and SHALL declare `"$schema": "https://json-schema.org/draft/2020-12/schema"`. Use of pre-2020-12 keywords (`definitions`, `dependencies` as a single keyword) is prohibited.

#### Scenario: Tool list schemas declare 2020-12 dialect
- **WHEN** a client calls `tools/list`
- **THEN** every `inputSchema` object includes `"$schema": "https://json-schema.org/draft/2020-12/schema"`

#### Scenario: Schemas validate under 2020-12
- **WHEN** every `inputSchema` is fed to a JSON Schema 2020-12 validator
- **THEN** all schemas pass validation

### Requirement: Set Scale Connection-Priority Mode Tool

The MCP server SHALL expose a `devices_set_scale_priority_mode` tool that sets the persistent backoff policy to `enforce` or `observe`, and MUST require an explicit confirmation argument before applying. The change is queued onto the BLE-manager thread and is eventually consistent: the response MUST NOT assert the persist has completed and MUST state that contract. An unrecognized mode MUST be rejected without changing state.

#### Scenario: Set observe mode
- **WHEN** the tool is called with `mode: "observe"` and `confirmed: true`
- **THEN** the mode change is queued and, once applied on the BLE-manager thread, reads back as `observe`
- **AND** the response states the change was queued (not asserted-persisted) and that HIGH-forcing applies on the next scale reconnect (the current connection is not torn down)

#### Scenario: Set back to enforce
- **WHEN** the tool is called with `mode: "enforce"` and `confirmed: true`
- **THEN** the mode change is queued and, once applied, reads back as `enforce`
- **AND** the prior persisted latch (if any) is honored again on the next reconnect

#### Scenario: Missing confirmation
- **WHEN** the tool is called without `confirmed: true`
- **THEN** no state changes and the response indicates confirmation is required

#### Scenario: Invalid mode value
- **WHEN** the tool is called with a `mode` other than `enforce` or `observe`
- **THEN** the call is rejected and the persisted mode is unchanged

#### Scenario: Observe takes effect on the next connect
- **WHEN** an MCP client sets mode `observe` while a scale is connected
- **THEN** the HIGH-forcing effect applies only on the next scale (re)connect, and the current connection is not torn down

### Requirement: Connection Status Reports Mode And Observe Events

The `devices_connection_status` tool SHALL report the active `backoffMode` and a bounded list of recent observe events. Each observe event MUST carry an ISO-8601 timestamp with UTC offset, the trigger kind as a human-readable string, an event kind (`wouldBackoff` or `recovered`), and the relevant duration with units in the field name (`stallSec` / `gapSec`). The list MUST be bounded (most recent first) and MUST be empty when the mode has never been `observe`.

#### Scenario: Status includes mode
- **WHEN** `devices_connection_status` is called
- **THEN** the response includes `backoffMode` as `"enforce"` or `"observe"`

#### Scenario: Observe events surfaced
- **WHEN** the mode is `observe` and one or more would-back-off and/or recovery events have occurred
- **THEN** the response includes a bounded, most-recent-first list of those events
- **AND** each event has an ISO-8601-with-offset timestamp, a human-readable trigger kind, an event kind of `wouldBackoff` or `recovered`, and a unit-suffixed duration field

#### Scenario: No observe history
- **WHEN** the mode has never been `observe` this run
- **THEN** the recent-observe-events list is empty (and `backoffMode` reports the current mode)

### Requirement: Connection Status Reports Detection Epoch And Diagnostic Build Code

The `devices_connection_status` tool SHALL report the current detection epoch and, when the scale link is latched, the build code that last set the classification as a diagnostic field. The build code MUST be presented as diagnostic ("last classified by build N"), not as the gating mechanism. Field names MUST follow the project MCP data conventions (human-readable, no Unix timestamps; the existing ISO-8601-with-offset latch timestamp is unchanged).

#### Scenario: Epoch always reported
- **WHEN** `devices_connection_status` is called
- **THEN** the scale connection-priority block includes the current detection epoch

#### Scenario: Diagnostic build code on a latched link
- **WHEN** the scale link is latched (skip-HIGH)
- **THEN** the response includes the build code that last set the classification, labeled as diagnostic
- **AND** the response does not present the build code as the rehydration gate

#### Scenario: Reset tool unchanged
- **WHEN** `devices_reset_scale_priority` is called
- **THEN** it clears the latch as before (the in-session escape hatch is unchanged by epoch scoping)

### Requirement: MCP grinder reads SHALL resolve via the equipment package

MCP surfaces reporting grinder identity (the `de1://dialing` resource in `mcpresources.cpp`, `dialing_get_context`, `dialing_get_grinder_calibration`, `ai_advisor_invoke`) SHALL resolve brand, model and burrs through the equipment package: the active bag's package for live snapshots, the shot's `equipment_id` for shot-derived blocks. They SHALL also expose the package `id`, `name`, `rpmAdjustable` and the `rpm` dial-in.

#### Scenario: Dialing resource reports the package
- **WHEN** the `de1://dialing` resource is read
- **THEN** the grinder block SHALL include the resolved brand/model/setting plus `rpm` and `rpmAdjustable`, sourced from the active bag's equipment package

#### Scenario: Data conventions on grinder reads
- **WHEN** a dialing MCP surface reports grinder identity
- **THEN** units are in field names, `kind` is a string enum, and timestamps are ISO-8601

### Requirement: equipment_list tool
The MCP server SHALL provide an `equipment` tool with an `action: "list"` (modeled on the `bag` tool's `list` action) returning equipment packages: `id`, display `name`, grinder `brand`/`model`/`burrs`, `rpmAdjustable`, `inInventory`, and the last-used grind setting and `rpm`.

#### Scenario: Listing packages
- **WHEN** an agent calls `equipment` with `action: "list"`
- **THEN** it SHALL receive the inventory of equipment packages with the fields above

### Requirement: equipment_select tool
The MCP server SHALL provide an `equipment` tool with an `action: "select"` (modeled on the `bag` tool's `select` action) that sets the active bag's equipment package by id (or clears it with 0). Selecting a package SHALL apply that package's last grind setting / rpm to the active bag per the dual-memory rule.

#### Scenario: Selecting a package
- **WHEN** an agent calls `equipment` with `action: "select"` and a valid package id
- **THEN** the active bag's `equipment_id` SHALL be set to that package
- **AND** the active bag's grind setting and rpm SHALL be set to the package's last-dial values

### Requirement: equipment_update tool
The MCP server SHALL provide an `equipment` tool with an `action: "update"` (modeled on the `bag` tool's `update` action) that edits a package's grinder identity (brand/model/burrs) and SHALL support creating a package. On a brand/model change, `rpmCapable` SHALL re-derive from the registry. Edits use reference semantics (apply to all referencing bags/shots).

#### Scenario: Editing a package
- **WHEN** an agent calls `equipment` with `action: "update"` changing a package's grinder model
- **THEN** the package SHALL be updated, `rpmAdjustable` re-derived, and all referencing bags/shots SHALL resolve to the new identity

### Requirement: Equipment MCP tools SHALL read and write the basket identity
The MCP `equipment_*` tools SHALL include the package's basket identity
(`brand`, `model`) in equipment reads, and SHALL accept an optional basket identity
when creating or updating a package, alongside the grinder identity. Omitting the
basket SHALL leave a package grinder-only. Basket edits SHALL flow through the same
package-identity (dedup/fork) rules as grinder edits.

#### Scenario: Equipment read includes the basket
- **WHEN** an MCP equipment read resolves a package that has a basket
- **THEN** the response SHALL include the basket brand and model

#### Scenario: Equipment write sets a basket
- **WHEN** an MCP equipment write supplies a basket identity
- **THEN** the package SHALL gain a `kind="basket"` item subject to the identity rules

### Requirement: MCP basket fields SHALL follow the data conventions
Basket fields exposed over MCP SHALL follow the project MCP conventions: dose range
named with its unit (`doseRangeG`), and `wallProfile` / `relativeFlow` / `precision`
expressed as human-readable strings/booleans rather than numeric codes. Derived
specs SHALL be omitted when unknown (custom basket) rather than zero-filled.

#### Scenario: Basket specs are LLM-legible
- **WHEN** the MCP dialing context emits the basket sub-object
- **THEN** `wallProfile` and `relativeFlow` SHALL be readable strings and the dose range SHALL be unit-suffixed (`doseRangeG`)

### Requirement: Equipment MCP tools SHALL read and write the puck-prep flags
The MCP `equipment_*` tools SHALL include the package's puck-prep flags (and the
derived `distribution`) in equipment reads, and SHALL accept the optional puck-prep
flags when creating or updating a package, alongside the grinder and basket
identity. Omitting them SHALL leave the package without puck prep. Puck-prep edits
SHALL flow through the same package-identity (dedup/fork) rules as grinder/basket edits.

#### Scenario: Equipment read includes puck prep
- **WHEN** an MCP equipment read resolves a package that has puck prep
- **THEN** the response SHALL include the set flags and the derived `distribution`

#### Scenario: Equipment write sets puck prep
- **WHEN** an MCP equipment write supplies one or more puck-prep flags
- **THEN** the package SHALL gain a `kind="puckprep"` item subject to the identity rules

### Requirement: MCP puck-prep fields SHALL follow the data conventions
Puck-prep fields exposed over MCP SHALL follow the project MCP conventions: boolean
flags with self-describing names (`wdt`, `shaker`, `puckScreen`, `paperFilter`,
`rdt`) and a human-readable `distribution` string rather than a numeric code.

#### Scenario: Puck-prep fields are LLM-legible
- **WHEN** the MCP dialing context emits the puck-prep sub-object
- **THEN** the flags SHALL be booleans and `distribution` SHALL be a readable string

### Requirement: Bag tools carry bean-detail fields

The `bag_update` tool SHALL accept `origin`, `region`, `farm`, `producer`, `variety`, `elevation`, `process`, `harvest`, `qualityScore`, `placeOfPurchase`, `tastingNotes` and `link`, merging them into `beanBaseData` with the editor's merge semantics. Such an update SHALL trigger the same Visualizer edit-push as an editor save. `bag_list` and `bag_update` SHALL emit stored detail fields as human-readable strings.

#### Scenario: Agent adds a URL and tasting notes
- **WHEN** an agent calls `bag_update` with `link` and `tastingNotes` for a bag
- **THEN** the blob SHALL carry both values, the response SHALL echo them
- **AND** the Visualizer push SHALL fire if the bag has a `visualizerBagId`

#### Scenario: Detail fields visible in bag_list
- **WHEN** an agent calls `bag_list`
- **THEN** each bag with stored details SHALL include them (origin, variety, process, tasting notes, link, etc.)

#### Scenario: Clearing a detail field
- **WHEN** an agent calls `bag_update` with `region` set to an empty string
- **THEN** the `region` key SHALL be removed from the blob and absent from subsequent reads

#### Scenario: Merge preserves link identity keys
- **WHEN** a `bag_update` sets detail fields on a bag whose `link` identity keys are stored
- **THEN** the link identity keys are preserved by the merge

### Requirement: Recipe tool family

The recipe tool family (`mcptools_recipes.cpp`) SHALL register `recipe_list`, `recipe_get`, `recipe_create`, `recipe_update`, `recipe_create_from_shot`, `recipe_clone`, `recipe_archive` and `recipe_activate`. Read tools SHALL register at the read access level, mutating tools at the write level. `recipe_activate` SHALL use the same shared controller path as the UI, and lifecycle rules (archive-only for used recipes) SHALL be enforced.

#### Scenario: AI saves a dialed-in shot
- **WHEN** an MCP client calls `recipe_create_from_shot` with a shot id and a name
- **THEN** a recipe is created prefilled from that shot's record and steam snapshot, with provenance recorded

#### Scenario: AI clones a family variant
- **WHEN** an MCP client calls `recipe_clone` and then `recipe_update` to change the milk weight and name
- **THEN** an independent recipe exists and the source recipe is unchanged

### Requirement: Recipe fields follow the data conventions

Recipe tool responses and inputs SHALL follow the house data conventions. Grind SHALL be an object carrying `mode` (`inherited` or `pinned`), `value`, and the resolved effective value, with inherited grind resolving from the linked bag. Responses SHALL expose the linked bag (`bagId` and display identity), and create and update SHALL accept `bagId`. The hot-water block SHALL round-trip as the steam block does.

#### Scenario: Grind representation
- **WHEN** `recipe_get` returns a recipe that inherits grind from its linked bag
- **THEN** the response shows `"grind": {"mode": "inherited", "value": <linked bag's current grind>}`

#### Scenario: Bag link over MCP
- **WHEN** an MCP client calls `recipe_get` on a recipe whose linked bag was finished
- **THEN** the response carries the `bagId`, the bag's display identity, and a human-readable stale indication

#### Scenario: Hot-water block round-trips over MCP
- **WHEN** an MCP client calls `recipe_create` (or `recipe_update`) with a hot-water block and later calls `recipe_get`
- **THEN** the block is accepted against the tool schema, persisted, and returned unchanged (including its `order`)

#### Scenario: Temperature is the offset field
- **WHEN** a recipe holding a −3° temperature offset is returned by any recipe tool
- **THEN** the response carries `tempOffsetC: -3` (a signed delta in °C against the recipe's profile) and no absolute recipe-temperature field

#### Scenario: Hot-water order and units
- **WHEN** a client sends a hot-water block on `recipe_create`
- **THEN** each field's unit is documented in the tool schema, and `order` is `before` (long black) or `after` (Americano)

#### Scenario: House conventions applied
- **WHEN** a recipe response is read
- **THEN** field names carry unit suffixes, timestamps are ISO 8601 with timezone, and enum values are human-readable strings

### Requirement: Recipe tools carry drink type and accept profile-less hot-water recipes

The recipe tools SHALL expose `drinkType` on `recipe_list` and `recipe_get`, and accept it on `recipe_create` and `recipe_update`. When omitted on create it SHALL be derived from the blocks, using the installed profile's `beverage_type` from the profile catalog. A profile-less recipe SHALL be accepted only with a hot-water block whose `hasWater` is true, and `recipe_activate` SHALL follow the shared profile-less activation path.

#### Scenario: Create hot-water tea recipe via MCP
- **WHEN** an MCP client calls `recipe_create` with a name, a tea bean link, and a hot-water block but no profile
- **THEN** the recipe is created and `recipe_get` returns it with `drinkType` reflecting hot-water tea

#### Scenario: Profile-less without hot water rejected
- **WHEN** an MCP client calls `recipe_create` with no profile and no hot-water block
- **THEN** the tool returns a validation error naming the rule

#### Scenario: Drink type re-derived on update
- **WHEN** `recipe_update` changes the blocks and does not set `drinkType`
- **THEN** `drinkType` is re-derived from the new blocks

#### Scenario: Tea profile derives as tea
- **WHEN** a recipe references an installed tea profile whose embedded JSON is absent
- **THEN** its `drinkType` derives as tea, not espresso

### Requirement: Bag tools expose kind
`bag_list` and bag detail payloads SHALL include the bag's `kind` (`"coffee"` or `"tea"`); `bag_update` SHALL NOT accept changing it (kind is set at creation). Tea bags' structured brewing fields (teaType, brewTempC, leafGramsPer100Ml, steepTime) SHALL appear in bag payloads following the data conventions (units in field names), and `bag_update` SHALL reject tea-vocabulary writes on a coffee bag with an error naming the rule (never a silent drop).

#### Scenario: Tea bag in bag_list
- **WHEN** an MCP client calls `bag_list` with a tea bag in inventory
- **THEN** the bag carries kind "tea" and its stated brewing fields

#### Scenario: Kind is immutable via MCP
- **WHEN** an MCP client calls `bag_update` attempting to change kind
- **THEN** the update is rejected or the field ignored with the response noting kind is creation-time only

### Requirement: Bags are creatable via MCP with kind stamped at creation

The MCP server SHALL provide `bag_create` at the write access level, with kind `coffee` (default) or `tea` and at least one of roasterName or coffeeName required. Kind-gated fields SHALL be rejected in both directions, and details SHALL land in the bag blob via the shared merge helper. The created bag SHALL enter the inventory but SHALL NOT become the active bag; `bag_select` activates it.

#### Scenario: Create a tea bag with brewing data
- **WHEN** an MCP client calls `bag_create` with kind "tea", a brand/name, teaType, and brewTempC
- **THEN** the bag appears in `bag_list` with kind "tea" and its brewing fields, and the active bag is unchanged

#### Scenario: Kind-gated create
- **WHEN** an MCP client calls `bag_create` with kind "coffee" and a teaType
- **THEN** the create is rejected with an error naming the tea-only fields

#### Scenario: Coffee-only fields rejected on tea
- **WHEN** an MCP client calls `bag_create` with kind "tea" and a roastLevel or grinderSetting
- **THEN** the create is rejected with an error naming the coffee-only fields

### Requirement: Page extraction is drivable and diagnosable via MCP
The MCP server SHALL provide `bag_extract_details` (control tier): runs the exact in-app two-stage extraction for a bag's product URL (kind selects the coffee/tea vocabulary) and returns the extracted fields WITHOUT writing them, plus diagnostics — which stage ran, the stage-1 failure when the fallback fired, the provider and model, and the fetched text size. Applying fields is the caller's explicit `bag_update`.

#### Scenario: Stage-2 diagnosis on a JS-rendered shop
- **WHEN** an MCP client calls `bag_extract_details` for a bag whose page is a JS-rendered SPA
- **THEN** the response shows stage 2, the stage-1 emptyPage error, and the extracted fields including imageUrl when present

### Requirement: Capability-URL Authorization for Remote Access

When remote MCP is enabled, requests SHALL be authorized solely by an unguessable 128-bit random token generated on-device, carried as `/mcp/<token>` and compared in constant time. A request without the current token SHALL be refused without revealing an MCP server. A request line carrying the current token SHALL receive a bare `404` in either mode, even with malformed framing, as SHALL a token holder's request for an unserved method or path.

#### Scenario: Valid token
- **WHEN** a client POSTs a JSON-RPC request to `/mcp/<token>` with the current token
- **THEN** the request is dispatched to the MCP server and handled normally

#### Scenario: Wrong token
- **WHEN** a client POSTs to `/mcp/<other>` where `<other>` is not the current token
- **THEN** the request is refused without any MCP-identifying headers or body — closed unanswered behind a tunnel, `404` otherwise

#### Scenario: Missing token
- **WHEN** a client POSTs to `/mcp` on the remote surface
- **THEN** the request is refused the same way — closed unanswered behind a tunnel, `404` otherwise

#### Scenario: Wrong token behind a tunnel
- **WHEN** a client POSTs to `/mcp/<other>` on a tunnel-proxied listener, where `<other>` is not the current token
- **THEN** the connection is closed with no bytes written

#### Scenario: Wrong token on a bring-your-own-proxy listener
- **WHEN** a client POSTs to `/mcp/<other>` on a listener fronted by the user's own reverse proxy
- **THEN** the server returns `404` with no MCP-identifying headers or body

#### Scenario: Malformed framing from a stranger
- **WHEN** a request whose request line does not carry the current token arrives with headers that never terminate, a body over the cap, or a non-numeric `Content-Length`
- **THEN** the refusal is the same as for a wrong token — no reply behind a tunnel, `404` otherwise

#### Scenario: Malformed framing from a token holder
- **WHEN** the same framing failure arrives on a request line that DOES carry the current token
- **THEN** the server returns `404` in either mode, because a silent drop is indistinguishable from a dropped network and would leave a legitimate client retrying a request that can never succeed

#### Scenario: Valid token, unserved method
- **WHEN** a client sends a method other than `POST`, `GET` or `DELETE` to `/mcp/<token>` with the current token
- **THEN** the server returns `404` in either mode

#### Scenario: Refusal form depends on the listener
- **WHEN** a request without the current token arrives behind an embedded tunnel
- **THEN** the connection is closed with no response
- **AND** on a bring-your-own-proxy listener the response is a bare HTTP `404`, because a silent drop there reads as a broken backend

#### Scenario: Silence does not hide the hostname
- **WHEN** a stranger's request is silently dropped behind the tunnel
- **THEN** the tunnel edge still serves its own error for a backend that hangs up, so the hostname stays visibly configured, and this application confirms nothing

### Requirement: Token Rotation as Revocation

The settings UI SHALL provide a rotate-token action. Rotation SHALL
generate a fresh token, immediately close all active remote MCP
sessions, and cause requests bearing the previous token to receive
`404`. The UI SHALL display the new connector URL and QR code after
rotation.

#### Scenario: User rotates the token
- **WHEN** the user confirms the rotate-token action
- **THEN** a request using the old token returns `404` within one second and active remote sessions are terminated

#### Scenario: New URL shown
- **WHEN** rotation completes
- **THEN** the settings UI displays the connector URL containing the new token, with copy and QR affordances

### Requirement: Isolated Remote Surface

The remote reachability path SHALL terminate at a dedicated listener
that serves only the tokenized MCP route (`POST`, `GET`, `DELETE` on
`/mcp/<token>`). All other paths and methods on the remote surface
SHALL return `404`. No other ShotServer route (web layout editor,
REST endpoints, data-migration API) SHALL be reachable through the
remote surface.

#### Scenario: Non-MCP route via tunnel
- **WHEN** a request arrives on the remote surface for any other path (e.g. `/layout`, `/api/shots`)
- **THEN** the server returns `404`

#### Scenario: LAN surface unchanged
- **WHEN** a LAN client uses the existing local `/mcp` endpoint
- **THEN** behavior is unchanged by remote mode being enabled or disabled

### Requirement: Remote Sessions Honor Existing MCP Gates

Remote MCP sessions SHALL be subject to the same `mcpAccessLevel`
filtering, `mcpConfirmationLevel` confirmation flows (including the
in-app dialog for machine-start operations), session limits, and
rate limits as LAN sessions.

#### Scenario: Access level enforced remotely
- **WHEN** `mcpAccessLevel` is Monitor Only and a remote client calls a control-category tool
- **THEN** the call is rejected identically to the LAN behavior

#### Scenario: In-app confirmation over the tunnel
- **WHEN** `mcpConfirmationLevel` requires confirmation and a remote client calls `machine_start_espresso`
- **THEN** the on-device confirmation dialog is shown and the held response resolves per the user's choice or the dialog timeout

### Requirement: Failed-Token Rate Limiting

The remote surface SHALL rate-limit requests that fail token
validation, per source, and SHALL log failures without echoing the
attempted path.

#### Scenario: Repeated bad tokens
- **WHEN** a source exceeds the failed-token limit
- **THEN** further requests from that source are dropped or delayed for the limit window

### Requirement: Reachability Mode — Embedded Tailscale Funnel

In Tailscale mode the app SHALL run an embedded tsnet node joining the tailnet and expose the remote surface via Tailscale Funnel at `https://<node>.<tailnet>.ts.net`. Setup SHALL surface the tsnet login URL (link and QR) and any Funnel-approval URL, and disabling remote MCP SHALL stop the tsnet listener. A confirmation-gated sign-out (forget) action SHALL wipe the tsnet node state, reachable whenever a tsnet node exists, even before login.

#### Scenario: First-time Tailscale setup
- **WHEN** the user selects Tailscale mode and enables remote MCP with no prior tsnet state
- **THEN** the UI shows the tailnet login URL as link and QR and reports status until the node is authorized and Funnel is active

#### Scenario: Funnel active
- **WHEN** the tsnet node is authorized and Funnel is enabled
- **THEN** the UI displays the stable connector URL `https://<node>.<tailnet>.ts.net/mcp/<token>` and requests to it reach the remote surface

#### Scenario: Network change
- **WHEN** the device changes networks while Tailscale mode is active
- **THEN** the tsnet node reconnects automatically and the connector URL remains unchanged

#### Scenario: Forget tailnet
- **WHEN** the user invokes the sign-out action and confirms
- **THEN** the tsnet node is stopped, the tsnet state directory is wiped, and re-enabling requires a fresh tailnet login

#### Scenario: Sign out recovers a stuck login loop
- **WHEN** the node is waiting for login (a fresh login URL is shown) because its stored nodekey belongs to a different or deleted tailnet, so authorization keeps failing
- **THEN** the sign-out action is available in that state, and confirming it wipes the stored identity so the next enable produces a login that can succeed under the intended account

#### Scenario: Confirmation required
- **WHEN** the user invokes the sign-out action
- **THEN** a confirmation dialog is shown warning that the device's tailnet identity will be cleared, and the state is wiped only if the user confirms

#### Scenario: Forget warns before wiping
- **WHEN** the user invokes sign-out (forget)
- **THEN** a confirmation warns that the device's tailnet identity is cleared and re-enabling requires a fresh login

#### Scenario: Re-enable after forget
- **WHEN** Tailscale mode is re-enabled after forget
- **THEN** the node starts with no prior identity and issues a fresh login URL, without reusing the wiped nodekey

### Requirement: Reachability Mode — Bring-Your-Own URL

In custom-URL mode, the user SHALL provide an `https://` base URL
that they have arranged to forward to the device's remote listener.
The UI SHALL compose and display the full connector URL
(`<base>/mcp/<token>`) with copy and QR affordances. The app SHALL
NOT attempt to manage the user's tunnel.

#### Scenario: Custom URL configured
- **WHEN** the user enters `https://coffee.example.ts.net` as the base URL
- **THEN** the UI displays connector URL `https://coffee.example.ts.net/mcp/<token>` with copy and QR

#### Scenario: Non-HTTPS rejected
- **WHEN** the user enters an `http://` base URL
- **THEN** the value is rejected with a validation message

### Requirement: Remote Access Status Visibility

The settings UI SHALL show the live state of the remote surface
(`off`, `starting`, `active`, `reconnecting`, `error`) and SHALL NOT
display the connector URL as usable while the underlying tunnel is
down.

#### Scenario: Tunnel drops
- **WHEN** the active tunnel disconnects
- **THEN** the status changes from `active` to `reconnecting` (or `error`) and the UI reflects that the URL is currently unreachable

### Requirement: Remote MCP Disable Semantics

Disabling remote MCP SHALL stop all tunnels, close the remote
listener, and terminate remote sessions. The capability token SHALL
be retained so re-enabling does not invalidate an already-configured
connector.

#### Scenario: Toggle off
- **WHEN** the user disables remote MCP
- **THEN** in-flight remote sessions are closed, the remote listener stops, and the public URL ceases to resolve to the app

#### Scenario: Re-enable
- **WHEN** the user re-enables remote MCP in the same mode
- **THEN** the previous connector URL (same host, same token) works again without reconfiguring claude.ai

### Requirement: Stateful Sessions Are Established Only by an SSE Stream

The MCP server SHALL treat a session as **stateful** only once the client establishes an SSE stream for it (a successful `GET /mcp` retained for notifications). A session created solely by a `POST` `initialize` with no SSE stream SHALL be **ephemeral** and SHALL NOT retain durable server-side state beyond answering its own connection. Access-level, confirmation-level, origin, protocol-version and rate-limit gating SHALL apply identically to both.

#### Scenario: POST-only client is served without a durable session

- **WHEN** a client sends `initialize` followed by tool calls over `POST` and never opens an SSE stream
- **THEN** the server answers every request successfully
- **AND** the server does not retain a durable session slot for that client once its in-flight requests are complete

#### Scenario: SSE subscriber keeps a durable session

- **WHEN** a client completes `initialize` and then opens an SSE stream via `GET /mcp` for that session
- **THEN** the server retains the session, its negotiated capabilities, and its resource subscriptions for the lifetime of the SSE stream
- **AND** resource-change notifications continue to be pushed to that client

### Requirement: Concurrency Limit Counts Only Stateful Sessions

The `MaxSessions` concurrency limit SHALL count only stateful (SSE-backed) sessions. Ephemeral POST-only sessions SHALL NOT be counted against `MaxSessions`, and their presence SHALL NOT cause a `-32000 "Too many sessions"` rejection of any client.

#### Scenario: Repeated re-initializing client cannot exhaust the pool

- **WHEN** a client re-runs `initialize` on every request without ever echoing the `Mcp-Session-Id` header or opening an SSE stream (the observed ChatGPT `openai-mcp` connector behavior)
- **THEN** the server continues to answer that client's requests
- **AND** no request from any other client is rejected with `-32000 "Too many sessions"` as a result of that churn

#### Scenario: Stateful sessions still bounded

- **WHEN** the number of concurrent SSE-backed sessions reaches `MaxSessions`
- **THEN** a further attempt to establish a new stateful (SSE) session is refused
- **AND** the refusal does not disturb existing stateful sessions or ephemeral request handling

### Requirement: Total Session Pool Is Bounded Against Churn Without Rejecting Clients

The server SHALL enforce an absolute backstop on the total number of retained sessions to bound memory. When creating a session would exceed it, the server SHALL evict the least-recently-active ephemeral session, never a stateful session or one holding a pending in-app confirmation, rather than rejecting the new session. A burst of per-request re-initialization SHALL NOT cause any client's request to be rejected.

#### Scenario: Tight-loop initialize is bounded by eviction, not rejection

- **WHEN** a client POSTs `initialize` in a tight loop without echoing a session header, driving the total session count to the backstop
- **THEN** the total retained session count stays bounded at the backstop
- **AND** each new `initialize` still succeeds, the server having evicted the least-recently-active ephemeral session to make room

#### Scenario: Eviction never targets a stateful or confirming session

- **WHEN** the pool reaches the backstop while some sessions hold a live SSE stream or a pending in-app confirmation
- **THEN** eviction skips those sessions and removes only an ephemeral, non-confirming one

### Requirement: Ephemeral Sessions Are Reaped Well Before the Stateful Timeout

Ephemeral (non-SSE) session state SHALL be released by a reaping pass bounded well below the idle-session timeout used for stateful sessions, so that per-request re-initializing clients do not accumulate durable state up to the full stateful timeout. A session with an in-flight request or a pending in-app confirmation SHALL NOT be reaped.

#### Scenario: Ephemeral state does not linger to the stateful timeout

- **WHEN** an ephemeral session has been idle past the ephemeral-reaping bound but well short of the stateful idle-session timeout
- **THEN** the server has released that session's state

#### Scenario: In-flight or confirming ephemeral session is not reaped

- **WHEN** an ephemeral session has a tool call still executing or a pending in-app confirmation
- **THEN** the server does not release that session's state until the request completes and any confirmation resolves

### Requirement: Debug log tools support substring/regex filtering

`debug_get_log` and `shots_get_debug_log` SHALL accept an optional `filter` string and `regex` boolean. A given `filter` makes only matching lines eligible for pagination or tail, applied before offset, limit or tail: case-insensitive substring by default, or case-insensitive regex when `regex` is `true`. The `debug_get_log` description SHALL name the markers (`[Scale]`, `[DE1]`) and warn that a marker is a substring, not a regex.

#### Scenario: Substring filter narrows an app-log request
- **WHEN** an MCP client calls `debug_get_log` with `filter: "R2 error"`
- **THEN** the response's `log`/`lines` contain only lines whose text contains "R2 error" (case-insensitive), and `returnedLines`/pagination fields are computed against the filtered set, not the full log

#### Scenario: Regex filter on a shot debug log
- **WHEN** an MCP client calls `shots_get_debug_log` with `shotId`, `filter: "SAW.*trigger"`, `regex: true`
- **THEN** the response contains only lines matching that pattern

#### Scenario: No filter reproduces existing behavior
- **WHEN** an MCP client calls either tool without a `filter` parameter
- **THEN** the response is identical in shape and content to the tool's behavior before this change

#### Scenario: A caller can reproduce a device panel from the tool description alone
- **WHEN** an MCP client has only `debug_get_log`'s description and wants the lines the connections page's scale view shows
- **THEN** the description tells it to pass the `[Scale]` marker as `filter` with `minLevel: "INFO"` and `session: -1`, and that call returns that set

#### Scenario: The marker-as-regex trap is documented
- **WHEN** an MCP client reads the description before filtering on a marker
- **THEN** it is told to pass the marker as a substring, because `[Scale]` under `regex: true` is a character class that matches nearly every line

### Requirement: Debug log discloses what its markers do not cover

`debug_get_log` SHALL accept a boolean `families` parameter that, when `true`, returns a census of the addressed file's line prefixes instead of log lines. The census SHALL partition every line into exactly one of: a registered subsystem marker, an unregistered bracketed prefix, a bare `ClassName:` prefix, or no prefix. An empty census SHALL name its cause rather than report zeros.

#### Scenario: A caller with no prior knowledge finds the subsystems that exist

- **WHEN** an MCP client calls `debug_get_log` with `families: true`
- **THEN** the response lists registered markers, unregistered bracketed prefixes, and `ClassName:` prefixes separately, each with a line count and a `filter` expression that retrieves it

#### Scenario: The unregistered families are not presented as searchable-in-full

- **WHEN** the census reports an unregistered prefix
- **THEN** the response states that its `filter` is a plain substring over one hand-written prefix and may be incomplete where a subsystem logs under more than one spelling

#### Scenario: An unreadable log is not reported as an empty one

- **WHEN** an MCP client calls `debug_get_log` with `families: true` and the log file is missing or cannot be opened
- **THEN** the response names the path and which of those states it is in, rather than returning all-zero counts

#### Scenario: Census entries carry counts and filters
- **WHEN** a census is returned
- **THEN** each prefix carries its line count and a ready-to-use `filter` expression, ordered by line count descending

#### Scenario: Census describes the file, not the build
- **WHEN** a census is returned
- **THEN** the response states that it describes that file, because the log is a ring buffer spanning app versions

### Requirement: App debug log supports a minimum-severity filter

`debug_get_log` SHALL accept an optional `minLevel` (`"DEBUG" | "INFO" | "WARN" | "ERROR" | "FATAL"`) that restricts returned lines to that level or higher, based on the level tag on every persisted line. It SHALL combine with `filter`, so a line must satisfy both. An unrecognized value SHALL be rejected with an `{"error": ...}` response. `shots_get_debug_log` SHALL accept `minLevel` without error and ignore it.

#### Scenario: Only warnings and errors from the current session
- **WHEN** an MCP client calls `debug_get_log` with `session: -1, minLevel: "WARN"`
- **THEN** only lines tagged WARN, ERROR, or FATAL from the most recent session are returned

#### Scenario: minLevel combined with filter
- **WHEN** an MCP client calls `debug_get_log` with `filter: "BLE", minLevel: "ERROR"`
- **THEN** only ERROR or FATAL lines containing "BLE" are returned

#### Scenario: minLevel ignored on shot debug log
- **WHEN** an MCP client calls `shots_get_debug_log` with `shotId` and `minLevel: "WARN"`
- **THEN** the call succeeds and returns lines from the shot's debug log unaffected by `minLevel`

#### Scenario: Unrecognized minLevel value is rejected, not silently ignored
- **WHEN** an MCP client calls `debug_get_log` with `minLevel: "WARNING"` (not one of the five recognized values)
- **THEN** the response is `{"error": ...}` naming the invalid value, rather than returning every line unfiltered

### Requirement: Debug log tools support a tail mode

`debug_get_log` and `shots_get_debug_log` SHALL accept an optional integer `tail` that returns the last `tail` qualifying lines (after `filter` and `minLevel`) of the addressed range, with `hasMore` `false`, without needing the total line count. A positive `tail` SHALL take precedence over `offset`. A `tail` of zero or less SHALL behave as if omitted, with `hasMore` still reflecting lines beyond the page.

#### Scenario: Tail of the current session
- **WHEN** an MCP client calls `debug_get_log` with `session: -1, tail: 100`
- **THEN** the response contains the last 100 lines of the most recent session, without a preceding call to learn its line count

#### Scenario: Tail of a filtered shot debug log
- **WHEN** an MCP client calls `shots_get_debug_log` with `shotId`, `filter: "flow calibration"`, `tail: 20`
- **THEN** the response contains the last 20 lines of that shot's debug log matching "flow calibration"

#### Scenario: Tail overrides offset
- **WHEN** an MCP client calls either tool with both `offset` and a positive `tail` set
- **THEN** the response is computed using `tail` and `offset` is ignored

#### Scenario: tail: 0 does not falsely report hasMore as false
- **WHEN** an MCP client calls either tool with `tail: 0` and there are more qualifying lines beyond the returned page
- **THEN** the response is paginated normally via `offset`/`limit`, and `hasMore` accurately reflects whether more qualifying lines remain — it is NOT forced to `false` merely because the `tail` key was present

### Requirement: Debug log responses carry absolute line numbers
`debug_get_log` and `shots_get_debug_log` SHALL include, alongside the existing newline-joined `log` string field, a `lines` array of `{"line": <absolute 0-based line number in the addressed range>, "text": <line text>}` objects for every returned line, so a caller can issue a follow-up `offset`-based request to see context around a specific hit.

#### Scenario: Line numbers accompany a filtered result
- **WHEN** an MCP client calls `debug_get_log` with `filter: "disconnected"`
- **THEN** each entry in the response's `lines` array carries the absolute line number of that match within the addressed range, in addition to the existing `log` string

### Requirement: Debug log tools support consecutive-line deduplication

`debug_get_log` and `shots_get_debug_log` SHALL accept an optional `dedupe` boolean. When `true`, consecutive lines that are identical once their leading `[<elapsed>]` timestamp is stripped SHALL collapse into one entry, applied before `tail`, `offset` and `limit`. Each collapsed entry SHALL carry `count` and `lastLine` (the absolute line number of the last occurrence) alongside `line` and `text`. Non-consecutive repeats SHALL NOT be collapsed.

#### Scenario: A repeated burst collapses to one entry
- **WHEN** an MCP client calls `debug_get_log` with `dedupe: true` over a range where the same warning fires 3 times consecutively (identical text apart from each line's own timestamp)
- **THEN** the response's `lines` array contains one entry for that warning with `count: 3` and `lastLine` set to the absolute line number of the third occurrence

#### Scenario: Non-consecutive repeats stay separate
- **WHEN** the same message occurs twice in the addressed range with a different, non-matching line in between
- **THEN** `dedupe: true` SHALL produce two separate entries, not one collapsed entry

#### Scenario: dedupe combines with filter and tail
- **WHEN** an MCP client calls either tool with `filter`, `dedupe: true`, and `tail` together
- **THEN** filtering is applied first, then consecutive collapsing, then `tail` selects the last N resulting (collapsed) entries

#### Scenario: No dedupe reproduces existing behavior
- **WHEN** an MCP client calls either tool without a `dedupe` parameter
- **THEN** the response is identical in shape to the tool's behavior without this parameter — no `count` or `lastLine` fields appear

### Requirement: App debug log session index is cached
The app debug log's session-boundary index (used by `debug_get_log`'s `sessions=true` and `session=N` modes) SHALL be cached keyed on the persisted log file's size and modification time, and rebuilt only when either differs from the cached key, instead of rescanning the full file on every call.

#### Scenario: Repeated session queries reuse the cached index
- **WHEN** an MCP client calls `debug_get_log` with `sessions: true` twice in a row with no log activity in between
- **THEN** the second call reuses the cached session index rather than rescanning the persisted log file

#### Scenario: Cache invalidates after new log activity
- **WHEN** new lines are appended to the persisted log file between two `debug_get_log` calls
- **THEN** the next `sessions: true` or `session: N` call rebuilds the index and reflects the new session boundaries

#### Scenario: A read failure is logged, not silent
- **WHEN** the persisted log file's size/modification time can be read but the file itself cannot be opened for reading
- **THEN** a warning is logged naming the file and the reason, so the condition is diagnosable from the log rather than indistinguishable from a genuinely empty log

### Requirement: A Terminated Session Is Rejected With HTTP 404

After an explicit `DELETE`, the server SHALL record the session ID and respond HTTP 404 to any later request carrying it, on any method. Sessions the server ends itself (idle expiry, orphan collection, pool eviction) SHALL NOT be recorded. An unrecognized session ID SHALL continue to be served by the recovery path rather than rejected. The record SHALL be bounded, and when full the oldest record SHALL be dropped.

#### Scenario: Request after explicit termination

- **WHEN** a client sends `DELETE` with `Mcp-Session-Id: X` and then POSTs a request carrying `Mcp-Session-Id: X`
- **THEN** the server responds HTTP 404

#### Scenario: SSE stream on a terminated session

- **WHEN** a client sends `DELETE` with `Mcp-Session-Id: X` and then issues `GET` with `Accept: text/event-stream` carrying `Mcp-Session-Id: X`
- **THEN** the server responds HTTP 404 rather than opening a stream that can never carry an event for that session

#### Scenario: Request after session expiry

- **WHEN** a session is collected by the expiry reaper and a client then POSTs a request carrying that session ID
- **THEN** the server serves the request via the existing recovery path and does not respond 404

#### Scenario: Unrecognized session ID

- **WHEN** a client POSTs a request carrying a session ID the server never issued and never terminated
- **THEN** the server serves the request via the existing recovery path and does not respond 404

#### Scenario: Initialize is always accepted

- **WHEN** a client POSTs `initialize` carrying a terminated session ID
- **THEN** the server creates a new session and responds normally

#### Scenario: Bounded record drops the oldest
- **WHEN** the record of terminated session IDs reaches its bound
- **THEN** the oldest record is dropped, and its ID is thereafter treated as unrecognized

#### Scenario: Server-ended sessions stay recoverable
- **WHEN** the server ends a session on its own initiative
- **THEN** the ID is not recorded, a deliberate shortfall against the specification that is revisited only against evidence from live clients

### Requirement: Resource Contents Carry Only Schema-Defined Fields

Each entry in a `resources/read` `contents[]` array SHALL carry only fields
defined by the MCP `ResourceContents` schema — `uri`, `mimeType`, `_meta`, and
`text` or `blob`. The server SHALL NOT emit `structuredContent` inside a
resource content entry: that field is defined on `CallToolResult` only, and the
serialized JSON is already carried by `text`.

#### Scenario: Resource read at the current version

- **WHEN** a client negotiating `2025-11-25` reads any resource
- **THEN** each `contents[]` entry carries `uri`, `mimeType` and `text`, and no `structuredContent`

#### Scenario: Resource payload is still fully available

- **WHEN** a client reads a resource whose payload is a JSON object
- **THEN** the `text` field contains that payload serialized as JSON

### Requirement: Resource-Not-Found Reports JSON-RPC Code -32002

A `resources/read` for a URI the server does not serve SHALL return JSON-RPC
error code `-32002`, with the requested URI in the error's `data` object. Codes
for other read failures are unchanged.

#### Scenario: Unknown resource URI

- **WHEN** a client calls `resources/read` with `uri: "decenza://nonexistent"`
- **THEN** the response carries a JSON-RPC error with `code: -32002` and `data.uri` naming the requested URI

### Requirement: Unknown Tool Reports JSON-RPC Code -32602

A `tools/call` naming a tool that is not registered SHALL return JSON-RPC error
code `-32602`. Registry failures that describe a server-side fault rather than a
bad request — a tool dispatched on the wrong path, or an access level
insufficient for the caller — SHALL continue to return `-32603`.

#### Scenario: Tool name not registered

- **WHEN** a client calls `tools/call` with `name: "no_such_tool"`
- **THEN** the response carries a JSON-RPC error with `code: -32602`

#### Scenario: Access level insufficient

- **WHEN** a client calls a tool above the configured access level
- **THEN** the response carries a JSON-RPC error with `code: -32603`, unchanged

### Requirement: SSE Streams Prime Clients For Reconnection

On opening an SSE stream the server SHALL immediately send a `retry` field and one event carrying an event ID and NO `data` field, since an empty `data` would be dispatched as an empty message. Every later event SHALL carry an event ID unique across all streams in the session. The server MAY ignore `Last-Event-ID` and is not required to replay missed events.

#### Scenario: Stream opens

- **WHEN** a client issues `GET` with `Accept: text/event-stream`
- **THEN** a `retry` field is present and the first event sent carries an `id` field and no `data` field

#### Scenario: Notification carries an ID

- **WHEN** the server pushes a `notifications/resources/updated` message on the stream
- **THEN** that event carries an `id` field distinct from every other event ID in the session

#### Scenario: Client reconnects with Last-Event-ID

- **WHEN** a client reconnects sending a `Last-Event-ID` header
- **THEN** the server opens a fresh stream and is not required to replay events sent after that ID

### Requirement: The Server Conforms To Every Protocol Revision It Advertises

The server SHALL conform to every protocol revision it advertises, legacy revisions included, and SHALL NOT advertise a revision it cannot conform to. Where the protocol's conformance suite covers a revision, conformance SHALL be verified against it, and a green run SHALL NOT be reported as evidence about revisions it did not exercise. Any deviation SHALL be deliberate, recorded where it occurs, and state what it protects.

#### Scenario: An advertised revision is exercised

- **WHEN** the conformance suite is run against the server for a revision the suite covers
- **THEN** every requirement of that revision either passes, or fails at a point the code documents as a deliberate deviation

#### Scenario: An advertised revision the suite does not cover

- **WHEN** the server advertises a revision the conformance suite has no scenarios for
- **THEN** its conformance is reported as unverified rather than implied by the other revisions' results

#### Scenario: A deviation protects a client the spec would break

- **WHEN** conformance requires behaviour that would leave a known real client unable to recover
- **THEN** the deviation is kept, and the code records which client it protects and why

#### Scenario: A revision that cannot be served

- **WHEN** the server cannot conform to a revision
- **THEN** that revision is absent from the list of versions the server advertises

#### Scenario: Permitted deviation
- **WHEN** a deviation exists to keep a real client working
- **THEN** it is permitted, while an unexamined deviation is not

### Requirement: Session Requirements Govern The Legacy Era Only

Every requirement in this capability concerning sessions — how a session
becomes stateful, the concurrency limit, the total pool bound, the reaping of
ephemeral sessions, and the rejection of a terminated session — SHALL be read
as governing the legacy era, in which sessions exist.

The modern era has no sessions for those requirements to govern. Their absence
there is not a gap in conformance.

#### Scenario: A modern request and the session limits

- **WHEN** the server is serving modern requests
- **THEN** no session is created for them, and they are not counted against any session limit

#### Scenario: Legacy sessions are still bounded

- **WHEN** legacy clients connect
- **THEN** every session limit in this capability applies to them exactly as before

### Requirement: List Results Are Returned In A Deterministic Order

`tools/list` and `resources/list` SHALL each return their entries in an order
that is stable across process restarts for an unchanged set of entries, so that
a client may cache the result and so that a repeated listing does not defeat
prompt caching.

The order SHALL NOT depend on the iteration order of an unordered container.

#### Scenario: Two runs return the same order

- **WHEN** a client lists tools, the server restarts with the same tools registered, and the client lists tools again
- **THEN** the two responses carry the tools in the same order

#### Scenario: Two runs return the same resource order

- **WHEN** a client lists resources, the server restarts with the same resources registered, and the client lists resources again
- **THEN** the two responses carry the resources in the same order

#### Scenario: Order is independent of registration order

- **WHEN** the order in which tools are registered changes but the set of tools does not
- **THEN** the listing order is unchanged

### Requirement: List And Read Results Carry Cache Guidance

`tools/list`, `resources/list` and `resources/read` SHALL carry a freshness
hint stating how long the result may be reused, and a scope stating whether
the result may be cached beyond the requesting caller.

In the modern era these SHALL be present on every such result; they are not
optional there.

A result whose content depends on the caller's access level SHALL NOT be
marked cacheable beyond that caller.

#### Scenario: A client caches a tool listing

- **WHEN** a client lists tools
- **THEN** the response states how long the listing may be reused

#### Scenario: Access-dependent results are not shared

- **WHEN** a listing reflects the caller's access level
- **THEN** its cache scope does not permit reuse for another caller

### Requirement: A Remote Caller Is Named By Something A Proxy Cannot Collapse

Where an embedded tunnel proxies the remote listener, every client arrives from loopback, so log lines and rate-limiter keys for such a caller SHALL use a label stating the request came from the public internet. That label SHALL be produced once by the listener that knows its exposure and supplied to every other component that keys or reports on the caller. A listener that is not tunnel-proxied SHALL keep the peer address.

#### Scenario: Rejection behind a tunnel
- **WHEN** an unauthorized request arrives on a tunnel-proxied listener
- **THEN** the log line names it as coming from the public internet rather than from `127.0.0.1`

#### Scenario: Rate-limit refusal behind a tunnel
- **WHEN** a tunnel-proxied caller exceeds the stateless era's control-call budget
- **THEN** the refusal line names the same caller the connector logs, not the loopback address

### Requirement: An Unauthenticated Caller Cannot Fill The Debug Log

The number of log lines an unauthorized caller can cause SHALL be bounded well below one per request. The per-source failed-token budget SHALL be small, and beyond it the connection SHALL be dropped rather than answered. Bounding SHALL NOT mean going silent about scale: after per-request lines stop, the running count SHALL still be recorded at increasing intervals.

#### Scenario: Sustained rejection
- **WHEN** one source sends many more unauthorized requests than the budget within a minute
- **THEN** the number of warnings emitted is fewer than the number of requests

#### Scenario: Scale is still recorded
- **WHEN** unauthorized requests from one source continue past the point where per-request logging stops
- **THEN** the log still receives lines carrying the running count for that minute

#### Scenario: A valid token never counts toward the budget
- **WHEN** a client holding a valid token makes requests
- **THEN** no request fails the check, so the budget never applies to it

### Requirement: settings_set applies Brew Settings values as brew overrides

`settings_set` SHALL apply Brew Settings fields as Brew Settings OK does, without starting a shot or editing the profile. Sending both `targetWeight` and `yieldRatio` SHALL be rejected, and `0` for either SHALL clear the yield override. `espressoTemperature` SHALL be 70-100 °C. A non-numeric or negative value SHALL be rejected with nothing written. The reply SHALL carry a `brew` object read after the change, with a `note` when it differs.

#### Scenario: Dialing a ratio over MCP
- **WHEN** the dose is 18 g and a client calls `settings_set` with `yieldRatio: 2.5`
- **THEN** the session yield anchor is `{2.5, ratio}`, the reply's `brew.targetWeightG` is 45, and the profile's `target_weight` is unchanged

#### Scenario: Both yield keys are rejected
- **WHEN** a client calls `settings_set` with `targetWeight` and `yieldRatio`
- **THEN** the call returns an error and nothing is written

#### Scenario: A value that is not a number is rejected
- **WHEN** a client calls `settings_set` with `targetWeight: "heavy"`
- **THEN** the call returns an error and the yield override is unchanged

#### Scenario: Temperature is an override, not a profile edit
- **WHEN** a client calls `settings_set` with `espressoTemperature: 91` on a 93 °C profile
- **THEN** the temperature override is 91 °C, the profile is uploaded, and the profile is not marked modified

#### Scenario: Clear restores the bean's ratio
- **WHEN** the active bag saves `{2.0, ratio}`, the session anchor is `{40, absolute}`, and a client calls `settings_set` with `clearBrewOverrides: true`
- **THEN** the session anchor is `{2.0, ratio}`

#### Scenario: Target equal to the profile is not an override
- **WHEN** `targetWeight` equals the profile's own target
- **THEN** no yield override is armed

#### Scenario: Temperature equal to the profile clears the override
- **WHEN** `espressoTemperature` equals the profile's temperature
- **THEN** the temperature override is cleared and the profile is re-uploaded

#### Scenario: Clear restores the recipe temperature
- **WHEN** `clearBrewOverrides` is `true` and a recipe is active
- **THEN** the temperature returns to the recipe's (profile temperature plus offset), or the profile's if no recipe is active, and the yield returns to the recipe or bag anchor when it designs one

#### Scenario: Dose applies before the ratio resolves
- **WHEN** `dyeBeanWeight` is written in the same call as `yieldRatio`
- **THEN** the dose applies before the ratio resolves, and `dyeGrinderSetting` and `dyeGrinderRpm` keep their meaning

#### Scenario: Preset and tare settings write
- **WHEN** `ratioPreset1`-`ratioPreset3`, `doseCupTareWeight` or `doseCaptureSoundEnabled` is sent
- **THEN** the setting is written, a non-numeric value or non-boolean `clearBrewOverrides` is rejected, and `clearBrewOverrides` is refused while the active recipe is loading

### Requirement: settings_get reports Brew Settings state

`settings_get` category `espresso` SHALL report `targetWeightG` (the stop target), `espressoTemperatureC` (the brew temperature), `brewYieldMode` and `brewYieldValue` (the session anchor), `yieldRatio` (the effective ratio, or 0), `hasTemperatureOverride` and `temperatureOverrideC`, and `yieldIsRealOverride` and `temperatureIsRealOverride`.

#### Scenario: A ratio-anchored session reads back
- **WHEN** the session anchor is `{2.5, ratio}` and the dose is 18 g
- **THEN** `settings_get` category `espresso` returns `brewYieldMode: "ratio"`, `brewYieldValue: 2.5`, `targetWeightG: 45`

### Requirement: settings_get reports the baseline and persist target

`settings_get` SHALL report `baselineYieldMode`, `baselineYieldValue`, `baselineYieldSource` (`recipe`, `bag` or `profile`) and `baselineTemperatureC`, and `yieldPersistTarget` (`recipe`, `bag` or empty, where Update Recipe or Update Bag would write).

#### Scenario: Persist target names where Update would write
- **WHEN** a recipe supplies the session's yield anchor
- **THEN** `yieldPersistTarget` is `recipe`

### Requirement: settings_get reports ratio presets and dose settings

`settings_get` SHALL report `lastUsedRatio`, `ratioPreset1`-`ratioPreset3`, `doseCupTareWeightG` and `doseCaptureSoundEnabled`.

#### Scenario: Presets read back
- **WHEN** an MCP client calls `settings_get` for category `espresso`
- **THEN** the three ratio presets, the cup tare weight and the capture-sound flag are present

### Requirement: profiles_edit_params saves a profile temperature like Update Profile

`profiles_edit_params` SHALL accept `espressoTemperature` (70-100 °C) on every editor type, sent without other parameters, and apply it through the same path as Brew Settings' Update Profile: shift every frame to the new temperature, clear a temperature override, upload, and save the profile when it has a file. The reply SHALL report `saved` and say why a profile was not saved (read-only built-in, no file); a failed write SHALL be reported as an error.

#### Scenario: Saving a temperature to the profile
- **WHEN** a client calls `profiles_edit_params` with only `espressoTemperature: 92` on a saved, writable 93 °C profile
- **THEN** the profile's `espresso_temperature` is 92 °C, every frame shifts by -1 °C, no temperature override remains, and `saved` is true

#### Scenario: Mixed with other parameters
- **WHEN** a client sends `espressoTemperature` together with `pourFlow`
- **THEN** the call returns an error and the profile is unchanged

### Requirement: equipment creates packages

The `equipment` tool SHALL accept `action=create` with a grinder and/or basket identity, an optional `name` and optional `puckPrep` flags, using the same storage rule as the Switch Equipment dialog: an identical package already in inventory SHALL be returned instead of duplicated, with `created: false`, and a name already used by another package SHALL be rejected.

#### Scenario: Creating a package
- **WHEN** a client calls `equipment` with `action: create`, `grinderBrand: "Niche"`, `grinderModel: "Zero"`
- **THEN** the response carries the new package with `created: true`, and `action=list` includes it

#### Scenario: Creating gear that already exists
- **WHEN** a client creates a package whose full identity matches one in inventory
- **THEN** the response carries that package with `created: false`

### Requirement: machine_start does not take brew overrides

`machine_start action=espresso` SHALL NOT accept dose, yield, temperature, grind or RPM arguments; a call carrying any of them SHALL be refused before any confirmation is requested, without starting a shot. Its description SHALL direct clients to set brew values with `settings_set` first.

#### Scenario: A stale client sends a yield
- **WHEN** a client calls `machine_start` with `action: espresso` and `yield: 40`
- **THEN** the call returns an error naming `settings_set`, and no shot starts

### Requirement: An unanswered machine confirmation is reported as a timeout

When the on-machine confirmation dialog for `machine_start` closes without an answer, the reply and the log SHALL say the call was not confirmed before the dialog timed out, distinct from a Deny tap, and the tool SHALL NOT run.

#### Scenario: Nobody answers the dialog
- **WHEN** a client calls `machine_start` at a confirmation level that raises the dialog and nobody taps it
- **THEN** after the dialog times out the reply's `error` says it was not confirmed before the dialog timed out, and nothing starts

### Requirement: shots_update reaches every field the shot pages edit
`shots_update` SHALL accept every shot field the app and web shot pages can set, including a bag pick (`bagId`, copying the bag's snapshot), the equipment package, the taste axes and the storage dates. Storage dates SHALL be validated by the shared bag rules against the shot's own dates, and a refused value SHALL return its reason rather than be dropped.

#### Scenario: Correcting a shot's storage dates
- **WHEN** a client sends `defrostDate` and `openedDate` that are in order and not in the future
- **THEN** the shot stores them

#### Scenario: A thaw before the freeze is refused
- **WHEN** a client sends a `defrostDate` earlier than the shot's `frozenDate`
- **THEN** the update is refused with the reason and nothing is written

#### Scenario: A bag pick refuses fields it would overwrite
- **WHEN** a client sends `bagId` together with `beanBrand` or a storage date
- **THEN** the update is refused and names the clashing fields
