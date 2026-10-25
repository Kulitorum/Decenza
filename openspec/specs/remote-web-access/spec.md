# remote-web-access Specification

## Purpose
Lets the owner open the Decenza web interface from anywhere, including a phone off the home network, through the device's embedded Tailscale Funnel. The unguessable capability URL works like the remote MCP connector.

## Requirements

### Requirement: Remote Web Access is a separate opt-in
The app SHALL provide a "Remote Web Access" switch that defaults off and is independent of remote MCP. Either SHALL be able to run without the other. The switch SHALL take effect only while the network server is enabled, and SHALL show as unavailable in builds without the embedded Tailscale node.

#### Scenario: Web on, remote MCP off
- **WHEN** the user enables Remote Web Access while remote MCP is disabled
- **THEN** the Tailscale node starts and the web route is published, and no MCP route is served remotely

#### Scenario: Remote MCP on, web off
- **WHEN** remote MCP runs in Tailscale mode and the web switch is off
- **THEN** the public host serves only the MCP connector route, as before this change

#### Scenario: Network server disabled
- **WHEN** "Enable Network" is off
- **THEN** the web route is not served and the card explains that the web interface needs the network enabled

#### Scenario: Build without Tailscale
- **WHEN** the app was built without the embedded Tailscale node
- **THEN** the switch is shown disabled with "Not included in this build"

### Requirement: Capability URL for the remote web interface
The web route SHALL be authorized by its own 128-bit random token, generated on the device and separate from the MCP token. The token SHALL be compared in constant time, and the safe URL SHALL be `https://<node>.<tailnet>.ts.net/web/<token>`. A request with no valid token and no web session established by one SHALL be refused, with the same silent drop and failed-token rate limit as the MCP route.

#### Scenario: Valid safe URL
- **WHEN** a browser opens the safe URL with the current web token
- **THEN** it is admitted and lands on the web interface at the public host's root

#### Scenario: Wrong or missing token
- **WHEN** a request arrives for `/web/<other>`, `/web`, or `/` without a valid web session
- **THEN** the connection is closed with no bytes written

#### Scenario: Tokens are not interchangeable
- **WHEN** the MCP token is used on `/web/<token>`, or the web token is used on `/mcp/<token>`
- **THEN** the request is refused as a wrong token

### Requirement: Same-origin web session after admission
Admission by the safe URL SHALL establish a browser session bound to the public host. The interface, its assets, its API requests and its live-update connection SHALL then work from that host's root, so the interface's same-origin checks keep holding. The session SHALL be HTTPS-only and SHALL NOT be readable by page scripts.

#### Scenario: Live updates over the public host
- **WHEN** an admitted browser loads the web interface over the public host
- **THEN** assets, API requests and the live session connect to that same origin and the interface becomes interactive

#### Scenario: Session outlives the address bar
- **WHEN** the admitted browser reloads the page at the public root
- **THEN** it stays admitted without the token in the address bar

### Requirement: Authenticator gating applies remotely
The remote web interface SHALL honour "Require authenticator code". While it is on, an admitted browser SHALL complete the authenticator sign-in before any protected data or command is available. While it is off, the safe URL alone SHALL grant access. The card SHALL state that next to the QR code.

#### Scenario: Authenticator required
- **WHEN** "Require authenticator code" is on and a browser opens the safe URL
- **THEN** it is asked for the authenticator code before the interface shows any data

#### Scenario: Authenticator off
- **WHEN** "Require authenticator code" is off
- **THEN** the safe URL opens the interface directly, and the Network card warns that anyone with the link can control the machine

### Requirement: Remote web route reaches only the web interface
The web route SHALL forward only the web interface entry and assets, the authentication routes, and the session and API routes the web interface uses. Device-migration endpoints, `/legacy/` HTML and the local `/mcp` endpoint SHALL NOT be reachable through it.

#### Scenario: Migration endpoint over the public host
- **WHEN** an admitted browser requests a device-migration endpoint over the public host
- **THEN** the request returns `404`

#### Scenario: Legacy HTML over the public host
- **WHEN** an admitted browser requests a `/legacy/` page over the public host
- **THEN** the request returns `404`

### Requirement: Legacy web interface is never served remotely
Remote Web Access SHALL serve only the current web interface. No part of the legacy HTML interface SHALL be reachable over the public host: no page, asset, bookmark redirect or alias into `/legacy/`, and no API route used only by legacy pages. The safe URL, the Network card and the remote interface SHALL NOT link to or offer the legacy interface. The legacy interface stays available on the LAN as before.

#### Scenario: Old bookmark path over the public host
- **WHEN** an admitted browser requests a pre-`/legacy/` page path that the LAN redirects into `/legacy/`
- **THEN** the request returns `404` and no redirect is issued

#### Scenario: Legacy asset over the public host
- **WHEN** an admitted browser requests a stylesheet, script or image that only the legacy pages use
- **THEN** the request returns `404`

#### Scenario: Legacy-only API over the public host
- **WHEN** an admitted browser calls an API route that only the legacy pages use
- **THEN** the request returns `404`

#### Scenario: LAN unchanged
- **WHEN** a LAN browser opens `/legacy/`
- **THEN** the legacy interface works as before

### Requirement: Safe URL and QR code
When the web route is active, the Network card SHALL display the safe URL with a copy action and a QR code that opens it on a phone. While the route is not reachable, the card SHALL NOT present the URL as usable.

#### Scenario: Open from a phone
- **WHEN** the user scans the QR code with a phone
- **THEN** the phone's browser opens the safe URL and reaches the interface, or the authenticator sign-in when that is required

#### Scenario: Funnel still publishing
- **WHEN** the Tailscale node is up but the public address does not answer yet
- **THEN** the card shows the publishing status instead of a usable URL

### Requirement: Web token rotation revokes access
The Network card SHALL provide a confirmation-gated rotate action for the web token. Rotation SHALL invalidate the previous safe URL and every web session established through it, and SHALL leave the MCP token and its sessions untouched.

#### Scenario: Rotate the web token
- **WHEN** the user confirms rotation
- **THEN** the old safe URL is refused, browsers admitted with it lose their session, and the card shows the new URL and QR code

#### Scenario: MCP unaffected by web rotation
- **WHEN** the web token is rotated
- **THEN** a connected remote MCP client keeps working with its existing connector URL

### Requirement: Disabling the web route
Turning the web switch off SHALL stop serving the web route and end remote web sessions. It SHALL stop the Tailscale node only when remote MCP does not need it. The web token SHALL be kept so the same safe URL works again when the switch is turned back on.

#### Scenario: Toggle off with remote MCP still on
- **WHEN** the user turns the web switch off while remote MCP uses Tailscale
- **THEN** remote web sessions end, `/web/` requests are refused, and the MCP connector keeps working

#### Scenario: Re-enable
- **WHEN** the user turns the web switch back on
- **THEN** the previous safe URL works again
