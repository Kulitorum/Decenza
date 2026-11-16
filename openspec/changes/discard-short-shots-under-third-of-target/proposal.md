## Why

A 1.5 s group rinse with no cup was saved as a shot (2026-11-15): the water landed on the scale as 6.4 g, clearing the fixed 5 g yield threshold, against a 32 g target. Its review page then showed a stub graph.

## What Changes

- The aborted-shot classifier's yield clause becomes relative to the shot's stop target: under 10 s AND under the larger of 5 g and a third of the stop target.
- Shots with no weight target keep the fixed 5 g threshold.

## Impact

- Affected spec: `shot-save-filter`
- Affected code: `src/controllers/abortedshotclassifier.h`, `src/controllers/maincontroller.cpp`, `tests/tst_aborted_shot_classifier.cpp`
- A short shot stopped by hand below a third of its target (for example 9 s and 9 g of 32 g) is now discarded too. The maintainer chose a third over a half to leave room for intentionally short shots; the figure was chosen, not validated against a corpus.
