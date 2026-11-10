# Proposal

## Why

After ten failed attempts the scale reconnect ladder slowed to one attempt every 5 minutes. A scale woken after that stayed unconnected, and a hot-water pour made in that window read 0 g (Kulitorum/Decenza#2058). Separately, when a saved WiFi scale was unreachable the app connected any Decent Bluetooth scale it saw, including one the user had forgotten, and saved it again.

## What Changes

- The 5-minute tier is removed. Outside an operation, a failed attempt starts the next one immediately; during an operation the 5 s / 30 s / 60 s ramp stands. The screensaver still pauses the ladder, and a suspended app does not chain attempts.
- The WiFi-to-Bluetooth fallback connects only a Bluetooth scale in the saved list, and is skipped when none is saved.
- Dismissing the "No scale found" and "Scale disconnected" dialogs no longer restarts the ladder.
- The log states when a seen scale was declined, and when the retry cadence changes.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `wifi-scale-discovery`: the retry schedule after a failed attempt, and which Bluetooth scale the fallback may take.

## Impact

- `src/main.cpp` (reconnect ladder), `src/ble/blemanager.{h,cpp}` (fallback rule, logging), `qml/main.qml` (two dialog handlers).
- With Auto-Sleep off, a scale left switched off is scanned for for the whole session. Accepted: a scale that wakes must connect promptly.
- A user who has only ever connected their scale over WiFi no longer gets a Bluetooth fallback until they connect it once over Bluetooth.
- Manual: no change. It says saved scales reconnect automatically and Forget removes a scale, both still true.
