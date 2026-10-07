## MODIFIED Requirements

### Requirement: Machine identity in the payload

Every uploaded document SHALL carry `machine.serialNumber`, and SHALL carry `machine.firmwareVersion` and `machine.model` when known. For a first upload these SHALL come from the DE1 connected at the time of upload; the serial is not stored with the shot itself. A replacement of an already-uploaded shot SHALL reuse the serial the shot was first uploaded under, so it lands on the same machine in the account. If no DE1 is connected when a first upload is due, the shot SHALL NOT be sent or marked rejected, and stays offered by Upload missing shots. A DE1 that answers the serial read with 0 SHALL be filed under a DE1 from the account's machine list, read at sign-in from `/support/api/sn?onlyespressomachines=1&withskus=1` and kept until sign-out: the DE1 the user chose for it, else the account's only DE1, else the only DE1 of the connected machine's model. When several DE1s remain and none is chosen, the user SHALL be asked which one in a dialog, at most once each time the app starts or the account is signed in, and there SHALL be no permanent on-screen control for it. Until a serial is settled, a first upload SHALL NOT be sent or marked rejected, and the user SHALL be told to sign out of the Decent account and sign in again. In simulation mode the machine reports the fixed serial `SIM-DE1`. It is not a number, so no Decent account can own it, and the server refuses its uploads (403): the simulator exercises sign-in, the request and the response handling, and with `SIM-DE1` no simulated shot can reach an account. For testing a full upload, MCP `settings_set` `simulatorSerialNumber` SHALL make the simulator report a given serial until the app restarts. It SHALL never be saved and SHALL have no on-screen control; an empty value restores `SIM-DE1`.

#### Scenario: Shot uploaded right after it is pulled
- **WHEN** an eligible shot is saved while a real DE1 is connected
- **THEN** it is uploaded with that machine's serial as `machine.serialNumber`

#### Scenario: Simulator with a test serial
- **WHEN** a developer sets `simulatorSerialNumber` to their machine's serial over MCP and uploads a simulated shot
- **THEN** it is sent under that serial, and after the app restarts the simulator reports `SIM-DE1` again

#### Scenario: No machine connected
- **WHEN** a never-uploaded shot is due and no real DE1 is connected
- **THEN** it is not sent and remains offered by Upload missing shots

#### Scenario: Replacement after switching machines
- **WHEN** a shot first uploaded under serial A is edited while a machine with serial B is connected
- **THEN** the replacement is sent with serial A

#### Scenario: DE1 that reports no serial, one DE1 in the account
- **WHEN** the connected DE1 reports serial 0 and the account's machine list has one DE1
- **THEN** shots are uploaded under that DE1's serial

#### Scenario: DE1 that reports no serial, several DE1s in the account
- **WHEN** the connected DE1 reports serial 0 and the account has several DE1s, none matching only by model
- **THEN** a dialog asks which one it is, once per app run or sign-in, and shots are uploaded under the one chosen

#### Scenario: Signed in before the machine list was read
- **WHEN** the connected DE1 reports serial 0 and the account has no machine list
- **THEN** a first upload is not sent, and the user is told to sign out and sign in again
