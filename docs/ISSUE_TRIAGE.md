# Issue Triage — 2026-02-09

Open issues triaged against release notes through v1.4.1-beta. 23 remain open, 17 closed as fixed/stale/duplicate.

## Closed Issues

| # | Reason |
|---|--------|
| #129 | BLE stack replaced in v1.3.9 (Nordic Java → Qt native). Shot-abort-no-scale safety added. |
| #131 | Fixed in v1.3.8 (steam temp widget added) |
| #142 | Fixed in v1.4.1-beta (configurable default rating) |
| #141 | Improved in v1.4.1-beta (suggestion dropdowns) |
| #100 | Fixed in v1.4.1-beta (autocomplete for fields) |
| #106 | Fixed in v1.2.5 (grind size in history list) |
| #143 | Already closed |
| #86, #65, #62, #61 | Stale crashes on old versions (v1.2.0-v1.2.10) |
| #104, #98 | iOS crashes on v1.2.12, fixed by v1.3.1 suspend crash fix |
| #135, #128 | Duplicates of #139 (BLE receiver leak) |
| #111 | Fixed: FlowScale puck absorption model (3c51bf7). ±0.5g accuracy |
| #159 | Fixed: Flow calibration removed entirely (3c51bf7). Raw integral is accurate without calibration |

## Tier 1 — Critical (shot safety)

| Priority | # | Issue | Notes |
|----------|---|-------|-------|
| 1 | #140 | New bean not applied to shot | Bean visually selected but old bean recorded. v1.3.9 |
| 2 | #93 | Scale disconnects mid-shot | Lunar BLE drops during extraction |

## Tier 2 — Crashes (stability)

| Priority | # | Issue | Notes |
|----------|---|-------|-------|
| 3 | #139 | Android BLE receiver leak | `Too many receivers (1000)` after long sessions. Qt BLE bug, Teclast P80X, Android 9 |
| 4 | #156 | iOS SIGSEGV during BLE write retry | Crash in FrameWrite timeout handler on iOS 26.1 |
| 5 | #148 | iOS SIGSEGV on app resume | Crash writing to Skale after ~30hr suspend/resume |
| 6 | #149 | Android DeadSystemException | System-level BLE crash on TrebleDroid. Low actionability |

## Tier 3 — Significant Bugs

| Priority | # | Issue | Notes |
|----------|---|-------|-------|
| 7 | #134 | Notes persist despite "clear" setting | Notes keep uploading to Visualizer repeatedly |
| 8 | #59 | Auto Sleep not working | Machine stays on. Old report (v1.2.8), no fix found |
| 9 | #126 | Water level not sent to DE1 | Refill trigger wrong. May be Decent API issue |
| 10 | #130 | iOS VoiceOver ghost overlay + keyboard | VoiceOver announces app name on empty space; keyboard won't open on Translation page |
| 11 | #114 | Text field scrolls off screen | Notes/AI field invisible when typing. KeyboardAwareContainer issue |
| 12 | #121 | MQTT profile set not working | Works for starred profiles but not others |
| 13 | #132 | Weight data wrong on Visualizer | Cumulative weight instead of flow rate. PR #138 may address |
| 14 | #69 | Flow graph shows cumulative weight | Related to #132. Design decision |

## Tier 4 — Feature Requests

| Priority | # | Issue | Notes |
|----------|---|-------|-------|
| 15 | #99 | Manual "next step" button | Safety net when frame advance fails |
| 16 | #133 | BLE scale reconnection improvements | Partially addressed in v1.4.0-beta |
| 17 | #127 | Water auto-refill level adjustment | Partially addressed in v1.4.1-beta |
| 18 | #123 | Screen off time intervals | Night mode |
| 19 | #122 | Web interface: compare shots + grind size | Shot comparison UX |
| 20 | #115 | Preserve filter when returning from shot | Navigation state persistence |
| 21 | #96 | Better shot history browsing | Group by bean, sorting |
| 22 | #95 | Shot review linked to bean preset | Workflow shortcut |
| 23 | #83 | Dual scale support | Niche advanced feature |
| 24 | #14 | Keyboard behavior improvements | Long-standing, partially improved |
