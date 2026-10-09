# Verify custom backgrounds on a physical tablet

## Why

The shipped custom-background feature passed current desktop source, rendering, persistence and deletion checks, but physical-tablet touch and screen-reader verification was unavailable. On October 8, 2026 the user explicitly accepted archiving the implementation with these checks tracked here instead of reported as passed.

## What Changes

- Record personal/cached-stock image selection and readable content on the tablet in dark/light mode.
- Verify picker touch and TalkBack focus/order/actions, plus tablet restart persistence, None and removal of an owned disposable backing image.
- Save source/build/device, input and observed result; open a scoped defect if a check fails.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

None. This is verification only; skip_specs is explicit.

## Impact

Evidence and task records only. No app behavior or settings migration is proposed. The archived implementation record retains the desktop evidence and the user's accepted validation disposition.
