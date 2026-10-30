# Proposal

## Why

Selecting the first shot inserts a toolbar and moves the list under the user's eye. On the comparison page, the plot can occupy the viewport while the shot inputs needed to interpret it are below the fold.

## What Changes

- Keep the history list stationary when selection starts or ends, while retaining selection actions and search access at practical screen sizes.
- Show a compact, fixed set of commonly useful shot inputs alongside the comparison graph, tied clearly to the displayed shots. Keep the detailed comparison and graph inspection intact.
- Preserve selection through search and sort as currently implemented; do not change keyword parsing or sorting based on the issue's unreproduced report.
- Defer a customizable field picker: this is a useful possible preference, but the report alone does not establish that its complexity is warranted.

## Capabilities

### New Capabilities

- `history-shot-selection`: Stable, usable selection controls in Shot History.

### Modified Capabilities

- `shot-comparison`: Add a compact graph-adjacent summary of common shot inputs to the shared app page.

## Impact

Shared QML history and comparison pages and possibly the existing comparison data service; native and Wasm clients must match. The legacy web page and MCP comparison are not being redesigned. Update the user manual and verify the screens at tablet and narrow sizes.
