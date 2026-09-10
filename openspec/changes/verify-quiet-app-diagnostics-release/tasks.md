# Release follow-up

These checks remain outstanding until evidence is recorded. The user authorized
the updated v2.0.5 prerelease on September 9, lifting the build/publication hold.
No new logging behavior is proposed here.

- [x] 1. Verify Android, iOS and other beta builds containing PRs #1930 and #1931. Record source revision, platform, run and result.
- [ ] 2. Retrieve a representative updated mobile charging/mismatch log through DE1 MCP. Verify requested-versus-observed charge wording, meaningful transitions and suppression; distinguish measured volume from the historical estimate.
- [x] 3. Publish the short wiki logging guidance with the released feature. Reconcile the current wiki with both archived patches and verify the rendered page.

Source records (repository-relative paths):

- `openspec/changes/archive/2026-09-09-normalize-app-diagnostic-logging/tasks.md` and that change's `evidence.md` and `wiki-manual.patch` are the original evidence.
- `openspec/changes/archive/2026-09-09-improve-visualizer-operation-logging/validation-holds.md`, `evidence.md` and `wiki-manual.patch` carry the latest scope and evidence.

All local builds/tests use Qt Creator MCP on Mac. Ask the user to start/restart
the live app; inspect it in the background and verify one instance. MQTT remains
excluded. A memory sample or FD census alone is not evidence of a leak.
