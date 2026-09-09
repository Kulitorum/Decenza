## Why

The complete DE1 week contains 9,342 physical lines. Automatic FD inventories alone occupy 2,052 lines, without establishing an FD or socket leak. Routine battery, memory, shot-sample and file-write messages obscure the failures the user needs an AI to investigate. The user's clarified goal is fewer, clearer existing diagnostics, not more operation lifecycle events.

## What Changes

- Remove speculative automatic FD dumps. Preserve the independent, on-demand `debug_get_fds` MCP endpoint.
- Log memory only when sampled history shows sustained growth; keep live values, peak accounting and sample/class snapshots available on demand.
- Make battery and healthy scale repetition use the existing suppression utility with meaningful state/progress keys. Remove extraction-color traces and routine settling samples while retaining actual stop, fault, interrupted stability and final-result evidence.
- Trim PR #1930's AI start/dispatch/response chatter and successful diagnostic-file receipts. Preserve its useful correlated terminal result, provider selection, page/archive status and retry evidence.
- Replace existing Visualizer body dumps and selected error prose with bounded IDs/statuses in the existing messages. Remove this PR's added Visualizer lifecycle infrastructure.
- Preserve registered owner/source formatting, multiline attribution and useful warning source locations. Omit redundant function signatures where file and line already locate a warning.
- Suppress repeating connection failures and forecast results through LogCollapse; remove duplicate Steam UI state and auto-load invocation receipts. Keep actual timer commands and steam-scaling decisions.
- Record the full-week value/noise audit and review of PR #1930. Hold beta builds, updated-device validation and queued wiki publication as already requested.

## Capabilities

### New Capabilities

- `quiet-app-diagnostics`: Existing application diagnostics favor actionable changes over routine snapshots, with source-level suppression and retained on-demand evidence.

### Modified Capabilities

- `ai-operation-logging`: Carry context to the terminal outcome without automatically logging every stage or successful file receipt.

## Impact

Logging-only edits across memory, battery, shot timing, AI, Visualizer and the shared capture handler; targeted regressions and logging documentation. No request, retry, storage, charging or machine-control policy change. MQTT is intentionally excluded. Historical logs are unchanged.
