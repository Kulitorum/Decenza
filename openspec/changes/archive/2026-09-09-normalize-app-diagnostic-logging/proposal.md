## Why

The 6–9 September DE1 MCP capture contains 3,039 of 5,240 timestamped lines without a registered subsystem marker. AI failures and successes have identical main-log messages, while Qt warning context is discarded, so a subsystem or severity query can look complete while omitting the explanation for a failed action.

## What Changes

- Complete the existing marker convention across first-party runtime logging in C++, headers, QML and platform code. Reuse existing subsystems where the diagnostic question matches; register missing ones and apply their markers through shared helpers.
- Make every persisted physical message line independently readable: existing elapsed-time/severity prefix, one registered subsystem marker, an emitter tag when needed, and available source context for warnings.
- Preserve Qt/framework messages under a registered runtime-diagnostic fallback with their original severity and available category/file/line. Do not infer ownership from message wording or use the fallback to excuse unconverted app call sites.
- Record explicit AI and bag-lookup outcomes in the main log: operation, request correlation, relevant bag/shot, provider/model when invoked, stage, elapsed time and a bounded reason or result count. A response file being written does not mean the operation succeeded.
- Correct misleading severity and wording at migrated sites, including benign R2 status reported at WARN, update-check failures hidden at DEBUG, and sentinel values printed as apparently valid measurements. Keep routine telemetry at DEBUG and preserve repeat collapsing.
- Extend the existing build-free logging gate to cover the remaining first-party emitters and QML helper usage. Require an evidence-backed runtime census and regression checks, including unformatted messages and multiline payloads.

The scope is diagnostic output and its enforcement. It does not change bag recovery, provider selection/spending, charging policy, brewing decisions, database behavior or log retention. Existing registered marker names, MCP arguments and historical log readability remain compatible; searches for legacy class prefixes must migrate to registered markers for new sessions.

## Capabilities

### New Capabilities

- `ai-operation-logging`: Correlated, truthful main-log outcomes for advisor, product-page search and bag extraction, including local fetch and response-parsing failures.
- `runtime-log-context`: Preservation of supplied runtime diagnostic context and independently filterable physical lines, including framework messages and multiline diagnostics.

### Modified Capabilities

- `log-tagging-convention`: Apply the marker/helper contract to all first-party runtime emitters, extend source enforcement across languages, and make completed scope and deliberate exceptions verifiable.

## Impact

- Shared logging: `src/core/logtags.h`, subsystem helper headers, `src/network/webdebuglogger.{h,cpp}`, the existing QML singleton registration and call sites.
- Emitters: AI, bag extraction, battery, memory/FD diagnostics, history/backups, profiles/recipes, steam/machine events, Visualizer, ShotServer/MQTT, startup/settings/UI and platform shims. Static inventory must include dormant failure branches absent from this capture.
- Validation: `scripts/check_log_markers.py`, its existing pull-request job, relevant existing Qt tests, and fresh current-session MCP queries. No new logging service, log file, MCP tool or dependency.
- Documentation: logging guidance, affected marker catalog descriptions and a short manual clarification for diagnostic log interpretation.
