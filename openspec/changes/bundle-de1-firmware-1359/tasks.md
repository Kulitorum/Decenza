## 1. Firmware bundle

- [x] 1.1 Add `resources/firmware/de1/de1-1359.bin` (SHA-256 `4b9ed1c1…dbd1cc81`, header build 1359) and remove `de1-1352.bin`; update `resources.qrc`.
- [x] 1.2 Manifest: 1358 on `stable`, 1359 on `early-access`, each with header fields, digest, release notes and provenance.
- [x] 1.3 Resolve each channel from the manifest's `channel` field instead of hard-coded artifact ids.

## 2. Validation and docs

- [x] 2.1 Update the firmware tests for the new builds, and assert exactly one manifest entry per channel.
- [x] 2.2 Update `docs/CLAUDE_MD/FIRMWARE_UPDATE.md`.
- [x] 2.3 Wiki manual: checked. `Manual.md` names channels but no build numbers, so no edit is needed.
- [ ] 2.4 Full test suite green through Qt Creator.
