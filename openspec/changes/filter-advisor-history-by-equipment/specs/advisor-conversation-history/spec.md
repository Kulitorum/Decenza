## ADDED Requirements

### Requirement: Advisor conversation threads SHALL be identified by equipment package

An advisor conversation thread SHALL be identified by the equipment package a shot was pulled on
in addition to the bean and the profile. Two shots on the same bean and profile but different
equipment packages SHALL open different threads; a shot returning to a package that already has
a thread SHALL resume that thread.

A saved conversation is replayed to the model on every request, so its stored turns carry
whatever context they were built with. Scoping the payload alone would leave older turns
describing shots from other equipment inside the same transcript, where they continue to inform
every subsequent answer. Threading on equipment is what keeps a transcript describing one
equipment set for its whole life.

Threads saved before this requirement SHALL be cleared once, at the upgrade. They cannot be
resumed — their key does not carry a package, so no shot on this device derives it — and they
cannot be read either: their turns are stored in the prose format whose only readers were deleted
with it. Retaining them would hold slots in a five-thread limit for transcripts nothing can
render.

Threads created under this requirement SHALL be retained unchanged: they age out under the same
limit as any other, and nothing is deleted eagerly.

#### Scenario: First advisor use after upgrading starts a fresh thread

- **GIVEN** a user with saved advisor conversations from before this change
- **WHEN** they open the advisor on a shot after upgrading
- **THEN** a new conversation thread SHALL start with no prior turns
- **AND** the pre-upgrade thread's turns SHALL NOT be sent to the model

#### Scenario: Switching basket opens a separate thread

- **GIVEN** an ongoing advisor conversation about a bean and profile on equipment package A
- **WHEN** the user pulls a shot on package B with the same bean and profile, and opens the
  advisor
- **THEN** a separate thread SHALL be used for the package B shot
- **AND** the package A conversation SHALL NOT contribute turns to it

#### Scenario: Returning to the earlier equipment resumes its thread

- **GIVEN** threads exist for both package A and package B on the same bean and profile
- **AND** neither has aged out under the retention limit
- **WHEN** the user pulls another shot on package A and opens the advisor
- **THEN** the package A thread SHALL resume with its existing turns
