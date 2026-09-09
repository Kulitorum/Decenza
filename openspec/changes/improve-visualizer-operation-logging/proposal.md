## Why

PR #1930 makes first-party messages searchable by owner, but a tag alone cannot reveal an outcome that is silent or logged below the user's severity filter. Visualizer still hides terminal upload failures at DEBUG, emits several list/recovery failures only as signals, and writes remote response bodies into the main log.

## What Changes

- Make Visualizer upload, metadata update, connection test, shot listing, profile import, history recovery and coffee-management outcomes readable through `[Visualizer]` at INFO/WARN, with operation identity, stage and known shot/bag context.
- Keep retries and parsing details at DEBUG; make the final usable result explicit, including partial batches, expected skips, cancellation and capability limitations. An upload's success must remain distinct from later bag synchronization.
- Replace full request/response dumps and remote error prose in these diagnostics with bounded identifiers, counts, HTTP/network statuses and safe reason codes. Use shared formatting and source tags consistently.
- Audit the complete result paths and signal consumers in this area, including silent exits, and add focused regression coverage for the gaps. Keep other app-wide findings in a documented next-pass list.
- Carry forward the prior change's held beta compilation, updated-device charging capture and queued wiki publication. No beta build is dispatched while the user's hold remains in effect.

## Capabilities

### New Capabilities

- `visualizer-operation-logging`: Correlated, bounded and truthful Visualizer operation outcomes available from the persisted application log.

### Modified Capabilities

None. This specializes the existing log-tagging convention without changing its marker grammar or the AI operation contract.

## Impact

Primary code: `src/network/visualizeruploader.{cpp,h}`, `src/network/visualizerimporter.{cpp,h}`, and their result forwarding in `src/controllers/maincontroller.cpp`; shared diagnostic helpers where common formatting can be reused. Existing Visualizer/coffee-bag/logger test infrastructure, `docs/CLAUDE_MD/LOGGING.md`, `VISUALIZER.md` and the short wiki guidance are affected.

Keep `[Visualizer]` stable. Request payloads, retry budgets, routing, profile save semantics, database results, UI/MCP responses, service selection and machine control remain unchanged. No new dependency, network request or public MCP tool is required. MQTT is intentionally disabled and is excluded from the follow-up.
