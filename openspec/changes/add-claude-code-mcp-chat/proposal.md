# Change: Add Subscription AI Chat as a Parallel AI Mode

## Why
Users want a conversational assistant that can use Decenza MCP data without requiring API tokens in the app, by authenticating through their existing AI subscription. The current AI Dialing Assistant is API-key based and should remain available as-is. We need a second, parallel mode — a persistent chat backed by a bridge service — that works with subscription-authenticated providers (Claude Code, Gemini) while preserving existing MCP safety controls.

## What Changes
- Add a new **parallel assistant mode**: "AI Chat (Bridge)" alongside the existing API-key "AI Dialing Assistant"
- Keep current `AIManager` provider flow unchanged for OpenAI/Anthropic/Gemini/OpenRouter/Ollama API usage
- Add a **Bridge Client** in Decenza that connects to an external bridge service over HTTPS/WebSocket — the bridge owns the AI runtime and authenticates with the provider; Decenza only knows the bridge contract
- **Implementation recommendation:** leverage existing Decenza building blocks (network transport patterns, MCP safety/confirmation pipeline, and conversation persistence patterns) rather than building a fully greenfield bridge stack
- Add a dedicated **chat session UI** (streaming responses, reconnect state, and session lifecycle)
- Add **Bridge settings** in AI settings (bridge URL, pairing/auth token, provider label, connection test, session status)
- Route bridge mode context/tooling through Decenza's existing MCP server and permission model
- Preserve existing destructive-action protections (MCP access levels, in-app confirmations, rate limits)
- Provide clear fallback behavior: if bridge is unavailable, API-key Dialing Assistant still works unchanged
- Sessions are **durable, named conversations** representing a dialing-in exploration of a specific bag of coffee — they persist across days or weeks of use, survive app/bridge restarts, and are listed so the user can resume any past conversation
- Add a **session list UI**: users can name a session (e.g. "Ethiopia Yirgacheffe, April 2026"), browse past sessions, and resume from where they left off

## Supported Providers (Bridge Side)

The bridge contract is provider-agnostic. Decenza does not know or care which AI model is behind the bridge. Bridge implementations are external services the user (or a future hosted offering) runs:

| Provider | Auth model | Status |
|---|---|---|
| **Claude Code** | Claude.ai subscription (Pro/Max) via OAuth | Primary target |
| **Gemini** | Google account OAuth (Gemini Advanced / Google One) | Second provider |
| **ChatGPT** | No subscription-based programmable interface exists today | Future — pending OpenAI shipping a Claude Code equivalent |

API-key providers (OpenAI, Anthropic, Gemini API, OpenRouter, Ollama) remain in the existing AI Dialing Assistant and are out of scope for this change.

## Bridge Project

The bridge service is a **separate GitHub project** and is out of scope for this Decenza change. This spec covers only the Decenza app-side client. The bridge project should be created as a new repository and should define its own spec covering:
- REST/SSE API contract (must match the contract defined in this spec's design.md)
- Claude Code SDK integration and session lifecycle
- Durable transcript storage
- Auth/pairing token flow
- Docker image and docker-compose setup (Phase 1)
- Native Mac and Windows installers (Phase 2)

**Deployment phases for the bridge:**
- **Phase 1:** Docker only (Docker Desktop on Mac/Windows, or any Linux host)
- **Phase 2:** Native Mac menu bar app and Windows service installer

## Impact
- Affected specs: `claude-code-mcp-chat` (new capability — rename to `subscription-ai-chat` in a follow-up if desired)
- Affected code (Decenza only):
  - `src/ai/` (new bridge chat orchestration classes, separate from `AIManager` provider path)
  - `src/network/` (bridge client transport and streaming event handling)
  - `src/core/settings.h/.cpp` (bridge endpoint/auth/session preferences)
  - `qml/pages/AISettingsPage.qml` (new bridge mode/settings section)
  - `qml/pages/` (new chat page/view model bindings)
  - `src/mcp/` (integration points only as needed; existing safety/confirmation behavior remains source of truth)
  - `CMakeLists.txt` (new files)
