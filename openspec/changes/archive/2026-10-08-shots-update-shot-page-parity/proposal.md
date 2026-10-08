## Why

MCP is meant to do what the app and web can do, but `shots_update` could not pick a shot's bag, set its equipment package or taste, or correct its freezer, thaw and opened dates. That left the Hometown portion-1 shots (task 2.5 of `auto-stamp-bag-opened-date`) uncorrectable.

## What Changes

- `shots_update` accepts `bagId` (the bag's snapshot is copied, as the bean picker does), `equipmentId`, `tasteBalance`/`tasteBody`, and `frozenDate`/`defrostDate`/`openedDate`/`storageHint`.
- Storage dates follow the bag rules (`CoffeeBag::writeError`) against the shot's own dates; bad taste values, unknown bags and unknown packages are refused with a reason.
- A `beanBase` edit also sets the indexed bean id, as the app's shot page does.
- McpSurfaceVersion 1.18.0.

## Impact

- `src/mcp/mcptools_write.cpp`, `src/history/shothistorystorage.{h,cpp}`, `src/mcp/mcpserver.h`, `docs/CLAUDE_MD/MCP_SERVER.md`, `tests/tst_mcptools_write.cpp`.
