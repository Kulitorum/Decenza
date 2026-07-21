# mcp-server Specification

## Purpose
Decenza's MCP server: protocol version negotiation and the `MCP-Protocol-Version` header, `Origin` validation, structured tool output, resource-link content blocks, tool/resource titles and icons, and JSON Schema 2020-12 input schemas, plus the domain tool surface for scale connection-priority mode and diagnostics, equipment package (grinder/basket/puck-prep) reads and writes, and bag bean-detail fields.
## Requirements
### Requirement: Latest Protocol Version Support

The MCP server SHALL declare `2025-11-25` as its preferred protocol version and SHALL also accept `2025-06-18`, `2025-03-26`, and `2024-11-05` for backward compatibility. During `initialize`, the server SHALL respond with the client-requested version when it is in the supported set, otherwise SHALL respond with the preferred version.

#### Scenario: Client requests current version
- **WHEN** a client sends `initialize` with `protocolVersion: "2025-11-25"`
- **THEN** the server responds with `protocolVersion: "2025-11-25"`

#### Scenario: Client requests prior version
- **WHEN** a client sends `initialize` with `protocolVersion: "2025-03-26"`
- **THEN** the server responds with `protocolVersion: "2025-03-26"` and SHALL serve subsequent requests under that version

#### Scenario: Client requests unsupported version
- **WHEN** a client sends `initialize` with `protocolVersion: "2023-01-01"`
- **THEN** the server responds with `protocolVersion: "2025-11-25"` (its preferred version)

### Requirement: MCP-Protocol-Version Request Header

For every HTTP request other than `initialize`, the server SHALL accept the `MCP-Protocol-Version` request header. When present, the value MUST equal the version negotiated at `initialize` for the session; mismatches SHALL be rejected with HTTP 400. When absent, the server SHALL assume `2025-03-26` per the spec's compatibility rule.

#### Scenario: Header matches negotiated version
- **WHEN** a client POSTs `tools/call` with `MCP-Protocol-Version: 2025-11-25` after negotiating `2025-11-25`
- **THEN** the server processes the request normally

#### Scenario: Header mismatch
- **WHEN** a client POSTs `tools/call` with `MCP-Protocol-Version: 2024-11-05` after negotiating `2025-11-25`
- **THEN** the server returns HTTP 400 with body indicating protocol version mismatch

#### Scenario: Header absent on legacy client
- **WHEN** a client negotiates `2025-03-26` and POSTs subsequent requests without an `MCP-Protocol-Version` header
- **THEN** the server processes the request normally, assuming `2025-03-26`

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

Every successful `tools/call` response SHALL include a `structuredContent` field carrying the tool's result payload as a JSON object. The existing `content` array with text content blocks SHALL be retained for backward compatibility with clients negotiating `2025-03-26` or earlier.

#### Scenario: Tool returns structured payload
- **WHEN** a client calls a tool that returns a JSON payload
- **THEN** the response includes both `content[]` (with at least one text block) and `structuredContent` (the same payload as a JSON object)

#### Scenario: Legacy client receives identical text
- **WHEN** a client negotiating `2025-03-26` calls the same tool
- **THEN** the response still includes the text content block matching the pre-upgrade behavior

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

Every tool record returned by `tools/list` and every resource record returned by `resources/list` SHALL include an `icons` array with at least one entry. Each icon entry SHALL contain `src` (a `data:image/svg+xml;base64,...` URI), `mimeType: "image/svg+xml"`, and a `sizes` field. Icon assignment SHALL be driven by tool category and resource kind.

#### Scenario: Tool list includes icons
- **WHEN** a client calls `tools/list`
- **THEN** every tool record includes an `icons` array with at least one SVG entry encoded as a `data:` URI

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

The MCP server SHALL expose a `devices_set_scale_priority_mode` tool that sets the persistent backoff policy mode to `enforce` or `observe`. It MUST require an explicit confirmation argument before applying. The change is eventually-consistent: it MUST be queued onto the BLE-manager thread and the response MUST NOT assert the persist has completed; the HIGH-forcing effect of `observe` additionally only applies on the next scale (re)connect and does NOT tear down the current connection. The response MUST state this queued, eventually-consistent contract explicitly. An unrecognized mode value MUST be rejected without changing state.

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
MCP surfaces that report grinder identity — the `de1://dialing` resource (`mcpresources.cpp`), `dialing_get_context`, `dialing_get_grinder_calibration`, and `ai_advisor_invoke` — SHALL resolve grinder brand/model/burrs through the equipment package (the active bag's package for live snapshots; the resolved shot's `equipment_id` for shot-derived blocks). They SHALL additionally expose the package identity (`id`, display `name`, `rpmAdjustable`) and the `rpm` dial-in. All fields SHALL follow MCP data conventions (units in field names, `kind` as a string enum, ISO-8601 timestamps).

#### Scenario: Dialing resource reports the package
- **WHEN** the `de1://dialing` resource is read
- **THEN** the grinder block SHALL include the resolved brand/model/setting plus `rpm` and `rpmAdjustable`, sourced from the active bag's equipment package

### Requirement: equipment_list tool
The MCP server SHALL provide an `equipment_list` tool (modeled on `bag_list`) returning equipment packages: `id`, display `name`, grinder `brand`/`model`/`burrs`, `rpmAdjustable`, `inInventory`, and the last-used grind setting and `rpm`.

#### Scenario: Listing packages
- **WHEN** an agent calls `equipment_list`
- **THEN** it SHALL receive the inventory of equipment packages with the fields above

### Requirement: equipment_select tool
The MCP server SHALL provide an `equipment_select` tool (modeled on `bag_select`) that sets the active bag's equipment package by id (or clears it with 0). Selecting a package SHALL apply that package's last grind setting / rpm to the active bag per the dual-memory rule.

#### Scenario: Selecting a package
- **WHEN** an agent calls `equipment_select` with a valid package id
- **THEN** the active bag's `equipment_id` SHALL be set to that package
- **AND** the active bag's grind setting and rpm SHALL be set to the package's last-dial values

### Requirement: equipment_update tool
The MCP server SHALL provide an `equipment_update` tool (modeled on `bag_update`) that edits a package's grinder identity (brand/model/burrs) and SHALL support creating a package. On a brand/model change, `rpmCapable` SHALL re-derive from the registry. Edits use reference semantics (apply to all referencing bags/shots).

#### Scenario: Editing a package
- **WHEN** an agent calls `equipment_update` changing a package's grinder model
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

The `bag_update` tool SHALL accept the bean-detail parameters `origin`, `region`, `farm`, `producer`, `variety`, `elevation`, `process`, `harvest`, `qualityScore`, `placeOfPurchase`, `tastingNotes`, and `link` (product URL), merging them into the bag's `beanBaseData` blob with the same merge semantics as the bag editor (preserve link identity keys; clearing a value removes the key). A `bag_update` carrying detail fields SHALL trigger the same Visualizer edit-push as an editor save. `bag_list` and the `bag_update` response SHALL emit the stored detail fields as human-readable strings.

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

### Requirement: Recipe tool family
The MCP server SHALL register a recipe tool family (`mcptools_recipes.cpp`): `recipe_list`, `recipe_get`, `recipe_create`, `recipe_update`, `recipe_create_from_shot`, `recipe_clone`, `recipe_archive`, and `recipe_activate`. Read tools SHALL register at the read access level; mutating tools (create/update/clone/archive/activate/create_from_shot) at the write access level, matching the preset tools. `recipe_activate` SHALL invoke the same shared controller activation path as the UI. Lifecycle rules SHALL be enforced (archive-only for used recipes).

#### Scenario: AI saves a dialed-in shot
- **WHEN** an MCP client calls `recipe_create_from_shot` with a shot id and a name
- **THEN** a recipe is created prefilled from that shot's record and steam snapshot, with provenance recorded

#### Scenario: AI clones a family variant
- **WHEN** an MCP client calls `recipe_clone` and then `recipe_update` to change the milk weight and name
- **THEN** an independent recipe exists and the source recipe is unchanged

### Requirement: Recipe fields follow the data conventions
Recipe tool responses and inputs SHALL use the house conventions: unit-suffixed field names (`doseG`, `yieldG`, `milkWeightG`, `tempOffsetC`), ISO 8601 timestamps with timezone, human-readable enum strings, and grind expressed explicitly as an object `{"mode": "inherited"|"pinned", "value": <string>}` plus the resolved effective value, so a client never guesses where grind lives. Inherited grind SHALL resolve from the recipe's linked bag. Recipe responses SHALL expose the linked bag (`bagId` plus its display identity) and a human-readable staleness indication when the linked bag is no longer in inventory; `recipe_create` and `recipe_update` SHALL accept `bagId`. The optional hot-water block SHALL be accepted on create/update and returned on read via a tool schema that mirrors the steam block's pass-through handling — the same block the recipe stores, with each field's unit documented in its schema description (as the steam block does for `flow`), and with an `order` of `before` (long black) or `after` (Americano).

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

### Requirement: Recipe tools carry drink type and accept profile-less hot-water recipes
The recipe tool family SHALL expose `drinkType` (human-readable string per the data conventions) on `recipe_list` and `recipe_get`, and accept it on `recipe_create`/`recipe_update` (derived from blocks when omitted; re-derived on update only when blocks change and the caller did not set it). Derivation SHALL resolve an installed profile's `beverage_type` from the profile catalog — recipes referencing installed profiles embed no profile JSON, and without the catalog a tea profile would derive as espresso. `recipe_create` and `recipe_update` SHALL accept a recipe with no profile when the payload carries a hot-water block with `hasWater` true, and SHALL reject a profile-less payload without one. `recipe_activate` on a profile-less recipe SHALL follow the shared profile-less activation path.

#### Scenario: Create hot-water tea recipe via MCP
- **WHEN** an MCP client calls `recipe_create` with a name, a tea bean link, and a hot-water block but no profile
- **THEN** the recipe is created and `recipe_get` returns it with `drinkType` reflecting hot-water tea

#### Scenario: Profile-less without hot water rejected
- **WHEN** an MCP client calls `recipe_create` with no profile and no hot-water block
- **THEN** the tool returns a validation error naming the rule

### Requirement: Bag tools expose kind
`bag_list` and bag detail payloads SHALL include the bag's `kind` (`"coffee"` or `"tea"`); `bag_update` SHALL NOT accept changing it (kind is set at creation). Tea bags' structured brewing fields (teaType, brewTempC, leafGramsPer100Ml, steepTime) SHALL appear in bag payloads following the data conventions (units in field names), and `bag_update` SHALL reject tea-vocabulary writes on a coffee bag with an error naming the rule (never a silent drop).

#### Scenario: Tea bag in bag_list
- **WHEN** an MCP client calls `bag_list` with a tea bag in inventory
- **THEN** the bag carries kind "tea" and its stated brewing fields

#### Scenario: Kind is immutable via MCP
- **WHEN** an MCP client calls `bag_update` attempting to change kind
- **THEN** the update is rejected or the field ignored with the response noting kind is creation-time only

### Requirement: Bags are creatable via MCP with kind stamped at creation
The MCP server SHALL provide `bag_create` (write access level): kind `coffee` (default) or `tea`, at least one of roasterName/coffeeName required, kind-gated vocabularies in both directions (tea fields rejected on coffee creates; roastLevel/grinderSetting rejected on tea creates), details landing in the bag blob via the shared merge helper. The created bag SHALL enter the inventory but SHALL NOT become the active bag (a remote client must not silently switch what the next shot is pulled with; `bag_select` activates).

#### Scenario: Create a tea bag with brewing data
- **WHEN** an MCP client calls `bag_create` with kind "tea", a brand/name, teaType, and brewTempC
- **THEN** the bag appears in `bag_list` with kind "tea" and its brewing fields, and the active bag is unchanged

#### Scenario: Kind-gated create
- **WHEN** an MCP client calls `bag_create` with kind "coffee" and a teaType
- **THEN** the create is rejected with an error naming the tea-only fields

### Requirement: Page extraction is drivable and diagnosable via MCP
The MCP server SHALL provide `bag_extract_details` (control tier): runs the exact in-app two-stage extraction for a bag's product URL (kind selects the coffee/tea vocabulary) and returns the extracted fields WITHOUT writing them, plus diagnostics — which stage ran, the stage-1 failure when the fallback fired, the provider and model, and the fetched text size. Applying fields is the caller's explicit `bag_update`.

#### Scenario: Stage-2 diagnosis on a JS-rendered shop
- **WHEN** an MCP client calls `bag_extract_details` for a bag whose page is a JS-rendered SPA
- **THEN** the response shows stage 2, the stage-1 emptyPage error, and the extracted fields including imageUrl when present

### Requirement: Capability-URL Authorization for Remote Access

When remote MCP is enabled, the remote surface SHALL authorize
requests solely by an unguessable capability token carried as a URL
path segment (`/mcp/<token>`), where the token is a 128-bit
cryptographically random value generated on-device. Token comparison
SHALL be constant-time. Requests with a missing or non-matching token
SHALL receive a bare HTTP `404` that does not reveal that an MCP
server exists.

#### Scenario: Valid token
- **WHEN** a client POSTs a JSON-RPC request to `/mcp/<token>` with the current token
- **THEN** the request is dispatched to the MCP server and handled normally

#### Scenario: Wrong token
- **WHEN** a client POSTs to `/mcp/<other>` where `<other>` is not the current token
- **THEN** the server returns `404` with no MCP-identifying headers or body

#### Scenario: Missing token
- **WHEN** a client POSTs to `/mcp` on the remote surface
- **THEN** the server returns `404`

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

In Tailscale mode, the app SHALL run an embedded tsnet node
(userspace, no system VPN interface) that joins the user's tailnet
and exposes the remote surface via Tailscale Funnel at a stable
`https://<node>.<tailnet>.ts.net` URL with a certificate managed by
Tailscale. Setup SHALL surface the tsnet login URL (link and QR) and
any required Funnel-approval URL. Disabling remote MCP SHALL bring
the tsnet listener down.

The app SHALL provide an explicit **sign-out (forget) action** in the
settings UI that wipes the tsnet node state directory. The action
SHALL be reachable whenever a tsnet node exists in Tailscale mode —
including while the node is unauthorized and waiting for login (the
state in which a stale nodekey produces an unrecoverable login loop),
not only when the connector is active. The action SHALL be
confirmation-gated, warning that it clears this device's tailnet
identity and that re-enabling requires a fresh login. After the action
runs, re-enabling Tailscale mode SHALL bring the node up with no prior
identity and issue a fresh tailnet login URL rather than reusing the
wiped nodekey.

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

The MCP server SHALL treat a session as **stateful** only once the client establishes a Server-Sent Events (SSE) stream for that session (a `GET /mcp` that succeeds and is retained for notifications). A session created solely by a `POST` `initialize` handshake, with no subsequent SSE stream, SHALL be treated as **ephemeral** and SHALL NOT retain durable server-side state beyond what is needed to answer the requests on its own connection.

Access-level, confirmation-level, origin, protocol-version, and rate-limit gating SHALL apply identically to ephemeral and stateful sessions; this requirement changes only session retention, not the security surface.

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

Because ephemeral sessions are no longer bounded by the stateful concurrency limit and the `initialize` handshake is not rate-limited, the server SHALL enforce an absolute backstop on the total number of retained sessions to bound memory. When creating a session would exceed that backstop, the server SHALL evict the least-recently-active ephemeral session (never a stateful session, and never one holding a pending in-app confirmation) rather than rejecting the new session. A burst of per-request re-initialization SHALL NOT cause any client's request to be rejected.

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
`debug_get_log` and `shots_get_debug_log` SHALL accept an optional `filter` string parameter and an optional `regex` boolean parameter. When `filter` is provided, only lines matching it are eligible for pagination/tail; matching is case-insensitive substring containment by default, or a case-insensitive regular expression match when `regex` is `true`. Filtering SHALL be applied before offset/limit or tail is applied.

#### Scenario: Substring filter narrows an app-log request
- **WHEN** an MCP client calls `debug_get_log` with `filter: "R2 error"`
- **THEN** the response's `log`/`lines` contain only lines whose text contains "R2 error" (case-insensitive), and `returnedLines`/pagination fields are computed against the filtered set, not the full log

#### Scenario: Regex filter on a shot debug log
- **WHEN** an MCP client calls `shots_get_debug_log` with `shotId`, `filter: "SAW.*trigger"`, `regex: true`
- **THEN** the response contains only lines matching that pattern

#### Scenario: No filter reproduces existing behavior
- **WHEN** an MCP client calls either tool without a `filter` parameter
- **THEN** the response is identical in shape and content to the tool's behavior before this change

### Requirement: App debug log supports a minimum-severity filter
`debug_get_log` SHALL accept an optional `minLevel` parameter (`"DEBUG" | "INFO" | "WARN" | "ERROR" | "FATAL"`, ordered ascending) that restricts returned lines to that level or higher, based on the level tag already present on every persisted log line. `minLevel` SHALL combine with `filter` (a line must satisfy both to be returned). An unrecognized `minLevel` value SHALL be rejected with an `{"error": ...}` response rather than silently matching every line. `shots_get_debug_log` SHALL accept `minLevel` without error but ignore it, since the shot debug log carries no level tagging.

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
`debug_get_log` and `shots_get_debug_log` SHALL accept an optional `tail` integer parameter. When its value (after clamping negatives to zero) is greater than zero, the response contains the last `tail` qualifying lines (after any `filter`/`minLevel` is applied) of the addressed range — the whole log, the addressed session, or the shot's debug log — without requiring a prior call to determine the total line count, and `hasMore` SHALL be reported as `false`. When both a positive `tail` and `offset` are supplied, `tail` SHALL take precedence and `offset` SHALL be ignored. A `tail` of zero or a negative value SHALL be treated identically to `tail` being omitted — in particular, `hasMore` SHALL continue to reflect whether more qualifying lines exist beyond the returned page, not be forced to `false`.

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
`debug_get_log` and `shots_get_debug_log` SHALL accept an optional `dedupe` boolean parameter. When `true`, consecutive lines within the already-filtered/leveled candidate list that are identical once each line's own leading `[<elapsed>]` timestamp field is stripped SHALL be collapsed into a single entry, applied before `tail`/`offset`/`limit`. Each collapsed entry in the `lines` array SHALL carry `count` (the number of consecutive occurrences collapsed) and `lastLine` (the absolute line number of the last occurrence), in addition to the existing `line`/`text` (describing the first occurrence). Non-consecutive occurrences of the same message elsewhere in the addressed range SHALL NOT be collapsed together. When `dedupe` is omitted or `false`, `count`/`lastLine` SHALL NOT appear and the response is unaffected.

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

