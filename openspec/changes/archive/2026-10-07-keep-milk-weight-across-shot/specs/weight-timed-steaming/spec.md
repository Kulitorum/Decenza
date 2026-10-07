## ADDED Requirements

### Requirement: Fallback-to-fixed-duration is diagnosable from the debug log

When weight-timed steaming applies the fixed duration instead of a weight-scaled one, the debug log SHALL show why: the milk inputs, the toggle, the steam rate, the selected pitcher, the computed and the applied duration. It SHALL cover the steam-start decision and the page-activation/pitcher-lift sync, whether steam was started from the app or the group head.

#### Scenario: Fallback due to missing calibration is visible in the log
- **WHEN** steaming starts with milk captured but the global steam-seconds-per-gram rate is uncalibrated
- **THEN** the debug log shows the captured milk value, the toggle as enabled, the calibration value as unset/zero, and the applied duration as the fixed fallback

#### Scenario: Fallback due to no captured milk is distinguishable from a calibration gap
- **WHEN** steaming starts with no milk captured (session value and last on-scale value both zero)
- **THEN** the debug log shows both milk sources as zero, distinguishing this case from an uncalibrated-but-milk-present fallback

#### Scenario: Successful scaling is also logged
- **WHEN** steaming starts with milk captured, the toggle enabled, a valid calibration, and an enabled pitcher
- **THEN** the debug log shows the computed scaled duration and records it as the applied source, not the fallback
