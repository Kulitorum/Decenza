# history-recipe-search Specification

## Purpose

A user who names a recipe for themselves ("Dad Monday", "Comp V3") SHALL be able to find
that recipe's shots by that name, both by typing it and by tapping through from a shot
that used it.

## ADDED Requirements

### Requirement: Bare search text matches recipe names

A free-text Shot History search term SHALL match shots whose recipe's name contains the
term, case-insensitively, in addition to the fields it already matches (notes, bean,
profile, grinder). A shot matching on any one of those SHALL be returned.

#### Scenario: Searching a recipe name that appears nowhere else

- **WHEN** the user types a term that appears only in a recipe's name
- **THEN** the shots pulled with that recipe are listed

#### Scenario: Term matches both a recipe name and a bean

- **WHEN** the user types a term that appears in a recipe's name and in a different shot's bean
- **THEN** both sets of shots are listed

#### Scenario: Archived recipe

- **WHEN** the user's search term matches an archived recipe's name
- **THEN** that recipe's shots are listed, rendered per the archived-recipe display rule

### Requirement: The `recipe:` keyword scopes a search to recipe names

Shot History SHALL support a `recipe:` search keyword that restricts matching to the
recipe name, excluding notes, bean, profile and grinder. It SHALL accept two forms:

- **Single token** — `recipe:dad` — matching recipe names containing `dad`.
- **Quoted** — `recipe:"dad tuesday"` — matching recipe names containing `dad tuesday`,
  so that names sharing a leading word can be distinguished.

Both forms SHALL be substring matches, case-insensitive. The keyword SHALL compose with
the existing numeric and boolean keywords and with remaining free text, in any order. An
unterminated quote SHALL be treated as running to the end of the search text rather than
failing the search. The keyword SHALL appear in the Keywords help sheet alongside the
existing keywords.

#### Scenario: Single-token keyword

- **WHEN** the user searches `recipe:dad` and two recipes are named "Dad Monday" and "Dad Tuesday"
- **THEN** shots from both recipes are listed

#### Scenario: Quoted keyword disambiguates

- **WHEN** the user searches `recipe:"dad tuesday"`
- **THEN** only shots from "Dad Tuesday" are listed

#### Scenario: Quoted keyword is still a substring

- **WHEN** the user searches `recipe:"hometown blend latte"` and a recipe is named "Hometown Blend Latte · D-Flow / Q"
- **THEN** that recipe's shots are listed

#### Scenario: Keyword excludes other fields

- **WHEN** the user searches `recipe:ethiopia` and a bean is named "Ethiopia Guji" but no recipe name contains "ethiopia"
- **THEN** no shots are listed

#### Scenario: Composed with another keyword

- **WHEN** the user searches `rating:70+ recipe:dad`
- **THEN** only shots from recipes matching "dad" and rated 70 or above are listed

#### Scenario: Unterminated quote

- **WHEN** the user searches `recipe:"dad tues`
- **THEN** the search matches recipe names containing `dad tues` rather than returning an error or no results

### Requirement: Tapping a row's recipe filters history to that recipe

Tapping the recipe name on a Shot History row SHALL filter the list to the shots of that
exact recipe, identified by id rather than by name, so that renaming the recipe does not
change the result and two recipes sharing a name are not conflated. The active filter
SHALL be visible and clearable like the existing external filters.

#### Scenario: Tap-through from a row

- **WHEN** the user taps the recipe name on a Shot History row
- **THEN** the list shows only shots pulled with that recipe, and the active filter is shown with a control to clear it

#### Scenario: Two recipes share a name

- **WHEN** the user taps through from a shot whose recipe shares its name with another recipe
- **THEN** only the tapped shot's own recipe's shots are listed

#### Scenario: Recipe renamed while the filter is active

- **WHEN** the recipe is renamed and the filtered list refreshes
- **THEN** the same shots remain listed under the new name
