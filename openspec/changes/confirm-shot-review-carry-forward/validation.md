# Validation and simplification review

## Environment and results

Checked on October 29, 2026, using the user's running macOS arm64 debug app,
Qt 6.12.0, and its packaged Wasm UI at `/`. Verified one process from this
checkout owned port 8888 before connecting. The assistant did not launch it.

- Native: corrected the October 28, 2:33 PM shot's bag while the Gran Bar recipe
  was active. The next setup stayed unchanged before acceptance. Acceptance
  relinked the recipe and kept it active at 18 g, grind 10, 1200 RPM, 84°C and a
  54 g target at 1:3, despite the corrected bag's different stored yield. Saved
  bag fields and the recipe's other editable fields were unchanged.
- Wasm: used the same picker and confirmation to restore the original bag.
  Stepping older waited for approval and then completed exactly one step.
  Changing the next target while an offer was visible withdrew it. Changing
  the target before leaving suppressed the entire offer. A simulator flush
  dismissed an offer without applying it; the saved shot edit remained.
- Restored the shot, bags, recipe's editable fields, and espresso, steam and DYE
  settings against the captured pre-test values. Recipe activation advanced its
  ordinary last-used timestamp. No external shot updates were sent.
- Qt Creator run `1793221417744`: seven affected CTest suites passed, with no
  failures, skips or warnings: shot-review actions, coffee bags, recipe wiring,
  recipe storage, operation completion, recipe context and shared widget HTML.
- After extracting the existing commit sequence, run `1793221417746` passed the
  coffee-bag suite, including injected recipe-write failure and cancellation
  before commit and before publication. It checks that no partial database
  patch remains, publication occurs once on success, and restoration preserves
  another editor's unrelated changes.
- Final native build `1793221414976` succeeded with zero errors or warnings.
  The build included QML lint (356/356 clean), Wasm lint/resource checks and a
  matching packaged Wasm client.

## Simplification decisions

Removed duplicate field allowlists: the approval-to-storage field mapping now
provides that single check. Removed a bean-selection forwarding function and
unnecessary checks for an always-declared page property. Kept the existing save
sequence in one testable helper, using one worker connection for commit and any
cancelled-publication restoration. The source retains ownership and main-thread
validation/publication; no new storage service or mutation framework was added.

Kept only the essential safeguards: the memory baseline, net-edit proposal,
request/session ownership, current-state checks, one database transaction and
field-scoped cancellation restoration. No per-shot database baseline, migration,
recovery journal, retry timer or persisted proposal was added.

Live inspection found a missing dialog background and stale local storage dates
after a bag correction; both were fixed. A small active-page check prevents a
late reply from popping a newer operation or child page. The unit check covers
that late-navigation case.

## Final live check

The three findings from the local review were fixed without adding a new state
manager: the existing proposal token remains invalidatable until apply finishes,
disconnect clears that view's carry-forward opening snapshot, and automatic bean
enrichment advances untouched opening fields. Focused run `1793221417747` passed
both affected suites. Removing the two QML fixes made their regression checks
fail in run `1793221417748`; they were restored before the final build.

Full Qt Creator run `1793221417749` passed all 207 CTest suites, with no failures,
skips or test warnings. Build `1793221414980` passed native/Wasm packaging and
lint; its three linker warnings concern the local Homebrew OpenSSL deployment
target and debug unwind-table size.

Awaiting the user to restart the rebuilt app to inspect the solid dialog
background and repeat one confirmed update on the final native/Wasm package.
The earlier native/Wasm behavior checks above passed; this last inspection is
not yet claimed as passed. No abrupt-process-termination recovery is provided.
