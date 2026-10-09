## 1. Signal and dialog

- [x] 1.1 Add `PermissionKind` (Q_ENUM) and `permissionNeeded(kind, message)`; emit it from the four permission sites and the LocationServiceTurnedOff / MissingPermissions scan errors.
- [x] 1.2 In `main.qml`, route both signals through `showBleError()`. The kind picks the title and button; Location services off uses a translated text.
- [x] 1.3 A generic error does not replace an open permission prompt, the rule the queue already applies; it is queued and shown when the prompt closes.

## 2. Verification

- [ ] 2.1 On Android, in a non-English language: deny Location, then deny Bluetooth. Each dialog shows its "open settings" button.

## 3. Docs

- [x] 3.1 Wiki manual: nothing to change. The dialogs read and act the same in English; other languages now get the settings button the manual already describes.
