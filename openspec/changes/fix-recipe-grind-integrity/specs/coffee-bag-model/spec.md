## MODIFIED Requirements

### Requirement: Bean/grinder edits write through to the active bag
Pre-shot edits to grinder fields (brew dialog, bag editing surfaces) SHALL write directly to the active bag, unconditionally — including while a recipe with its own owned grind (recipe-model) is active. There SHALL be no intermediate live-DYE copy of bean/grinder state that can diverge from the bag, and no modified-state computation or save prompt. When a recipe is active, the same edit SHALL also write to that recipe's own `grindPinned`/`rpmPinned` (recipe-model) — the bag and the active recipe's own grind both update immediately from the same edit, independently of each other; neither is ever deliberately withheld from the other.

#### Scenario: Grinder edit before a shot
- **WHEN** the user changes the grinder setting in the brew dialog while a bag is active
- **THEN** the active bag's `grinderSetting` SHALL be updated immediately (background DB write)
- **AND** no save prompt or modified indicator SHALL appear

#### Scenario: Grinder edit with an active recipe updates both the bag and the recipe
- **WHEN** the user changes the grinder setting while a recipe is active
- **THEN** the active bag's `grinderSetting` SHALL be updated immediately, exactly as when no recipe is active
- **AND** the active recipe's own `grindPinned` SHALL also be updated immediately
- **AND** neither write waits on or is gated by the other

#### Scenario: Grinder/dose correction on post-shot review
- **WHEN** the user corrects the grinder setting or dose on the post-shot review page (e.g. the recorded value was wrong)
- **THEN** both the just-saved shot's record AND the active bag SHALL be updated (preserving today's dual-write behaviour)

#### Scenario: Activating a recipe updates its linked bag's grind
- **WHEN** a recipe whose own grind is "17" is activated and its linked bag's stored grind was "18"
- **THEN** the live grind becomes "17" and the bag's stored `grinderSetting` updates to "17" — the bag mirrors the most recently dialed grind, and activating a recipe that selects this bag counts as dialing

#### Scenario: Bean-less recipe activation touches no bag
- **WHEN** a bean-less recipe with its own grind is activated
- **THEN** the active bag is cleared as part of activation (recipe-activation), so the recipe's grind — and any subsequent grind edits — write through to no bag at all (the write-through is a no-op with no active bag)
