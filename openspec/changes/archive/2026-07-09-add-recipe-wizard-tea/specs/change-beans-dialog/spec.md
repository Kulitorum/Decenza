# change-beans-dialog Specification (delta)

## ADDED Requirements

### Requirement: Tea creation mode
The Change Beans dialog SHALL support a tea mode used by the "Add Tea" entry point. In tea mode: the Visualizer canonical search lane SHALL be suppressed (the canonical database is coffee-only and returns coffee false-positives for tea terms); the past-bags lane SHALL search only tea bags (re-buy flow); when no tea bags exist the dialog SHALL open directly on the form. The tea form SHALL relabel roaster → "Brand" and coffee → "Tea", SHALL hide roast level, grinder setting/rpm, and all canonical-link affordances, and SHALL keep the URL field, "Get info from page", photo resolution, weight/remaining, and show-on-idle. Tea mode is subtraction over the existing form — one mode property, not a parallel form.

#### Scenario: No Visualizer results for tea
- **WHEN** the user types "earl grey" in tea mode
- **THEN** only past tea bags are searched and no canonical coffee results appear

#### Scenario: First tea goes straight to the form
- **WHEN** the user taps "Add Tea" with no tea bags in history
- **THEN** the form opens directly with tea labels and without roast-level or grind fields

#### Scenario: Re-buying a tea
- **WHEN** the user picks a past tea bag from the tea-mode search
- **THEN** the form prefills from it exactly as the coffee re-buy flow does
