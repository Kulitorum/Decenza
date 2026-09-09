# Inherited validation and publication holds

On 2026-09-09 the user requested merge of PR #1930 while holding beta builds, then
required CLI archival as the PR's final commit. The change was archived using
`openspec archive normalize-app-diagnostic-logging --yes --json`; its archive commit
was `f92c76be36fb9e4fd3d0c73499d091d491afe364`, and the squash merge is
`5c2f1d585bde0fd5675eae91988fa89de7208b29`.

Source record: `../archive/2026-09-09-normalize-app-diagnostic-logging/tasks.md`
and `evidence.md`. That archive retains tasks 3.6 and 5.2 unchecked for their
outstanding portions. The prior Mac suite passed all 117 suites through Qt Creator
MCP (run 1788809091554); those results do not validate this follow-up; its Mac results are in `evidence.md`.

| Outstanding work | Origin | Resume condition and evidence |
| --- | --- | --- |
| Android, iOS and other platform compilation | Prior task 5.2; this task 6.1 | The user lifts the beta-build hold. Record beta run, source revision, platform and result. Do not substitute an unsolicited platform test workflow. |
| Updated-device charge/mismatch observation | Prior task 3.6; this task 6.2 | A mobile build containing the migration is available. Retrieve its representative log through the DE1 MCP and verify truthful command-versus-sample wording. Existing update/R2/calibration checks already passed on Mac. |
| Short wiki guidance publication | Prior task 6.1 delivered a prepared patch; this task 6.3 | Publish with the shipped feature; reconcile with the then-current wiki first. The reviewed patch is `../archive/2026-09-09-normalize-app-diagnostic-logging/wiki-manual.patch`. |

The hold is not a successful validation result. Keep the relevant tasks unchecked
until their evidence exists. If another archive occurs before these items resume,
carry this record into an explicit successor without silently dropping it.

All local builds/tests use Qt Creator MCP on Mac. Do not start additional Decenza
copies: verify process and port ownership first and target the exact checkout.
MQTT is intentionally disabled. No paid AI calls or machine commands are required
to validate the follow-up diagnostics.
