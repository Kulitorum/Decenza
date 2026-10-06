## 1. Implementation

- [x] 1.1 `SearchField` and `SortControls` components; Recipes and Shot History moved onto them.
- [x] 1.2 Beans search (every text value of a bag, finished bags included) and sort, persisted; settings export/import.
- [x] 1.3 Profile picker, Settings search and Change Beans use `SearchField`.
- [x] 1.4 Web `/beans` search and sort; shared web tokenizer.
- [x] 1.5 Tests: bag search on app and web (`tst_recipesearch`), bag sort round trip (`tst_settings`).
- [x] 1.6 Review fixes: no search timers; finished-bag read state and failure line; finished results refresh during a search; kind searchable, `yieldMode` not; unknown saved sort falls back; clear drops an IME preedit; shared web search/sort controls and comparator, saved sort on both web pages, "no matches" only when both shelves are empty; `KeyboardAwareContainer` on Beans and Recipes.
- [x] 1.7 Tests: app and web haystacks identical; every skipped key at every depth; unreadable blob; `sortedCopy` on app and web.

## 2. Verification

- [x] 2.1 Build and full suite through Qt Creator: 119/119; QML lint gate clean (255/255). New `tst_recipesearch` checks seen red by breaking the web skip list and the web sort tie-break.
- [x] 2.2 Checked on the Mac (live build): Beans search per keystroke, "tea", finished-only matches, ×, sort; Recipes search and ×; Shot History fast typing lands on the final query, × restores all; profile picker, Settings search and Change Beans search and ×. Web `/beans` and `/recipes`: search, finished/archived-only matches, ×, saved sort shared with the app. Found and fixed: `SearchField` took Shot History's list height (nested layouts fill height by default).
- [x] 2.3 Wiki manual: Beans search and sort (Manual §5 Bean Bags, wiki 031f832).
