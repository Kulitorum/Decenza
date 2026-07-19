# translation-reactivity Specification

## Purpose
Governs how translated text updates when the user changes language: visible strings retranslate in place, without a restart or a navigation round-trip. The central constraint is that the *natural* call pattern must be the reactive one — `TranslationManager.translate` is a property holding a callable, so an ordinary binding records a dependency on it. This exists because the previous `Q_INVOKABLE` form recorded no dependency, and 3,248 call sites written exactly as the documentation instructed silently froze on whatever language was active when the page was built. Includes the automated regression coverage that keeps it honest, and the guarantee that non-QML callers are unaffected.

## Requirements
### Requirement: A language change retranslates visible text without a restart

When the user changes the interface language, all text currently visible SHALL be re-rendered in
the new language without restarting the application.

Text that is stale after a language change is worse than untranslated text: the user sees a UI in
two languages at once and has no indication that a restart would fix it.

#### Scenario: Switching language updates the current page

- **WHEN** the user changes the interface language while a page is displayed
- **THEN** every translated string on that page renders in the new language

#### Scenario: Switching away and back leaves no residue

- **WHEN** the user changes from English to another language and then back to English
- **THEN** no text remains in the previously selected language

#### Scenario: Pages not visible at the time of the change

- **WHEN** the user changes language and then navigates to a page that was not displayed at the
  time of the change
- **THEN** that page's text renders in the new language

### Requirement: The natural call pattern is the reactive one

The translation lookup exposed to QML SHALL be reactive when called in the most obvious way, with
no additional ceremony required at the call site.

A pattern that is correct only when the author remembers an extra step will be got wrong, and the
resulting defect is invisible in review — the text is correct when the page is first built and
only diverges later.

#### Scenario: A newly written binding is reactive by default

- **WHEN** a developer writes a property binding that looks up a translated string in the manner
  the project documentation describes
- **THEN** that binding re-evaluates on a language change without the author having taken any
  further step

#### Scenario: Project documentation matches the implementation

- **WHEN** a developer follows the i18n guidance in the project instructions
- **THEN** the resulting code is reactive to language changes

### Requirement: Regression in translation reactivity is caught automatically

The build SHALL fail when the translation lookup is changed to a form that silently stops
re-evaluating bindings.

The failure mode being guarded against is invisible at compile time and at first render: the UI
looks correct until a language is changed, so no ordinary test or review step catches it.

#### Scenario: The reactive mechanism is removed

- **WHEN** the QML-facing translation lookup is changed such that bindings over it no longer record
  a dependency on translation changes
- **THEN** a test fails identifying the regression

### Requirement: Existing non-QML callers continue to work

Changing the shape of the QML-facing translation lookup SHALL NOT break translation lookups made
from C++.

#### Scenario: C++ translation lookups are unaffected

- **WHEN** C++ code looks up a translated string with a key and an English fallback
- **THEN** it receives the translation for the current language, or the fallback when none exists
