# Tasks

- [x] 1.1 Arm the milk detector only while an attempt is open and the selected pitcher is weight-timed with a saved empty weight (`PreparationController::milkSampleBlocker`)
- [x] 1.2 Stop `selectMode("steam")` from opening an attempt; keep an open attempt across a pitcher change; end it when any non-steam operation starts
- [x] 1.3 Cache the last on-scale milk only while a sample is offered; relock a manual ±5 on pitcher lift regardless of arming
- [x] 1.4 Hold the attempt per owner (`<clientId>/<surface>`); release a client's owners on `remoteObjectClientClosed`; no restore when withdrawn mid-operation
- [x] 2.1 Offer/withdraw helper in `HomePresetActions`, per surface, always sending the withdraw
- [x] 2.2 Idle Steam row and compact Steam popup offer on open and withdraw on close or page change
- [x] 2.4 One `SteamPitcherPicker` for the Home row and the bar popup; picking a pitcher keeps the popup open, starting steam or a purge closes it
- [x] 2.5 Live net-milk weight on the selected pitcher's pill only
- [x] 2.3 Review button: tap offers, further taps cycle the pitcher (the label shows the new name for 2 s), leaving the page withdraws
- [x] 3.1 Controller tests: no capture without an offer, after a steam, or after the one sample; offer gated on pitcher; pitcher change keeps the offer
- [x] 3.2 Full suite green through Qt Creator (207/207 after 2.5)
- [ ] 3.3 Wiki manual: weight-timed steaming entry describes when milk is weighed (drafted in the local wiki clone, not yet published)
