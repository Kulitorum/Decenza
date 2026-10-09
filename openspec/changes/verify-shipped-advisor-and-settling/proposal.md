# Verify shipped advisor and settling behavior

## Why

The implementations in PRs [#1067](https://github.com/Kulitorum/Decenza/pull/1067) and [#1282](https://github.com/Kulitorum/Decenza/pull/1282) shipped with automated evidence, but their live verification was not recorded. The user accepted archiving those implementation changes on October 8, 2026 while retaining the outstanding checks here.

## What Changes

- Record an anchored bean correction's persisted shot metadata and the next advisor envelope, including whether `currentBean` actually reflects the correction with today's bag model.
- Record a current-device cup-lift shot's scale reading, saved yield, and stop reason.
- Verify profile-family and other-profile parameter guidance in an existing advisor session; retain unresolved automated integration/prompt-prefix checks from the old metadata task list as evidence gaps.
- Record source revision, app build, device, input and observed result. A missing result remains pending; an archive is not evidence of success.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

None. This is verification only; `skip_specs: true` prevents new behavior requirements.

## Impact

Evidence and task records only. No application, firmware, provider, or settings changes. If verification finds a defect, capture a separate scoped fix rather than declaring this check successful.
