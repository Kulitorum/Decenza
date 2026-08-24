## ADDED Requirements

### Requirement: In-app advisor shot history SHALL be scoped to the shot's equipment package

The in-app advisor's historical context SHALL include a prior shot only when that shot's
equipment package matches the current shot's, in addition to the bean, profile and time-window
match it already applies. "No package recorded" SHALL be treated as a package value in its own
right, so shots with no equipment package match each other and nothing else — a user who has
never created a package SHALL see no change in which shots qualify.

An equipment package identifies grinder, basket and puck prep together, and changing any one of
them yields a different package. The same numeric grind setting on a different basket does not
describe the same extraction, so a prior shot on different equipment SHALL be excluded however
closely its bean, profile and setting match.

#### Scenario: History excludes shots pulled on a different basket

- **GIVEN** two equipment packages sharing one grinder, differing only in basket
- **AND** a history of shots on both, all on the same bean and profile
- **WHEN** the in-app advisor builds historical context for a shot on the second package
- **THEN** the `## Previous Shots with This Bean & Profile` section SHALL contain only shots
  from the second package
- **AND** SHALL NOT contain a shot from the first

#### Scenario: A user with no equipment packages sees an unchanged history

- **GIVEN** a user whose shots all have no equipment package recorded
- **WHEN** the in-app advisor builds historical context for any of their shots
- **THEN** the qualifying shots SHALL be exactly those that qualified before this requirement
- **AND** no shot SHALL be excluded on equipment grounds

### Requirement: Both advisor surfaces SHALL send one payload in one format

The in-app advisor and `ai_advisor_invoke` build their system prompt from one function and send
it to the same model. They SHALL therefore send the same user-prompt format, assembled by the
same code: the structured payload whose field paths that shared system prompt names. Neither
surface SHALL carry a second renderer of the same data.

A system prompt that instructs the model to read `dialInSessions[].context` and a payload that
delivers markdown are a contract and a breach of it. Two renderers of one dataset also drift by
construction — the identity fields are defined once in `ShotIdentity::fields()`, and a hand-
written second copy is what left the basket and puck prep out of one surface while the other
picked them up from a single table row.

The user's question SHALL travel as its own field rather than concatenated into the payload, so
that recovering it for display is a field read and not a parse of prose.

#### Scenario: The in-app advisor sends the structured blocks its system prompt names

- **WHEN** the in-app advisor sends a shot with historical context
- **THEN** the user prompt SHALL be the structured payload
- **AND** SHALL carry the blocks the shared system prompt references, on the same field paths
  `ai_advisor_invoke` uses

#### Scenario: One renderer defines the payload

- **WHEN** an equipment component is added to `ShotIdentity::fields()`
- **THEN** it SHALL appear on both surfaces without a further edit to either
- **AND** no surface SHALL hand-render an identity field it could read from that table

#### Scenario: The displayed conversation reads the question from a field

- **WHEN** the conversation view renders a user turn that carried shot context
- **THEN** the question SHALL be read from the turn's own field
- **AND** SHALL NOT be recovered by searching the payload text for delimiters

### Requirement: The advisor payload SHALL name the equipment set its shots were pulled on

The advisor payload SHALL name the equipment set shared by the history's shots: the grinder
(brand, model, burrs), the basket (brand, model), and the puck-prep technique set. Components
with no recorded value SHALL be omitted rather than emitted empty.

A filter the model cannot see is a silent one: without the equipment named in the payload, the
model can neither attribute the history to the gear it came from nor recognise that a user has
changed baskets. The equipment set SHALL come from a single shared definition so the session
context and the no-history block below cannot describe the same package differently.

#### Scenario: The payload names grinder, basket and puck prep

- **GIVEN** a history whose shots were pulled on a Niche Zero with 63mm Mazzer Kony conical
  burrs, a Graph Coffee "Stepped 58→46mm" basket, and puck prep of shaker + puck screen + RDT
- **WHEN** the advisor assembles the payload
- **THEN** the hoisted session context SHALL name the grinder, the basket and the puck-prep
  techniques
- **AND** SHALL continue to name the bean, roast level and roast date as before

#### Scenario: A package with no basket recorded omits the basket fields

- **GIVEN** a history whose equipment package has no basket recorded
- **WHEN** the advisor assembles the payload
- **THEN** the session context SHALL name the grinder and bean as before
- **AND** SHALL NOT carry an empty or placeholder basket field

### Requirement: In-app advisor SHALL state an empty history rather than omitting it

When no prior shot matches the current shot's bean, profile, time window and equipment package,
the in-app advisor's historical context SHALL emit a block that states no prior shots matched
and names the equipment set that was matched on, together with the reason equipment-mismatched
shots were excluded. It SHALL NOT emit an empty historical context in this case.

An absent history block is indistinguishable from "this user has no history at all", and a model
given no anchor in context is a model that supplies one: the reported failure cited a "70/100
shot" that appears nowhere in its context and then reasoned from it. A stated absence is a fact
the model can use in place of an invented one.

#### Scenario: First shot on a new equipment package states the empty history

- **GIVEN** a user with an extensive history on one equipment package
- **WHEN** they pull the first shot on a newly created package and open the advisor
- **THEN** the historical context SHALL state that no prior shots match this equipment set
- **AND** SHALL name the equipment set
- **AND** SHALL instruct the model to judge the shot on its own data rather than referring to
  shots it cannot see

#### Scenario: A populated history does not carry the empty-history block

- **GIVEN** at least one qualifying prior shot
- **WHEN** the in-app advisor renders the historical context
- **THEN** the rendered context SHALL contain the per-shot history blocks
- **AND** SHALL NOT contain the empty-history statement

### Requirement: Shot-to-shot change detection SHALL compare only within one equipment package

Change detection compares the current shot with the previous shot IN THE SAME THREAD. A thread is
identified by its equipment package, so both shots necessarily share one, and an equipment change
cannot appear in that comparison — a basket switch opens a different thread instead. Change
detection SHALL continue to report dose, yield, duration and grind setting, and SHALL NOT report
an equipment change: the arm would compare a value with itself and could never emit.

This requirement is recorded rather than dropped because an earlier draft of this change asked
for the opposite, and the reasoning is not obvious from the code. It was written when a thread
was keyed on bean and profile alone, where a swap genuinely could land two packages in one
transcript and narrating it was the mitigation on the table. Keying on the package removed the
condition instead of describing it.

#### Scenario: Switching basket does not appear as a change within a thread

- **GIVEN** a thread on equipment package A
- **WHEN** the user pulls a shot on package B with the same bean and profile
- **THEN** a separate thread SHALL be used
- **AND** the package B shot's changes line SHALL NOT reference package A's shots

#### Scenario: A grind change on the same package is still reported

- **GIVEN** consecutive shots in one thread at grinder settings 9.5 and 9.25
- **WHEN** the advisor assembles the second shot's message
- **THEN** the changes line SHALL report the grind change

### Requirement: System prompt SHALL scope grind-setting comparability to one equipment set

The shared espresso system prompt SHALL instruct the model that a numeric grind setting is
comparable only among shots pulled on the same equipment set, and that a change of grinder,
burrs or basket makes two settings incommensurable even on the same dial. When the model
observes a setting that does not fit the ordering the rest of the history implies, it SHALL
consider an equipment difference before concluding anything about the grinder's mechanism.

This complements the existing rule that settings are never comparable across grinder models.

#### Scenario: An out-of-order setting prompts an equipment question, not a mechanism theory

- **GIVEN** a history where coarser settings produced lower peak pressure
- **AND** a current shot at a numerically much coarser setting that produced higher peak
  pressure and a longer shot
- **WHEN** the model explains the discrepancy
- **THEN** it SHALL consider a basket or grinder difference as a candidate explanation
- **AND** SHALL NOT assert a change in the grinder's own calibration or burr alignment as
  established fact

### Requirement: System prompt SHALL forbid citing shots, scores or taste notes absent from context

The shared system prompt SHALL instruct the model that it may cite only shots, enjoyment scores,
taste notes and measurements that appear in the context it was given, and that inventing any of
them is hallucination — the same discipline the prompt already imposes on the setpoints of
profiles other than the current one. When the model wants an anchor the context does not
contain, it SHALL say the data is not available rather than supplying a value.

#### Scenario: The model does not invent a rated shot to anchor a recommendation

- **GIVEN** a context whose shots carry no enjoyment score except the current shot's
- **WHEN** the model recommends a change and wants to refer to a previously well-rated shot
- **THEN** it SHALL NOT cite a score, taste description or shot that is not in the context
- **AND** SHALL state that no rated prior shot is available

#### Scenario: Scores present in context remain citable

- **GIVEN** a context containing a prior shot with an enjoyment score and taste notes
- **WHEN** the model refers to that shot
- **THEN** it MAY cite that shot's score and notes as recorded
