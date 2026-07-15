## 1. C++ — expose sign-out

- [x] 1.1 Add `Q_INVOKABLE void forgetTailscale();` to `McpRemoteAccess` (`src/mcp/mcpremoteaccess.h`), near `rotateToken()`.
- [x] 1.2 Implement `forgetTailscale()` in `src/mcp/mcpremoteaccess.cpp`: if `m_tunnel` is non-null call `m_tunnel->wipeState()`; then call `refresh()` so a fresh node comes up when remote MCP is still enabled in Tailscale mode. Guard against no-tunnel (Mode C / tunnel unavailable) as a no-op.
- [x] 1.3 Confirm no header include of `settings_<domain>.h` is added to `settings.h` and no new `Settings` property is introduced (none needed — action is stateless).
- [x] 1.4 Default `ENABLE_TSNET` ON for `APPLE OR ANDROID` (macOS/iOS/Android) in `CMakeLists.txt` so local dev builds include Tailscale mode and match what CI ships; keep OFF on Windows/Linux (no prebuilt artifact). This makes the sign-out button reachable in normal builds on those platforms.

## 2. QML — sign-out button + confirmation dialog

- [x] 2.1 In `qml/pages/settings/SettingsAITab.qml`, add a "Sign out of Tailscale" `AccessibleButton` inside the Tailscale block, gated by `Settings.mcp.remoteMcpMode === "tailscale" && RemoteMcpAccess.tunnelAvailable`, placed at the block level (NOT nested under the `loginUrl`/`connectorUrl` sub-blocks) so it is reachable during the login loop.
- [x] 2.2 Add a confirmation dialog cloned from `rotateTokenDialog` (destructive wording, Cancel/Sign out, `AccessibilityManager.announce` on open); on confirm call `RemoteMcpAccess.forgetTailscale()` and close.
- [x] 2.3 Add translation keys under `settings.ai.remoteMcp.tailscaleSignout.*` (button label, hint, dialog title, dialog body, accessible names); reuse `common.button.cancel` where it fits. (Inline fallbacks, matching the existing rotate-token keys — no central registration in this project.)
- [x] 2.4 Apply accessibility attributes on the new button and dialog (`AccessibleButton` + `AccessibilityManager.announce`, matching the rotate dialog).

## 3. Verify

- [x] 3.1 Quick compile check via Qt Creator MCP (build the current project target). — Build succeeded, 0 errors / 0 warnings.
- [ ] 3.2 Ask Jeff to launch the app and confirm: with Tailscale mode enabled and the node in the login-URL state, the sign-out button is visible, the confirm dialog appears, and confirming wipes state and produces a fresh login URL. Confirm no `qrc:/…qml` TypeErrors in the running app log.
- [ ] 3.3 Confirm the button is hidden in Custom-URL mode and when `tunnelAvailable` is false.

## 4. Docs

- [x] 4.1 Update the GitHub wiki Manual (remote-access / Tailscale section): document the "Sign out of Tailscale" action and add a short "stuck in a login loop (device already exists / 403)" recovery note pointing to it plus deleting the stale device in the Tailscale admin console. — Edited local wiki clone; commit/push pending user confirmation (publishing to public wiki).

## 5. Archive

- [ ] 5.1 As the final commit on the branch before merge, run `/opsx:archive` for `add-tailscale-signout` so the archive + spec promotion lands in the PR.
