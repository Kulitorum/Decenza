## ADDED Requirements

### Requirement: Parallel Assistant Modes
The app SHALL provide two assistant modes in parallel:
1. API-key-based Dialing Assistant (existing behavior)
2. AI Chat (Bridge) mode — subscription-authenticated, provider-agnostic (new behavior)

Selecting AI Chat (Bridge) mode SHALL NOT alter API-key provider configuration or behavior for the existing Dialing Assistant.

#### Scenario: User keeps using API-key assistant
- **WHEN** the user does not enable AI Chat (Bridge) mode
- **THEN** the existing AI Dialing Assistant continues to function exactly as before
- **AND** provider selection and API key flows remain unchanged

#### Scenario: User switches to bridge chat mode
- **WHEN** the user selects AI Chat (Bridge) mode
- **THEN** the app routes new chat interactions through the configured bridge session path
- **AND** the API-key assistant remains available for later use

### Requirement: Bridge Session Connectivity
The app SHALL establish chat sessions through a configurable external bridge endpoint, including health-check, session creation, message send, streamed response events, and session close. The bridge contract is provider-agnostic — the app does not need to know which AI model is behind the bridge.

#### Scenario: Bridge health check succeeds
- **WHEN** the user configures a valid bridge URL/token and taps test connection
- **THEN** the app shows bridge connectivity as available
- **AND** the user can start a chat session

#### Scenario: Bridge unavailable
- **WHEN** the bridge endpoint cannot be reached or authentication fails
- **THEN** the app shows a clear error state with retry guidance
- **AND** API-key Dialing Assistant remains usable

#### Scenario: User connects a Gemini bridge
- **WHEN** the user points the bridge URL at a Gemini bridge (Google OAuth, Gemini Advanced)
- **THEN** the app connects and operates identically to a Claude Code bridge
- **AND** no app changes are required to support a different bridge provider

### Requirement: MCP-Backed Conversation Context
Claude Code chat mode SHALL use Decenza MCP tools/resources as the authoritative context source for machine state, profile data, and shot history.

#### Scenario: Claude conversation reads MCP context
- **WHEN** the user asks a question requiring current machine/profile/shot context
- **THEN** the Claude mode session obtains relevant MCP context before or during response generation
- **AND** the assistant response reflects current Decenza state

#### Scenario: Tool call results appear in chat timeline
- **WHEN** Claude mode performs MCP tool calls during a turn
- **THEN** the app records tool activity in the conversation timeline
- **AND** final assistant output includes the effect of those tool calls

### Requirement: MCP Safety Controls Are Preserved
Claude Code chat mode SHALL respect existing MCP safety controls, including access levels, confirmation gates, and rate limits.

#### Scenario: Dangerous action requires confirmation
- **WHEN** Claude mode requests an MCP action that requires in-app confirmation
- **THEN** the existing confirmation flow is shown on the app
- **AND** the action does not execute unless accepted

#### Scenario: Access level restriction blocks action
- **WHEN** MCP access level does not permit a requested tool category
- **THEN** the tool call is rejected using existing MCP enforcement
- **AND** the chat surfaces a clear permission-related error

### Requirement: Cross-Platform Remote-First Behavior
Claude Code mode SHALL be available on desktop, Android, and iOS via bridge connectivity without requiring on-device terminal execution.

#### Scenario: Mobile device starts Claude mode session
- **WHEN** user starts Claude mode chat on Android or iOS
- **THEN** the app uses remote bridge connectivity for session execution
- **AND** no local shell/PTY runtime is required on device

### Requirement: Durable Named Sessions
Claude mode sessions SHALL persist across app restarts, bridge restarts, and network interruptions. Sessions SHALL remain available until explicitly deleted by the user.

#### Scenario: User resumes a session days later
- **WHEN** the user opens the session list and selects a session created days or weeks ago
- **THEN** the app connects to that session on the bridge and renders the prior conversation
- **AND** the assistant maintains continuity with earlier turns, including earlier grind and recipe decisions

#### Scenario: App restart does not lose session
- **WHEN** the user closes and reopens the app
- **THEN** the session list is available and the most recently active session can be resumed
- **AND** no messages or session metadata are lost

#### Scenario: Bridge restart does not lose session
- **WHEN** the bridge service is restarted
- **THEN** previously created sessions remain available with their full transcript
- **AND** the user can resume any session as normal

### Requirement: Session List and Naming
The app SHALL provide a session list that allows users to create, rename, browse, resume, and delete sessions.

#### Scenario: User names a new session
- **WHEN** the user creates a new session
- **THEN** the app prompts for an optional session name (e.g. "Ethiopia Yirgacheffe, April 2026")
- **AND** the session is listed under that name, defaulting to creation timestamp if no name is provided

#### Scenario: User renames an existing session
- **WHEN** the user selects rename on a session in the list
- **THEN** the session name is updated in both the app and bridge
- **AND** the updated name is shown in the session list immediately

#### Scenario: User resumes a specific session
- **WHEN** the user taps a session in the session list
- **THEN** the chat view opens at the end of that session's conversation
- **AND** new messages are appended to the existing transcript

#### Scenario: User deletes a session
- **WHEN** the user explicitly deletes a session from the list
- **THEN** the session and its transcript are permanently removed
- **AND** a confirmation is shown before deletion

### Requirement: Context Bootstrapping
At the start of each session or turn, the app SHALL supply a compact context bundle to the bridge so that the assistant has grounded awareness of current machine state, active profile, and recent shot history without requiring low-value discovery turns.

#### Scenario: First turn in a new session
- **WHEN** the user sends the first message in a new session
- **THEN** the bridge receives a context bundle including active profile summary, recent shot summary, and current MCP capability/access constraints
- **AND** the assistant response reflects current Decenza state without the user needing to describe it

### Requirement: Bridge Credential Separation
Bridge chat mode SHALL use bridge credentials (URL + pairing token) and SHALL NOT require storing AI provider API keys in Decenza. The app has no knowledge of which provider the bridge uses.

#### Scenario: User uses Claude subscription bridge
- **WHEN** user enables bridge mode with a Claude Code bridge URL and pairing token
- **THEN** bridge mode functions without entering Anthropic API keys into Decenza settings
- **AND** API-key assistant credentials remain optional and independent

#### Scenario: User switches bridge to a different provider
- **WHEN** user updates the bridge URL to point at a Gemini bridge (or any other compliant bridge)
- **THEN** the app connects using the same pairing token flow
- **AND** no Decenza settings beyond the bridge URL and token need to change
