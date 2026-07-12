## 1. Remote surface + capability token (shared by all modes)

- [ ] 1.1 Add `src/mcp/mcpremoteaccess.{h,cpp}`: coordinator owning the
      remote loopback listener, token lifecycle, mode switching, and
      status (`off | starting | active | reconnecting | error`)
      exposed to QML.
- [ ] 1.2 Dedicated loopback listener that serves **only**
      `POST/GET/DELETE /mcp/<token>`; every other path/method returns
      a bare `404`. Forward matching requests in-process to
      `McpServer` dispatch, with the session flagged remote.
- [ ] 1.3 Token: 128-bit CSPRNG, base64url, generated on first enable;
      constant-time comparison; "rotate token" slot that closes
      active remote sessions immediately.
- [ ] 1.4 Per-source rate limit on failed-token requests; log at
      warning level without echoing the attempted path.
- [ ] 1.5 Settings (in `settings_network`): `remoteMcpEnabled`
      (default false), `remoteMcpMode` (tailscale | ngrok | custom),
      `remoteMcpToken`, `remoteMcpCustomBaseUrl`, ngrok
      authtoken/domain. Token + ngrok authtoken via QtKeychain when
      available, AES-GCM-encrypted QSettings fallback.
- [ ] 1.6 Remote sessions respect existing `mcpAccessLevel`,
      `mcpConfirmationLevel`, session caps, and rate limits (verify —
      no new gates, no bypasses).

## 2. Mode C — BYO public URL (ships first)

- [ ] 2.1 Custom base URL field; validate `https://` scheme; display
      the composed connector URL `<base>/mcp/<token>`.
- [ ] 2.2 QR code + copy button for the connector URL.
- [ ] 2.3 Docs: recipes for Tailscale Funnel on a home box,
      cloudflared named tunnel (user-owned domain), generic reverse
      proxy — all forwarding to the tablet's LAN IP + remote port.

## 3. Mode A — Embedded Tailscale (tsnet) + Funnel

- [ ] 3.1 Minimal Go library wrapping tsnet: `Up(stateDir)`, `Down()`,
      `Status()`, `LoginURL()`, `FunnelURL()`, `ListenFunnel` →
      forward to the loopback remote listener. Build as AAR via
      gomobile; decide prebuilt-in-repo vs CI-built (open question).
- [ ] 3.2 `src/mcp/mcptunnel_tsnet.{h,cpp}`: JNI binding, lifecycle,
      state persistence dir, reconnect on network change
      (`QNetworkInformation`), off-main-thread.
- [ ] 3.3 Setup flow in Settings: show tsnet login URL as QR/link;
      poll status until node is authorized; surface the
      funnel-attribute approval URL when Tailscale requires it.
- [ ] 3.4 Display resulting stable URL
      `https://<node>.<tailnet>.ts.net/mcp/<token>`; verify cert
      provisioning end-to-end.
- [ ] 3.5 Gradle packaging; gate behind a CMake option so non-tsnet
      builds still link; measure APK size delta.
- [ ] 3.6 Logout/disable: `Down()`, wipe tsnet state dir on explicit
      "forget this tailnet".
- [ ] 3.7 Desktop (Windows/macOS/Linux): build the same Go wrapper as
      a `c-shared` library, link via the identical C API; verify
      coexistence with an installed Tailscale client.
- [ ] 3.8 iOS: gomobile xcframework build of the same wrapper;
      sequenced after Android + desktop ship.

## 4. Mode B — Embedded ngrok

- [ ] 4.1 **Spike first**: confirm Anthropic's connector backend is
      not blocked by ngrok's free-tier browser interstitial. If
      blocked, mark Mode B not-shippable and stop here.
- [ ] 4.2 Integrate `ngrok-java` (Android); authtoken + static domain
      fields in Settings; tunnel to the remote loopback listener.
- [ ] 4.3 Reconnect/backoff on network change; status surfacing.

## 5. Settings UI

- [ ] 5.1 Remote MCP section (extend `SettingsAITab.qml` MCP section
      or new `RemoteMcpTab.qml`): enable toggle, mode selector
      (tap-to-open dialog per accessibility rules, not ComboBox),
      per-mode setup fields, status line, connector URL + QR + copy,
      rotate-token button with confirm dialog.
- [ ] 5.2 Setup guidance text: where to paste the URL on claude.ai
      (Settings → Connectors → Add custom connector) and note that
      mobile apps sync connectors from claude.ai.
- [ ] 5.3 i18n keys under `settings.remoteMcp.*`; all text via
      `TranslationManager`/`Tr`.
- [ ] 5.4 Accessibility pass per `ACCESSIBILITY.md` (toggle, dialog,
      QR alt text, status announcements).

## 6. Tests

- [ ] 6.1 Unit: token generation/rotation, constant-time compare,
      remote-listener route gating (non-MCP path → 404, wrong token →
      404, valid token → dispatched).
- [ ] 6.2 Unit: remote session honors access level + confirmation
      level (reuse existing MCP test fixtures).
- [ ] 6.3 Unit: failed-token rate limiting.
- [ ] 6.4 Integration: `initialize` → `tools/list` → `tools/call`
      through the remote listener via loopback.
- [ ] 6.5 Manual QA: add connector on claude.ai web, verify it
      appears and works in Claude iOS and Claude Android; exercise a
      control tool with confirmation dialog through the tunnel;
      rotate token and confirm the old URL dies.

## 7. Documentation

- [ ] 7.1 Update `docs/CLAUDE_MD/MCP_SERVER.md`: remote access
      section (modes, token model, isolated surface, limitations —
      no wake-from-doze, tunnel-vendor dependency).
- [ ] 7.2 Wiki manual page: end-user setup walkthrough per mode with
      screenshots (claude.ai connector config + mobile).
- [ ] 7.3 Security note: what the capability URL grants, why rotation
      is revocation, interaction with access/confirmation levels.
