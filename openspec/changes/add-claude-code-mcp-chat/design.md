## Context

Decenza already has two relevant building blocks:
- A mature API-key assistant pipeline (`AIManager` + provider classes)
- A local MCP server with access controls, rate limits, and in-app confirmation for dangerous actions

Users want a second assistant path that can run on Claude subscription authentication (via Claude Code login) while still using Decenza MCP tools/resources. This must be additive and must not break or replace the current API-key assistant.

## Goals / Non-Goals

- **Goals:**
  - Add a new Claude Code-based conversation mode without modifying existing API-key advice behavior
  - Allow chat turns to use Decenza MCP tools/resources and live machine context
  - Work across desktop, Android, and iOS with the same user-facing behavior
  - Preserve MCP safety model (access levels, rate limiting, in-app confirmations)
  - Provide robust reconnect and actionable error states

- **Non-Goals:**
  - Replacing or migrating the existing API-key Dialing Assistant
  - Embedding a full terminal/PTY in the app
  - Re-implementing Claude Code internals inside Decenza
  - Expanding MCP tool permissions beyond current settings

## Decisions

### Decision: Bridge Architecture (Provider-Agnostic External Runtime)

Decenza will not run any AI model directly on-device. Instead, Decenza connects to an external bridge service that hosts an AI session. The bridge authenticates with the AI provider using the user's subscription credentials; Decenza only knows the bridge URL and pairing token.

- Decenza role: chat UI, session control, transport client
- Bridge role: AI runtime orchestration, provider auth, streaming, transcript storage
- MCP role: Decenza remains the MCP server that exposes tools/resources

This is the only approach that is realistic for all platforms (especially iOS), and it keeps Decenza's codebase provider-agnostic — adding a new provider means deploying a different bridge, not changing the app.

**The bridge is a separate project.** It lives in its own GitHub repository, is versioned and released independently, and is outside the scope of this Decenza spec. This spec covers only the Decenza app-side client. The bridge project should define its own spec covering the REST/SSE contract, session storage, auth, and deployment.

**Bridge deployment — phased:**
- **Phase 1 (MVP):** Docker only. The bridge ships as a Docker image. Users run it with Docker Desktop (Mac or Windows) or on any Linux host. Setup is two commands after a one-time `claude auth login`. This is the only supported deployment in the first release.
- **Phase 2:** Native Mac app (menu bar) and Windows service installer. Removes the Docker Desktop dependency for users who find it unfamiliar.

**Supported bridge providers (subscription-based, not API keys):**
- **Claude Code** (primary): wraps the `claude` CLI or SDK, authenticates via Claude.ai OAuth (Pro/Max subscription)
- **Gemini** (second provider, future bridge release): authenticates via Google account OAuth; Gemini Advanced / Google One subscription covers usage without per-token billing
- **ChatGPT** (future): no subscription-authenticated programmable interface exists today; add when OpenAI ships an equivalent

**Important — Anthropic subscription coverage applies to first-party products only.** As of April 2025, Anthropic no longer covers third-party harnesses (e.g. OpenClaw) under Claude subscription limits — those require "extra usage" billing. Claude Code is a first-party Anthropic product and remains fully covered by Claude Pro/Max subscriptions. The Claude Code bridge **must** use the official `claude` CLI or SDK directly and must not be built on top of a third-party harness, or users will be billed beyond their subscription.

### Decision: Parallel Assistant Modes

Assistant selection is explicit:
- **AI Dialing Assistant (API keys)**: existing `AIManager` behavior — unchanged
- **AI Chat (Bridge)**: new bridge-backed session mode, subscription-authenticated

The two modes have separate settings, status, and error messaging. Failing one mode must not block the other.

### Decision: Session Contract Between App and Bridge

Define a minimal bridge contract:
- `GET /v1/health` for availability/auth status
- `POST /v1/sessions` to create a named chat session
- `GET /v1/sessions` to list all sessions (id, name, created, last active, message count)
- `POST /v1/sessions/{id}/messages` to submit user messages
- `GET /v1/sessions/{id}/events` (SSE/WS) for streaming assistant output, tool-call progress, and errors
- `GET /v1/sessions/{id}/messages` to fetch the transcript of a past session
- `PATCH /v1/sessions/{id}` to rename a session
- `DELETE /v1/sessions/{id}` to explicitly close/delete a session (user-initiated only)

The protocol must support resumable sessions and explicit disconnect/retry semantics. Session close is never automatic — sessions remain available until the user deletes them.

### Decision: Session Durability and Context Management

Sessions represent a dialing-in exploration of a specific bag of coffee and must persist across days or weeks of use. This drives several requirements:

**Durability**: The bridge stores the full conversation transcript durably (not in memory). On resume, the bridge restores session state so the user can continue a conversation started days earlier.

**Context window management**: A multi-week conversation with tool call results will exceed Claude's context window. The bridge is responsible for managing this. Recommended strategy: keep the full transcript in durable storage; send a sliding window of recent messages plus a rolling summary of earlier turns. The app does not need to implement or understand context windowing — it sends messages and receives responses. The bridge owns the strategy.

**Session naming**: Sessions are named by the user (defaulting to creation timestamp if unnamed). The name is the primary way users identify which bag of coffee a session belongs to. Names are editable at any time.

**One active session at a time**: The app supports one active (foreground) session at a time. Users switch between sessions via the session list. There is no concurrent multi-session execution in MVP.

**Session lifecycle**: Sessions are permanent until the user explicitly deletes them. Closing the app, losing connectivity, or restarting the bridge does not delete a session.

### Decision: MCP Safety as Source of Truth

All machine-affecting operations continue to be governed by Decenza MCP controls:
- Access level gates (`read`, `control`, `settings`)
- Existing confirmation requirements for dangerous actions
- Existing rate limiting behavior

The bridge/Claude mode cannot bypass these controls.

### Decision: Context Bootstrapping

At session start, Decenza supplies a compact context bundle to the bridge (or injects into the first turn), including:
- Active profile summary
- Recent shot summary
- Dial-in context resource snapshots as available
- Current MCP capability/access constraints

This reduces low-value discovery turns and keeps tool usage grounded.

### Decision: Credentials and Privacy

Decenza stores only bridge connection credentials (URL + pairing token or equivalent), not Anthropic API keys for Claude mode.

Logs and history for Claude mode should be locally reviewable and deletable, with clear labeling that they belong to bridge-backed sessions.

## Risks / Trade-offs

- **Bridge dependency:** Feature availability depends on reachable bridge runtime.
  - Mitigation: clear status UI, reconnect controls, fallback to API assistant.
- **Network variability on mobile:** intermittent connectivity may interrupt streaming.
  - Mitigation: resumable session IDs and explicit retry UX.
- **Tool overreach concerns:** users may worry Claude can control machine unexpectedly.
  - Mitigation: preserve existing MCP confirmation and access settings, and show tool activity in UI.

## Migration Plan

1. Introduce settings and UI scaffolding behind a feature flag.
2. Implement bridge health/session plumbing with mock transport.
3. Implement live streaming chat UI and persistence.
4. Integrate MCP context bootstrap and tool activity rendering.
5. Remove feature flag after cross-platform validation.

## Open Questions

- Should bridge discovery be manual URL-only for MVP, or include LAN discovery? (**MVP: manual URL only. LAN discovery is a future enhancement.**)
- Should session transcripts be kept in existing AI conversation storage or separate storage namespace? (**Separate namespace on bridge. The bridge owns transcript storage; the app only caches enough to render the current session view.**)
- Should multiple simultaneous Claude sessions be allowed, or enforce one active session per device? (**One active session at a time. Users switch sessions via the session list. Multiple concurrent sessions are a future enhancement.**)
