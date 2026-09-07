## 1. Keep the URL on a dead verdict

- [x] 1.1 Change `BeanBaseBlob::markLinkDead` to retain `link` and set `linkChecked`/`linkDead`; verify with a unit test that the URL survives and both marks are set.
- [x] 1.2 Give `blobWithLinkVerdict` the dead URL rather than `""` at its `BagCard.onBagLinkDead` call site, and drop the now-wrong "clear the dead reorder link" comment; verify the stored blob keeps the URL after a dead verdict.
- [x] 1.3 Verify `linkIsUsable` already answers false for the retained link (its `linkDead` names that URL) — no change expected, but assert it, because everything below depends on it.

## 2. Consumers gate on usable, not present

- [x] 2.1 `BagCard.imageKey`: a manual bag's `bag-<id>` key requires a USABLE link, so a dead one drives no photo fetch; verify no `ensureBagImage` request is issued for a bag whose link is marked dead.
- [x] 2.2 `BeanBaseDetailsPopup`: show the product-link row for a retained dead URL, marked as no longer resolving, through `TranslationManager`; verify the marking disappears once the link is recovered.
- [x] 2.3 Verified no change needed: the web surface has an editable Product URL input, not a view-at-roaster row — a dead URL must stay visible there so it can be fixed — and its `linkIsUsable()` JS already decides the Get-info-vs-Find-page split from `linkDead`.
- [x] 2.4 Grep for remaining `beanBase.link`/`blob.link` presence tests across `qml/` and `src/` and convert the ones that mean "usable"; list any deliberately left as presence tests and why.

## 3. Every bag is checked, keyed by the bag

- [x] 3.1 Add `BagCard.linkKey` — the canonical id when the bag has one, otherwise `bag-<rowid>`, the key its photo cache already uses; verify it is non-empty for a manual bag with a row id.
- [x] 3.2 Drop `hasCanonical` from `maybeValidateLink` and key it on `linkKey`; verify a manual bag holding a URL issues exactly one `validateBagLink` and stamps `linkChecked`.
- [x] 3.3 Match `onBagLinkResolved` / `onBagLinkArchived` / `onBagLinkDead` on `linkKey` rather than `canonicalId`, so a manual bag's own verdicts reach it; verify each handler fires for a manual bag. Leave `onBagLinkRecovered` on `canonicalId` — it answers a canonical-API search that a manual bag never issues.
- [x] 3.4 Verify `isSafeCacheFilename` accepts `bag-<rowid>`, so the client-side guards do not silently drop a manual bag's queries.

## 4. Recovery retries from the retained URL, when the bag is used

- [x] 4.1 `BagCard.maybeRecoverArchivedLink`: retry from `beanBase.link` when marked dead, falling back to `canonical.link` only for a bag an older build already emptied; verify a MANUAL bag with a dead link issues an archive lookup.
- [x] 4.2 Move the retry off `Component.onCompleted`/`onImageKeyChanged` and onto selection, so drawing the inventory queries nothing; verify no archive lookup is issued for a non-active dead bag.
- [x] 4.3 On recovery the link becomes the snapshot and `linkDead` clears (the existing `onBagLinkArchived` path); verify end to end that a dead manual bag becomes an ordinary working bag.

## 5. The 404 decides the mark; the archive only upgrades

- [x] 5.1 `validateBagLink`: emit `bagLinkDead` on the 404/410 itself, then ask the archive purely for an upgrade; verify a bag is marked dead even when the availability query never answers.
- [x] 5.2 Verify a capture still replaces the link and clears the mark, and that no archive outcome other than a capture changes the bag.
- [x] 5.3 Re-read the fault/miss wording in `beanbaseclient.cpp` and `BEAN_BASE.md` and correct anything that now describes the old behaviour — `answered` no longer decides a verdict on this path.

## 6. Tests and gates

- [x] 6.1 Add the cases above as new slots in `tests/tst_beanbaseclient.cpp` — no new test file.
- [x] 6.2 Break each new behaviour and watch the assertion go red before keeping it.
- [x] 6.3 Run the full suite through `mcp__qtcreator__run_tests` scope `all`; confirm no warnings.
- [x] 6.4 Run `python3 scripts/qmllint_report.py --check` and the text-invariant scripts.

## 7. Documentation

- [x] 7.1 Update `docs/CLAUDE_MD/BEAN_BASE.md`: a dead verdict retains the URL, recovery retries from it and covers manual bags, and an empty availability envelope is not proof of absence — with the observed HTTP 200 evidence, since the next reader will otherwise assume a 200 is authoritative.
- [x] 7.2 Wiki manual: no change. The only user-visible difference is a label that states its own meaning ("Roaster's page no longer responds"); the manual documents features a user must be told exist, not self-explanatory status text.

## 8. Ship

- [ ] 8.1 Open the PR.
- [ ] 8.2 Run `/pr-review-toolkit:review-pr` and address the findings.
- [ ] 8.3 Archive the change and sync specs as the final commit on the PR.
