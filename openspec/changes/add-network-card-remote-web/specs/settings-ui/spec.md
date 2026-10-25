# Spec Delta

## MODIFIED Requirements

### Requirement: Merged History and Data Tab
The app SHALL provide a single "History & Data" tab combining shot history access, DE1 import, backup/restore, the data summary, device migration, and factory reset. The tab SHALL NOT contain the network server toggle or authenticator controls; those live in the Connections tab's Network card. The tab SHALL use a three-column layout: Shot History (left), Backup (middle), Data (right). Device Migration SHALL be accessible via a button that opens a stepped dialog.

#### Scenario: No duplicate server toggle
- **WHEN** user searches for the network enable toggle
- **THEN** it exists in exactly one location (Connections tab, Network card)

#### Scenario: Data summary stays
- **WHEN** user opens the History & Data tab
- **THEN** the shot and profile counts are shown in the Data column

#### Scenario: Device Migration as dialog
- **WHEN** user taps "Import from Another Device"
- **THEN** a modal dialog opens with the search → auth → manifest → import workflow

#### Scenario: Factory Reset at bottom
- **WHEN** user looks for Factory Reset
- **THEN** it is at the bottom of the Data column, separated by a divider

## ADDED Requirements

### Requirement: Network card in Connections
The Connections tab SHALL show a Network card at the top of its left column, above the Machine card. It SHALL hold the "Enable Network" switch with the LAN address, the "Require authenticator code" switch with its set-up and reset actions, the Tailscale node controls, and the Remote Web Access controls.

#### Scenario: Card position
- **WHEN** user opens Settings → Connections
- **THEN** the Network card is the first card in the left column, with Machine directly below it

#### Scenario: Authenticator controls move with the server switch
- **WHEN** "Enable Network" is on
- **THEN** the Network card shows "Require authenticator code" and, when it is on, Set Up Authenticator or Reset Security

### Requirement: Enable Network label
The network server switch SHALL be labelled "Enable Network". The stored setting and its existing value SHALL be unchanged, so no user's server state changes on upgrade. The settings search SHALL find it under "Enable Network" as well as under the existing server keywords.

#### Scenario: Upgrade preserves state
- **WHEN** a user who had "Enable Server" on upgrades
- **THEN** "Enable Network" is on and the server is running

#### Scenario: Search by old wording
- **WHEN** user searches settings for "server"
- **THEN** the "Enable Network" switch is returned

### Requirement: Tailscale node controls live in the Network card
Tailscale sign-in (link and QR), the one-time Funnel setup, node status and Sign out SHALL appear in the Network card whenever remote MCP in Tailscale mode or Remote Web Access needs the node. They SHALL NOT appear on the AI tab.

#### Scenario: Sign-in prompted by the web switch
- **WHEN** user turns on Remote Web Access with no prior Tailscale identity
- **THEN** the Network card shows the Tailscale login link and QR code

#### Scenario: Sign-in prompted by remote MCP
- **WHEN** user enables remote MCP in Tailscale mode on the AI tab with no prior Tailscale identity
- **THEN** the login link and QR code appear in the Network card, and the AI tab points the user there

### Requirement: AI-specific remote controls stay on the AI tab
The AI tab SHALL keep the remote MCP switch, the reachability mode choice, the custom base URL, the MCP connector URL with copy and QR, and MCP token rotation. When Tailscale mode is selected it SHALL show the remote status and point to the Network card for sign-in and setup.

#### Scenario: Connector URL location
- **WHEN** remote MCP is active in Tailscale mode
- **THEN** the MCP connector URL and its QR code are shown on the AI tab, not in the Network card

### Requirement: AI tab separates the AI Advisor from outside AI apps
The AI tab SHALL show two cards. "AI Advisor Config" holds the AI Advisor's provider, API key and model, and states that the advisor is billed to that key. "Connect AI Apps (MCP)" holds everything for outside AI apps, and states that they use their own subscription, not that key.

#### Scenario: Which card uses the API key
- **WHEN** user opens the AI tab
- **THEN** the API key field is labelled "API Key (for AI Advisor)" in the AI Advisor Config card, and the MCP card says the key is not used

### Requirement: Remote Access is the recommended way to connect AI apps
In the MCP card, Remote Access SHALL follow the MCP switch and its access and confirmation levels, marked as recommended, with steps and a copy-and-open button for both Claude and ChatGPT. Discuss Shot SHALL follow it, and say it needs Remote Access while that is off. While the MCP server is off, the greyed Remote Access switch SHALL say why.

#### Scenario: Copy and open
- **WHEN** the connector link is ready and user taps "Copy link and open ChatGPT"
- **THEN** the link is on the clipboard and ChatGPT's settings open in the browser

#### Scenario: Discuss Shot without Remote Access
- **WHEN** Remote Access is off
- **THEN** the Discuss Shot section says it needs the MCP server and Remote Access turned on

### Requirement: Local network MCP is advanced and out of the way
The local, same-WiFi MCP setup (setup page link, its network requirement, listening status and setup guide) SHALL be shown only in a dialog opened from a small "Advanced…" link at the end of the MCP card. Settings search SHALL still find it as "Local network MCP (advanced)".

#### Scenario: Not on the page
- **WHEN** user opens the AI tab
- **THEN** no local MCP setup is shown, only the "Advanced…" link

#### Scenario: Found by search
- **WHEN** user searches settings for "local MCP" or "Claude Desktop"
- **THEN** the result opens the AI tab at the "Advanced…" link
