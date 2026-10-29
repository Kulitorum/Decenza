# PORTAL review and delivery

Source: the 21 owner messages in Antigravity conversation
`78647781-cad3-423f-b6a9-f21046c50dde` and its 27 October handoff.

## Required behavior

Keep mounting selection, mass-weighted EC, history/review access, paired
refractometer calibration, uncertainty notes and taste-informed ratio advice.
PORTAL remains advisory and never changes machine control or stop-at-weight.

## Corrections

- Integrate all recorded EC observations against monotone scale mass, respecting
  plateaus, segment boundaries, partial mass windows and the actual stop time.
  Missing tail data must not become a low-concentration tail.
- Require evidence of a physical scale; reject virtual or unknown provenance.
- Exclude the target shot and unrelated beans from its calibration. Preserve
  mounting and reliability in stored summaries and restrict calibration inputs.
- Scope rated reference cups by coffee, equipment and profile. Use actual taste
  feedback; do not infer the current cup's taste from another cup or arbitrary
  substrings in notes. EC contribution is a relative proxy, not remaining solids
  or proof that a cup is finished. Keep advice conditional and experimental.
- Make history and review agree on estimates and uncertainty. Remove duplicated
  advice in QML, expose warnings accessibly and keep translatable presentation.
- Do not overwrite user notes while reading a shot. Save warnings with new shots.
- Update the manual in the fork and document physical validation still required.

## Verification

The owner authorized GitHub Actions instead of unavailable Qt Creator MCP.
Run owner-boundary regression tests against the original behavior, then the
full Linux suite and native/Wasm lint/build checks on the corrected commit.
Run repository text-invariant checks. Build a signed Android APK with the
existing release workflow, verify its commit and downloadable artifact.

No physical tablet, BLE, refractometer calibration or native/browser interaction
is claimed by CI evidence. No upstream merge, public release replacement or
device installation is part of this task.
