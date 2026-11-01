## REMOVED Requirements

### Requirement: Steam page SHALL NOT present a dedicated Purge control

**Reason**: The removed buttons only sent Idle, duplicating Stop, because the purge request was not known. The new Purge requests the firmware's SteamRinse state, a separate operation the firmware ends itself and accepts without a GHC tap.
**Migration**: The Steam page's Purge control and the idle Purge pill are specified in `steam-purge`. Stopping steam still triggers the firmware's wand purge on the Steam→Idle transition, unchanged.

## ADDED Requirements

### Requirement: Back on the Steam page stops steam
The Steam page SHALL show its bottom bar with Back while steam runs, including steam heating, a soft stop and puffing. Back SHALL then act as the Stop control does, as Back stops a shot on the espresso page. With no steam running, Back SHALL leave the page as before.

#### Scenario: Back while steaming
- **WHEN** the machine is steaming and the user taps Back
- **THEN** the app sends the same stop request as the Stop control

#### Scenario: Back with no steam running
- **WHEN** the Steam page shows its settings and the user taps Back
- **THEN** the page closes without a stop request
