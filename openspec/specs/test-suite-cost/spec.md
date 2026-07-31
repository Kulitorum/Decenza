# test-suite-cost Specification

## Purpose
TBD - created by archiving change cut-test-build-cost. Update Purpose after archive.
## Requirements
### Requirement: A production source SHALL NOT be compiled into more than one test target

Where two or more test targets require the same production source, that source SHALL be compiled
once into an intermediate library shared by exactly those targets. The same SHALL hold for compiled
Qt resource units.

An intermediate library's consumer set SHALL be no wider than the set of targets that need it.
Sources SHALL NOT be added to the universally-linked `decenza_testlib` merely to deduplicate them:
that converts a bounded compile fan-out into a link fan-out across every test target, and makes
every test target rebuild whenever any shared source changes.

A test target SHALL NOT list a source that `decenza_testlib` already compiles.

#### Scenario: A source needed by several targets

- **GIVEN** `src/controllers/profilemanager.cpp` required by nine test targets
- **WHEN** the test build is configured
- **THEN** it SHALL be compiled exactly once, into a library linked by those nine targets
- **AND** it SHALL NOT be added to `decenza_testlib`

#### Scenario: A source needed by one target

- **GIVEN** a production source required by exactly one test target
- **WHEN** the test build is configured
- **THEN** it MAY remain in that target's own source list

#### Scenario: A source the shared library already carries

- **GIVEN** `src/ai/shotanalysis.cpp` compiled into `decenza_testlib`
- **WHEN** `tst_visualizershotparse`, which links `decenza_testlib`, also lists it
- **THEN** that entry SHALL be removed rather than a library introduced

#### Scenario: A compiled Qt resource unit needed by several targets

- **GIVEN** `qrc_resources.cpp` required by three test targets
- **WHEN** the test build is configured
- **THEN** it SHALL be compiled once and linked by those three targets

### Requirement: The duplication rule SHALL be enforced by a build-free check on every pull request

A check SHALL fail a pull request that introduces or retains either violation of the requirement
above. It SHALL read the test build configuration as text and SHALL NOT require Qt, a configured
build directory, or any compilation, so that it fits the project's build-free pull-request job.

The check SHALL fail only on invariants that have a correct answer. It SHALL NOT report advisory
figures that no decision depends on.

#### Scenario: A pull request adds a duplicate source

- **GIVEN** a production source already listed by one test target
- **WHEN** a pull request adds it to a second target's source list
- **THEN** the check SHALL fail and SHALL name the source and both targets

#### Scenario: A pull request adds a source the shared library carries

- **WHEN** a pull request lists a source in a test target that `decenza_testlib` already compiles
- **THEN** the check SHALL fail and SHALL name the source and the target

#### Scenario: The check needs no build

- **WHEN** the check runs in the pull-request workflow
- **THEN** it SHALL complete without configuring or building the project

#### Scenario: A conforming pull request

- **GIVEN** a pull request that adds a test without duplicating any source
- **WHEN** the check runs
- **THEN** it SHALL pass and SHALL NOT emit advisory cost figures

### Requirement: A new test SHALL state what it catches that the suite does not

Adding a test SHALL be accompanied by a statement of the defect shape it detects which no existing
test detects. "More cases of the same shape" is insurance rather than detection: it MAY be added,
but the argument SHALL be made on risk and SHALL be stated as insurance rather than presented as
new coverage.

This requirement is additional to, and does not replace, the existing rule that a test SHALL be
shown able to fail by breaking the code it covers and watching it go red.

#### Scenario: A test covering a genuinely new defect shape

- **GIVEN** a bug whose failure mode no existing test asserts against
- **WHEN** a regression test for it is proposed
- **THEN** the stated defect shape SHALL name that failure mode, and the test SHALL be kept

#### Scenario: A test that adds cases of an already-covered shape

- **GIVEN** an invariant already asserted by an existing test over three inputs
- **WHEN** a proposal adds 120 further generated inputs asserting the same invariant
- **THEN** it SHALL be described as insurance, with the risk it insures against stated
- **AND** it SHALL NOT be described as closing a coverage gap

#### Scenario: No distinguishing shape can be stated

- **GIVEN** a proposed test whose defect shape is already detected by an existing test
- **WHEN** no distinguishing shape can be stated
- **THEN** the assertion SHALL be added to the existing test rather than as a new one

### Requirement: One invariant SHALL be asserted in one place

Where an invariant is already asserted by the suite, a change SHALL extend that assertion rather
than add a parallel one elsewhere. Two tests asserting the same invariant over different fixtures
SHALL be consolidated into one test over both fixtures.

#### Scenario: Extending an existing assertion

- **GIVEN** `tst_dbmigration` already asserts that the schema version advances across the chain
- **WHEN** a new migration step is added
- **THEN** the existing assertion SHALL be extended to cover it
- **AND** a second test asserting schema-version advancement SHALL NOT be created

#### Scenario: Two fixtures, one invariant

- **GIVEN** the same invariant asserted in two tests differing only in their fixture
- **WHEN** the overlap is found
- **THEN** the two SHALL be consolidated into one test parameterised over both fixtures

### Requirement: Test build cost SHALL be attributed to where it is actually incurred

Guidance about the cost of a test SHALL reflect measured distribution. On the reference clean macOS
Debug rebuild (4,846 s cpu total), `tests/` accounts for 1,956 s (40%), distributed as: production
sources recompiled inside test targets 688 s (35.2%), test source translation units 625 s (31.9%),
moc and autogen 519 s (26.5%), Qt resource compilation 66 s (3.4%), link and timestamps 58 s (3.0%).

Guidance SHALL NOT present link cost, or the number of test targets, as a primary driver. Guidance
SHALL state that relocating test code between targets does not reduce the cost of compiling that
code.

#### Scenario: Documentation states the cost model

- **WHEN** `docs/CLAUDE_MD/TESTING.md` or `CLAUDE.md` describes what a test costs
- **THEN** it SHALL attribute the cost to compilation of test bodies and of the production sources a
  target pulls in
- **AND** it SHALL NOT imply that link cost is a comparable share

#### Scenario: Consolidation is not presented as a body-compile saving

- **GIVEN** advice to add a test to an existing target rather than create a new one
- **WHEN** the saving is described
- **THEN** it SHALL be described as saving link cost and duplicated production-source compilation
- **AND** it SHALL NOT be described as reducing the compile cost of the test body itself

