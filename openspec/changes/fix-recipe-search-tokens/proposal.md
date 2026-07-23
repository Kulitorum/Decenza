# Tokenize recipe search so multi-field queries match

## Why

A user reported that typing `Yirg Df` on the Recipes page finds nothing, even though
Shot History finds the same Yirgacheffe / D-Flow shots. The recipe search *does* already
include coffee and profile in what it looks at — the fields are not the problem. The
matcher is.

Recipe search (`RecipesPage.qml:81`, and the identical web `/recipes` code at
`shotserver_recipes.cpp:862`) joins the recipe's fields into one string and tests it with
`hay.indexOf(query)` — a single contiguous-substring match of the **entire** query. So:

- A two-token query that spans two fields (`Yirg` in the coffee, `Df` in the profile) can
  never match, because "yirg df" never appears as one contiguous run in the joined text.
- Punctuation defeats even a single token: `df` will not match `D-Flow` because the hyphen
  breaks the run.

Shot History does not have this problem because its FTS path (`formatFtsQuery`,
`shothistorystorage_queries.cpp:289`) splits the query into tokens on whitespace **and**
punctuation `[-/.]`, prefix-matches each, and ANDs them across the whole indexed row. This
change brings the same multi-word, cross-field idea to recipe search — on both the in-app
page and the ShotServer web page.

One deliberate difference from FTS: the real profile titles are stored as `D-Flow / Q`
(verified against live shot data), which FTS would tokenize into `d`/`flow`/`q` and
prefix-match — so History would **not** actually match a bare `df` either. The reported
query is literally `Yirg Df`, and users think of the profile as one word ("Dflow"). So this
matcher **removes** `-`, `/`, `.` (rather than splitting on them), collapsing `D-Flow` to
`dflow` so the abbreviation `df` matches. That makes recipe search a superset of History's
behavior for exactly the case the user hit.

## What Changes

- Recipe search matching becomes **token-based (AND)** with **punctuation removal**:
  - Remove `-`, `/`, `.` from both the query and the searchable text.
  - Split the query on whitespace into tokens.
  - A recipe matches only if **every** token is found (as a substring) somewhere in its
    combined searchable text (name + roaster + coffee + profile title; the web page also
    folds in the drink-type label, which it already does).
- Applies to the in-app Recipes page (`RecipesPage.qml`) and the ShotServer web `/recipes`
  page (`shotserver_recipes.cpp`), keeping the two surfaces in sync.
- No backend, storage, or data-model change. No change to which fields are searched, to
  sort behavior, or to the clear control.

## Impact

- Affected specs: `recipe-list-organization` (Recipe search requirement).
- Affected code: `qml/pages/RecipesPage.qml`, `src/network/shotserver_recipes.cpp`.
- Affected tests: add coverage for the tokenized matcher (multi-token cross-field query,
  punctuation-spanning token, single-token still works, all-tokens-required).
