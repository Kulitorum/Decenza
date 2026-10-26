## 1. Implementation

- [x] 1.1 Shift the learned empty reading when a tare lands; drop a tare that never lands (`StableWeightCapture::tare`)
- [x] 1.2 Apply it only to tares while preparing; reset as before during an operation (`PreparationController::onTareCompleted`)
- [x] 1.3 Send every app tare through `MachineState::tareScale()` so the capture hears it
- [x] 1.4 Tests: tare with the pitcher on, tare that never lands, hot water's dropped tare
