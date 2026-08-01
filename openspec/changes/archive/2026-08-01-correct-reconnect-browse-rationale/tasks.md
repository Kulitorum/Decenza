## 1. Correct the record

- [x] 1.1 Rewrite the rationale in "Saved WiFi scale reconnect resolves by service browse" to describe the observed failure state rather than assert a property of the responder, and record the 357 ms counter-example so the old claim cannot be re-derived.
- [x] 1.2 Weaken the "not an Android-only defect" framing to what one macOS occurrence supports: same symptom, cause not established, cross-platform fallback kept as a hedge.
- [x] 1.3 Fix the same claim in `src/ble/scales/decentscalewifi.cpp` and `src/ble/blemanager.cpp`.

## 2. Verify and land

- [x] 2.1 No behaviour change, so no new tests — but run the full local suite to confirm the comment edits compile clean.
- [x] 2.2 Archive this change before merge.
