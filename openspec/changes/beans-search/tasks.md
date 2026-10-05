## 1. Implementation

- [x] 1.1 `SearchField` and `SortControls` components; Recipes and Shot History moved onto them.
- [x] 1.2 Beans search (every text value of a bag, finished bags included) and sort, persisted; settings export/import.
- [x] 1.3 Profile picker, Settings search and Change Beans use `SearchField`.
- [x] 1.4 Web `/beans` search and sort; shared web tokenizer.
- [x] 1.5 Tests: bag search on app and web (`tst_recipesearch`), bag sort round trip (`tst_settings`).

## 2. Verification

- [x] 2.1 Build and full suite through Qt Creator: 119/119; QML lint gate clean.
- [ ] 2.2 Check on the Mac: Beans search and sort; Recipes and Shot History search, clear and sort unchanged; the profile picker, Settings search and Change Beans search with their new clear button. Web `/beans` search and sort.
- [ ] 2.3 Wiki manual: a line on Beans search (draft for approval).
