## Why

Some older DE1s answer the serial-number read (MMR `0x803830`) with 0. Decenza treats 0 as "no machine", so a user with one of these machines can never upload a shot to their Decent account, even though the account has the machine on file (#2013: firmware 1358, PCB 1.0, `Serial number read: 0` on every connect). Decaid handles this case with its `LegacyDe1IdentityResolver`, which takes the serial from the account's own machine list.

## What Changes

- At sign-in, read the account's espresso machines from `/support/api/sn?onlyespressomachines=1&withskus=1` and keep the list with the account.
- When the connected DE1 reports serial 0, file its shots under a DE1 from that list, as Decaid does: the one the user chose, the account's only DE1, or the only one matching the machine's model.
- With several DE1s and no choice yet, ask in a dialog, once each time the app starts or the account is signed in. There is no permanent on-screen control.
- A first upload that has no serial for this reason is refused with its own message (sign out and in again), not "connect your DE1".

## Capabilities

### Modified Capabilities
- `decent-shot-upload`: the machine identity requirement gains a fallback serial from the account for a DE1 that reports none.

## Impact

- `src/network/decentaccount.*`, `src/core/settings_decent.*`, `src/ble/de1device.*`, `src/network/decentshotuploader.*`, `src/network/decentshotrecord.h`, `src/controllers/maincontroller.cpp`
- `qml/main.qml` (choice dialog), `qml/components/DecentUploadStatus.qml`
- Users already signed in must sign out and in again for the list to be read.
