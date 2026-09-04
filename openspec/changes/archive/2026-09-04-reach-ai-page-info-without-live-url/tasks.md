## 1. Archive fallback in the extraction fetch

- [x] 1.1 In `BeanBaseClient::fetchPageText()`, on a reply error whose HTTP status is 404 or 410, call `fetchArchiveAvailability(url, …)` and, when it returns a snapshot, re-issue the fetch against `archiveRawForm(snapshot)`. Verified as two tests, not one: `parseArchiveSnapshot` upgrades the capture URL to https and the stub speaks plain HTTP, so the recovered fetch cannot land on it — `getInfoAsksTheArchiveWhenThePageIsGone` covers the ask and `getInfoReadsASnapshotInItsRawForm` the read.
- [x] 1.2 Keep every other transport error (timeout, DNS, TLS) on the existing failure path with no archive query, and verify with a test that a timed-out URL emits `pageTextFailed` and issues no availability request.
- [x] 1.3 Emit `pageTextFailed` with the original page error when the archive answers "no capture", and verify the status text names the page rather than the archive.
- [x] 1.4 Treat an archive fault (429, malformed envelope) as a fault, not a miss — surface the failure, never a claim the page is gone; verify with a test that a 429 from the stub produces `pageTextFailed` and no dead-marking side effect.
- [x] 1.5 Confirm the recursion is bounded: a snapshot URL that itself 404s must not re-enter the fallback. Verify with a test that the stub sees exactly two page fetches.

## 2. One usability rule in the bag editor

- [x] 2.1 Add a single computed property to `ChangeBeansDialog.qml` for "the field holds a URL not known dead" (blob `linkDead`, or a session `linkState` of `"dead"`; no fresh probe), and verify by reading it in both places that currently test `fLink.trim().length`.
- [x] 2.2 Change the Get info row's `visible` to: AI configured AND (URL usable OR `supportsProductPageSearch()`); verify on bag 133 that the action appears with the link field empty, and disappears when the AI provider is deselected.
- [x] 2.3 Change `maybeFindProductPage()`'s link guard from "field is empty" to "no usable URL"; verify a bag holding a known-dead URL reaches the search.
- [x] 2.4 Give the Get info button its no-URL mode: press runs the product-page search, reuses the existing confirmation row, and on confirmation chains into `fetchPageText` without a second press. Verify end to end on a bag with no link.
- [x] 2.5 Make an explicit press bypass the `aiPageSearched` marker while the automatic path still honours it; verify by pressing twice on a bag already marked searched and seeing two searches.
- [x] 2.6 Label the action for its mode (page vs find-then-read) through `TranslationManager`, with keys under `changebeans.form.getInfo.*`; verify no untranslated literal reaches the UI.

## 3. A changed link is checked again

- [x] 3.1 In `beanbase_blob.h`, add one helper that writes `link` and drops `linkChecked`/`linkDead` when the value differs from what the blob held; verify with a unit test that a same-value write leaves both marks in place.
- [x] 3.2 Route `revertToCanonical` through that helper so restoring the canonical URL re-opens the check; verify with a unit test that a revert onto a blob marked `linkChecked` clears the mark when the URL changes.
- [x] 3.3 Route the editor's save path and the AI-suggestion accept through the same helper; verify no writer sets `link` directly outside it.
- [x] 3.4 Key `m_linkValidated` and `m_archiveAttempted` on the bag AND the URL rather than the bag alone; verify with a test that a second `validateBagLink` for a different URL on the same bag issues a request, and that a repeat of the same URL does not.
- [x] 3.5 Verify end to end on bag 133: revert to Bean Base data, then confirm the restored dead URL is probed and either archive-recovered or marked dead.

## 4. Observability

- [x] 4.1 Register a `[BeanBase]` subsystem with a helper header, move the twelve existing hand-rolled `BeanBaseClient:`/`BeanBase:` prefixes onto it, and log at `INFO` when the automatic search declines, naming the condition only (`no provider`, `provider cannot search`, `already searched`, `link usable`, `identity incomplete`); verify the line appears in `debug_get_log` for a bag that declines.
- [x] 4.2 Run `python3 scripts/check_log_markers.py` and confirm the new line's marker and tier pass the gate.

## 5. Surface parity

- [x] 5.1 Export the "no usable URL" condition so the ShotServer and MCP surfaces consume it rather than re-deriving it; verify all three read one definition.
- [x] 5.2 `/beans` web editor: show its Get info control under the same rule and give it the find-then-read path; verify at `localhost:8888` on a bag with no link.
- [x] 5.3 `bag_extract_details`: for a bag whose stored link is dead, search for a page and return it as `suggestedUrl` exactly as it already does for a bag with no link; verify over MCP against bag 133.
- [x] 5.4 Bump `McpSurfaceVersion` in `src/mcp/mcpserver.h` and run `python3 scripts/check_mcp_tool_budget.py`.

## 6. Tests and gates

- [x] 6.1 Extend `tests/tst_beanbaseclient.cpp` with the fallback cases from group 1 (404 recovers, non-404 does not, no capture, fault, bounded recursion) as new slots in the existing file, not a new file.
- [x] 6.2 Break each new behaviour and watch the assertion go red before keeping it.
- [x] 6.3 Run the full suite through `mcp__qtcreator__run_tests` scope `all` and confirm no warnings.
- [x] 6.4 Run `python3 scripts/qmllint_report.py --check` and the text-invariant scripts touched by this change.

## 7. Documentation

- [x] 7.1 Update `docs/CLAUDE_MD/BEAN_BASE.md`: extraction reaches the archive on its own, and the availability rule is "no usable URL", not "no URL".
- [x] 7.2 Update the wiki manual's Get info entry only if the user-visible rule changed — one or two sentences, no explanation of the archive machinery, which stays invisible automation.

## 8. Ship

- [x] 8.1 Open the PR.
- [x] 8.2 Run `/pr-review-toolkit:review-pr` and address the findings.
- [x] 8.3 Archive the change and sync specs as the final commit on the PR.
