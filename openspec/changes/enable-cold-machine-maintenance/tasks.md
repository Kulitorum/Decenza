# Tasks — Enable Cold-Machine Maintenance (ON HOLD)

> **Do not start** until the blocking firmware is released and available for
> validation. See proposal.md status banner.

## 0. Unblock gate
- [ ] 0.1 Confirm the DE1 firmware build that natively honors cold maintenance on
  GHC hardware is publicly released.
- [ ] 0.2 Validate on real GHC hardware that a cold `AirPurge`/`Descale` request
  is honored on that build (and confirm the exact threshold — reaprime's 1356 is
  an estimate).

## 1. Firmware support
- [ ] 1.1 Ensure Decenza recognizes the fixed build and gates behavior on the
  reported `m_firmwareBuildNumber`.
- [ ] 1.2 If the fix is to be bundled/offered, wire it into the existing firmware
  update path.

## 2. Relax the ready gate (firmware-conditional)
- [ ] 2.1 Make Transport Mode's "must be ready" precondition conditional on the
  firmware build being below the threshold.
- [ ] 2.2 Apply the same firmware-conditional relaxation to any descale start gate.

## 3. Docs
- [ ] 3.1 Update the wiki Maintenance section: cold starts supported on the fixed
  firmware; keep the older-firmware caveat.

## 4. Verify & review
- [ ] 4.1 On supported firmware, start Transport from a cold machine and confirm
  the drain runs.
- [ ] 4.2 On older firmware, confirm the ready gate still applies.
- [ ] 4.3 Run `/pr-review-toolkit:review-pr` before merge.

## Fallback (only if the firmware fix is abandoned)
- [ ] F.1 Instead of waiting, port the pre-maintenance-profile workaround (see
  design.md "Rejected"): fold cold-maintenance prep into `requestState()`, gated
  on `isMaintenanceState && cold && ghcPresent`, sending a 1°C / tank-threshold-0
  profile before the state. Decide explicitly before doing this.
