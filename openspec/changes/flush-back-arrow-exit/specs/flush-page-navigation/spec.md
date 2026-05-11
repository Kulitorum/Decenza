## ADDED Requirements

### Requirement: BottomBar visible during active flush

The Flush Water page SHALL display its bottom navigation bar in both the settings view and the active-flush view, so the back-arrow exit affordance is reachable at all times.

#### Scenario: Settings view shows full BottomBar

- **WHEN** the user opens the Flush Water page and the machine is not flushing
- **THEN** the BottomBar is visible with its back arrow, preset-name title, and the duration / flow value chips

#### Scenario: Active-flush view shows BottomBar with chips hidden

- **WHEN** the machine phase is `Flushing` and the FlushPage is the current page
- **THEN** the BottomBar is visible
- **AND** the back arrow is reachable and focusable
- **AND** the duration and flow value chips inside the BottomBar are hidden, leaving only the back arrow and preset-name title

### Requirement: Back arrow exits an active flush immediately

The back arrow on the Flush Water page SHALL stop an in-progress flush and return the user to the previous page without waiting for the configured flush duration or the completion overlay.

#### Scenario: Back tapped during active flush

- **WHEN** the machine phase is `Flushing` and the user activates the back arrow (tap, Enter, Space, or Escape)
- **THEN** `DE1Device.stopOperation()` is invoked
- **AND** the user-exit flag is set so the completion overlay is suppressed
- **AND** navigation returns to the previous page in the stack, or to the idle page when no previous page exists

#### Scenario: Back tapped in settings view (unchanged behavior)

- **WHEN** the machine is not flushing and the user activates the back arrow
- **THEN** `MainController.applyFlushSettings()` is invoked to persist any in-progress preset edits
- **AND** navigation returns to the previous page in the stack, or to the idle page when no previous page exists
- **AND** the user-exit flag is NOT set

### Requirement: Completion overlay suppressed on explicit user exit from flush

The "Flush Complete" completion overlay SHALL be suppressed when the phase transition to Idle/Ready is the result of an explicit user-initiated exit from the Flush Water page. Natural completions (firmware-driven end of flush) SHALL continue to show the overlay as today.

#### Scenario: Explicit exit skips overlay

- **WHEN** the user activates the back arrow during a flush
- **AND** the machine phase subsequently transitions to Idle or Ready
- **THEN** the completion overlay is NOT shown
- **AND** the user-exit flag is cleared so it cannot affect a later phase transition

#### Scenario: Natural completion still shows overlay

- **WHEN** the firmware ends the flush on its own because the configured duration elapsed
- **AND** the machine phase transitions to Idle or Ready while the FlushPage is current
- **THEN** the completion overlay is shown for its existing duration (1.5 seconds)
- **AND** the user-exit flag remains clear

#### Scenario: Flag is unconditionally cleared on every Idle/Ready transition

- **WHEN** the user-exit flag is set
- **AND** any phase transition to Idle or Ready occurs (regardless of which page is current)
- **THEN** the flag is cleared at the end of the Idle/Ready handler so it cannot strand and suppress a later legitimate completion
- **AND** the flag is only ever checked inside the Flush Water page branch, so it cannot suppress steam or hot-water completion overlays

### Requirement: Existing headless STOP button preserved and made equivalent to back arrow

The existing on-screen STOP button shown only on headless machines (`DE1Device.isHeadless`) during an active flush SHALL remain in place. To make the two on-screen exits behaviorally equivalent, the STOP button's handlers SHALL also set the user-exit flag so the completion overlay is suppressed identically to the back-arrow path.

#### Scenario: Headless machine retains STOP button with equivalent exit behavior

- **WHEN** the machine is flushing and `DE1Device.isHeadless` is true
- **THEN** the on-page STOP button is visible alongside the back arrow on the BottomBar
- **AND** both controls set the user-exit flag, invoke `DE1Device.stopOperation()`, and navigate away from the FlushPage
- **AND** neither path shows the 1.5s "Flush Complete" overlay

#### Scenario: Non-headless machine relies on back arrow only

- **WHEN** the machine is flushing and `DE1Device.isHeadless` is false
- **THEN** the on-page STOP button is hidden (existing behavior)
- **AND** the back arrow on the BottomBar is the on-screen way to exit
