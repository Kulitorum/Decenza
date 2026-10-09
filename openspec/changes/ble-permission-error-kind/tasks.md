## 1. Signal and dialog

- [x] 1.1 Add `PermissionKind` (Q_ENUM) and `permissionNeeded(kind, message)`; emit it from the four permission sites and the LocationServiceTurnedOff / MissingPermissions scan errors.
- [x] 1.2 In `main.qml`, route both signals through `showBleError()`. The kind picks the title and button; Location services off uses a translated text.

## 2. Verification

- [ ] 2.1 On Android, in a non-English language: deny Location, then deny Bluetooth. Each dialog shows its "open settings" button.
