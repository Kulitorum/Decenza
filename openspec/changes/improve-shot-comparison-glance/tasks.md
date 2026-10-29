# Tasks

## 1. Stable history selection

- [x] 1.1 Reserve a compact responsive selection region in Shot History; verify first selection and clearing the last leave the list viewport and visible rows fixed at tablet and narrow widths.
- [x] 1.2 Verify search and sort remain reachable, selected IDs survive filtering, the selection count includes hidden selections, Compare needs two shots, and Delete still asks for confirmation; add focused regression coverage where practical.
- [x] 1.3 Update the wiki manual's Shot History selection guidance and verify it describes the resulting controls concisely.

## 2. Graph-adjacent summary

- [x] 2.1 Expose actual per-shot grind, dose and saved brew temperature through the shared comparison data path as needed; test missing values, tolerance-collapsed differences, rebasing and paging without changing comparison results.
- [x] 2.2 Add the compact accessible summary alongside the graph, using existing shot identities, swatches and formatting; verify it follows the displayed shots and base and labels brew temperature accurately.
- [x] 2.3 Verify graph scrubbing, readout, resizing and visibility controls remain usable with the summary at tablet and narrow widths, including long translations and larger text; add focused coverage for relevant behavior.
- [x] 2.4 Update the wiki manual's comparison guidance and verify it describes the summary and unavailable values concisely.

## 3. Shared integration

- [x] 3.1 Run the appropriate Qt Creator build and focused tests, including shared QML resource checks if new files are added; verify results are clean.
- [x] 3.2 With the user starting the app, inspect the history and comparison flows in native and host-served Wasm at tablet and narrow sizes, including search, selection, paging, rebasing and graph inspection; record any remaining limitations.

  Verification: native landscape and 1024×600 host-served Wasm; native selection, paging, graph inspection and resizing, and Wasm filtering, selection, paging, rebasing and inspection. Native rebasing was not separately exercised.
