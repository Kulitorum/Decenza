## 1. Implementation

- [x] 1.1 Read the account's machine list at sign-in; keep it and the user's choice with the account (cleared on sign-out)
- [x] 1.2 Port Decaid's resolver: choice, only DE1, only DE1 of the machine's model; SKU parsing at token boundaries
- [x] 1.3 Tell a DE1 that answered the serial read with 0 apart from one not yet read
- [x] 1.4 File first uploads from such a DE1 under the resolved serial; refuse with `NoSerial` when none is settled
- [x] 1.5 Ask which DE1 it is in a one-time dialog when the account has several

## 2. Verification

- [x] 2.1 Tests: resolver table, sign-in reads the list and the choice settles the serial, `NoSerial` result
- [x] 2.2 Full suite green through Qt Creator (119/119; the new assertions go red when the resolver or NoSerial mapping is broken)
- [ ] 2.3 Reporter of #2013 signs out and in again on a build with this change, and shots upload — HELD at merge (2026-10-07): needs a build containing this change; tracked on #2013

## 3. Docs

- [x] 3.1 `docs/CLAUDE_MD/DECENT_UPLOAD.md` serial section
- [x] 3.2 Wiki manual: Decent Account Upload section (wiki a18e8a5)
