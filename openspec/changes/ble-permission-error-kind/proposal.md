## Why

The BLE error dialog told a permission prompt from a generic error by searching the message for the English words "Location", "Bluetooth" and "permission". The message is already translated, so in every other language the prompt lost its "open settings" buttons.

## What Changes

- `BLEManager` emits `permissionNeeded(PermissionKind, message)` for Location/Bluetooth permission prompts and for Location services being off.
- `errorOccurred` stays the generic channel.
- The QML dialog takes its title and settings button from the kind and shows the translated message as given.

## Capabilities

### Modified Capabilities
- `ble-error-surfacing`: permission prompts are classified by kind, not by message text.

## Impact

`src/ble/blemanager.{h,cpp}`, `qml/main.qml`.
