# Proposal

## Why

A milk capture ends its request on the first reading that holds for the dwell. A user who pours with the pitcher on the scale and pauses mid-pour gets that partial weight, and the rest of the pour is ignored, so the steam runs short (Kulitorum/Decenza#2058: 77.6 g captured 7.5 s after the pitcher was set down, with a heavier reading before it was lifted).

## What Changes

- A request stays open after a capture while the pitcher is on the scale. A reading that moves at least 2 g and settles again replaces the earlier capture.
- Lifting the pitcher after a capture ends the request, so a cup set down afterwards is still never taken for milk.
- A manual ±5 is still kept: a later top-up is weighed but does not re-time the steam.
- The review-page button shows the captured weight while the pitcher is still on the scale.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `weight-timed-steaming`: "Automatic milk capture before steaming" ends a request on the lift after a capture rather than on the capture.

## Impact

- `src/controllers/preparationcontroller.{h,cpp}`: the request closes at the load-removed edge; milk `rearmDelta` 6 g to 2 g. `StableWeightCapture` is unchanged.
- `qml/pages/PostShotReviewPage.qml`: one label condition.
- Manual: no change. "Wait for the milk weight to be captured, then lift the pitcher" still describes it.
