## MODIFIED Requirements

### Requirement: Recipes carry a drink type
The `recipes` table SHALL gain a `drink_type` TEXT column (values: `espresso`, `filter`, `americano`, `long_black`, `latte`, `latte_hotwater`, `tea`, `tea_hotwater`), added by migration with kCols registration, riding transfer/backup import like other recipe columns. The value records user intent and SHALL NOT drive machine behavior — activation reads only the blocks and profile. For rows without a stored value (pre-migration recipes) and for promote-from-shot, the type SHALL be derived from the blocks and profile beverage type (hot-water block without profile → tea_hotwater; profile tea_portafilter → tea; milk AND hot-water → latte_hotwater; milk alone → latte; hot-water block alone by order "after" → americano, "before" → long black; profile filter/pourover → filter; else espresso), and the derived value SHALL be stored on the next save.

#### Scenario: Legacy recipe derives its type
- **WHEN** a pre-migration americano recipe (hot-water block, order "after", no milk) is opened for edit
- **THEN** the summary shows drink type Americano, and saving stores `americano`

#### Scenario: Latte with added hot water derives its own type
- **GIVEN** a recipe with a profile carrying BOTH a milk block (`hasMilk`) and a hot-water block (`hasWater`)
- **WHEN** its drink type is derived (legacy row or promote-from-shot)
- **THEN** the derived type SHALL be `latte_hotwater`, superseding the prior "milk wins" collapse to `latte`
- **AND** a recipe with a milk block but no hot-water block SHALL still derive `latte`

#### Scenario: Drink type never gates activation
- **WHEN** a recipe's blocks contradict its stored drink type
- **THEN** activation applies the blocks exactly as stored
