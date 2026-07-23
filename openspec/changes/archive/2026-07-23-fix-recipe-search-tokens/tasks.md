# Tasks

## 1. In-app Recipes page
- [x] 1.1 In `qml/pages/RecipesPage.qml` `filterAndSort`, replace the single-substring
      `hay.indexOf(q)` match with a tokenized matcher: normalize `-`, `/`, `.` to spaces in
      both the query and the haystack, split the query on whitespace, and require every
      token to be found in the normalized haystack (name + roaster + coffee + profile title).
      (Extracted to `qml/components/RecipeSearch.js` as the single source of truth; registered
      in `CMakeLists.txt`.)
- [x] 1.2 Preserve existing behavior for the empty query (all pass) and for a single token.

## 2. ShotServer web /recipes page
- [x] 2.1 In `src/network/shotserver_recipes.cpp` `matchesFilter`, apply the same tokenized,
      punctuation-normalized, all-tokens-required matching over the existing haystack
      (name, profileTitle, roaster, coffee, drink-type label).
- [x] 2.2 Keep `applyFilter()`'s trim/lowercase entry point; move normalization into the
      matcher so the two surfaces stay behaviorally identical.

## 3. Tests
- [x] 3.1 Add a unit test for the tokenized matcher covering: multi-token cross-field query
      (`Yirg Df` matches Yirgacheffe + D-Flow), punctuation-spanning token (`df` matches
      `D-Flow`), single-token still matches, and all-tokens-required (a non-matching token
      hides the recipe). (`tests/tst_recipesearch.cpp`, evaluates the real `RecipeSearch.js`.)

## 4. Manual
- [x] 4.0 Update the wiki Manual's Recipes search section (Manual.md ~L485) to note multi-word
      cross-field search and punctuation-insensitivity.

## 5. Verification
- [x] 5.1 Run the full test suite via `mcp__qtcreator__run_tests` (scope all) — green
      (93 passed, 0 failed, 0 warnings).
- [ ] 5.2 Launch the app and confirm `Yirg Df` (and similar cross-field queries) now filter
      the Recipes page as History does.
