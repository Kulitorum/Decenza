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

### Requirement: The MCP card is a guided setup for the user's AI app
The MCP card SHALL start with "Your AI app", which also sets what Discuss opens, then a "Ready to connect" checklist and "What AI apps can do". Once Remote Access is on, it SHALL show "Connect it": the link with its QR code or what it is waiting for, numbered steps for both Claude and ChatGPT whose copy and open steps are buttons that tick when done, and a new-link action. An app that cannot take a connector SHALL see that Discuss pastes the shot.

#### Scenario: Claude chosen, network off
- **WHEN** the user's AI app is Claude and Enable Network is off
- **THEN** the checklist marks Enable Network not done, with a button that opens Connections, and no connection steps are shown

#### Scenario: Steps before the link answers
- **WHEN** Remote Access is on and Tailscale is still connecting
- **THEN** "Connect it" shows both apps' steps, says the link appears once Tailscale is connected, and keeps the copy buttons disabled

#### Scenario: ChatGPT steps
- **WHEN** the link is ready
- **THEN** ChatGPT's steps cover the Plugins page, Add custom MCP server (or Create MCP App, with where to turn on Developer mode if neither is there), No authentication, Create as a plugin, and using it with @; its open button goes to ChatGPT's plugins page

#### Scenario: Network missing
- **WHEN** Enable Network is off
- **THEN** that row is not done and offers a button that opens Connections

### Requirement: One choice for what AI apps can do
"What AI apps can do" SHALL offer Look only, Ask before acting and Full control, each setting access level and confirmation together. Any other combination SHALL read as custom, set in Advanced options.

#### Scenario: One choice sets both levels
- **WHEN** the user picks "Ask before acting"
- **THEN** access level becomes Control and confirmation becomes Dangerous Only

#### Scenario: Turning on Remote Access picks Tailscale
- **WHEN** the user turns on Remote Access with no custom URL set, in a build with Tailscale
- **THEN** the connection mode is set to Tailscale

### Requirement: Advanced AI app options are out of the way
The separate access level and confirmation lists, the connection mode with a custom URL, and local network MCP with its "Enable MCP Server" switch SHALL be in a dialog opened from a small "Advanced…" link at the end of the MCP card. Settings search SHALL still find it, as "Advanced AI app options", under those terms.

#### Scenario: Not on the page
- **WHEN** user opens the AI tab
- **THEN** none of those controls are shown, only the "Advanced…" link

#### Scenario: Found by search
- **WHEN** user searches settings for "confirmation", "custom url", "claude desktop" or "enable mcp server"
- **THEN** the result opens the AI tab at the "Advanced…" link

### Requirement: Discuss pastes the shot unless the app reads it
The Discuss button on shot review SHALL copy the shot to the clipboard unless the chosen app reads it itself: the Claude apps and ChatGPT through a live remote connector, or Claude Desktop through the local server. Turning the MCP server on SHALL NOT, by itself, stop the copy.

#### Scenario: MCP on, no connector
- **WHEN** the MCP server is on, Remote Access is not set up, and the user taps Discuss with Claude chosen
- **THEN** the shot is copied to the clipboard and Claude opens

#### Scenario: App without connectors
- **WHEN** Gemini is chosen and everything is on
- **THEN** the shot is copied to the clipboard

#### Scenario: Connector live
- **WHEN** ChatGPT is chosen and its remote connector is live
- **THEN** ChatGPT opens without the shot being copied
