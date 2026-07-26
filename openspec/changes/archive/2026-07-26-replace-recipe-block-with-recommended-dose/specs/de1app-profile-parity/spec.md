## MODIFIED Requirements

### Requirement: User-saved profiles are not rewritten

Correcting import fidelity SHALL apply to newly imported profiles and to the app-authored
built-ins in `resources/profiles/` only. Profiles a user has saved MUST NOT be retroactively
rewritten to change a value, since they load through `fromJson` and may hold deliberate user
edits.

Removing a key that no reader consults is not such a rewrite, and is permitted. The distinction
is whether a stored value the user could have set is altered: correcting a scalar to match an
import rule would be, while dropping the `recipe` block is not, because every value in it is
re-derived on read, duplicated by a top-level key, or read by nothing — except `dose`, which is
preserved as `recommended_dose` rather than discarded.

#### Scenario: User profile with a non-default scalar

- **WHEN** the built-in corpus is re-synced
- **THEN** profiles in the user's own profile directory are byte-identical to before

#### Scenario: A user profile keeps every value it holds

- **WHEN** a user-saved profile has its recipe block removed
- **THEN** every scalar, frame, target and flag it holds is unchanged
- **AND** a dose the user set is preserved as the profile's recommended dose
