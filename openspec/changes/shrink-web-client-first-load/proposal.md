# Proposal

## Why

Over Tailscale, the recommended way to reach Decenza from a phone, the first load of the web interface takes about 35 s, and 28 s of that is a single file download. Measured on an iPhone 17 (iOS 27.2) against the SM-X210 host, 2026-10-26:

| Path | Cache | `decenza-web.wasm` transfer | Usable home screen |
|---|---|---|---|
| Home network | empty | 29.6 MB in 0.8 s | about 3–11 s |
| Home network | cached | none | about 3.3 s |
| Tailscale | empty | 29.6 MB in 27.7 s (about 8.5 Mbit/s) | about 35 s |
| Tailscale | cached | none | about 3.5 s |

Compiling the code and starting QML on the phone takes about 3 s. The rest of a cold load is bytes moving through the tunnel. Every host update changes the asset hash, so the full download repeats after each update, not just on a phone's first visit. The interface's startup budget (`replace-web-interface-with-wasm` design: usable within 8 s cold at 20 Mbit/s) can only be met by sending fewer bytes.

## What Changes

- **Brotli replaces gzip.** The host serves a precompressed Brotli copy of the client assets, with the uncompressed bytes as the fallback. Every origin that serves the client is HTTPS (the LAN server redirects plain HTTP), and every supported browser offers Brotli there, so a gzip copy would never be served. On the current client this cuts the wasm from 29.6 MB (gzip) to 18.8 MB at maximum quality, with no runtime cost. Because the native app embeds the bundle, dropping the gzip copy also makes every native install about 11 MB smaller. Release packaging uses maximum quality. Local builds use a fast setting, which is still smaller (22.1 MB) and quicker than today's gzip step.
- **Emoji and map images load on demand.** The roughly 4,000 Twemoji SVGs (3.5 MB compressed) and the three screensaver map textures (1.1 MB) leave the wasm binary. The browser fetches each image the first time a screen shows it, from the host's own copy, at a URL versioned by the content of the set. These URLs don't change when only the app changes, so a host update doesn't download them again. The list of which emoji exist stays inside the client, so stripping an emoji that has no image still decides immediately, as it does today.
- **Bytecode-only QML, kept only if measured within budget.** Build the client with qmlcachegen `--only-bytecode` and measure both the download saved and the cost on the busiest screens (the live shot graph and telemetry during a shot). Adopt it only if those stay within the interface's input and graph budgets. Otherwise record the measurement and keep compiled QML.
- **The page keeps the runtime itself.** Everyday iOS Safari does not reuse the wasm from its HTTP cache: with Web Inspector attached, a reload and a fresh navigation both re-downloaded all 28,925 kB (25 s over Tailscale), while the `.js` files beside it came from cache. This is why loads take about a minute even when the app should be cached. The page stores the runtime in Cache Storage under its versioned URL and starts from that copy on later visits, falling back to a normal download whenever storage can't be used.
- **Measured against the phone, before and after.** Repeat the iPhone first-visit and repeat-visit loads over Tailscale in everyday Safari and record the results in the evaluation.

## Capabilities

### New Capabilities
<!-- none -->

### Modified Capabilities
- `wasm-browser-client`: adds requirements for compressed asset delivery, on-demand image resources, and a client-retained runtime. This capability is introduced by the in-progress change `replace-web-interface-with-wasm` and is not yet in `openspec/specs/`; this change's requirements are written as additions to it, so that change must archive first.

## Impact

- `scripts/package_webclient.py`, `scripts/build_webclient.py`, `cmake/webclient_bundle.cmake` and `tools/webclient/test_package.py`: produce, record, verify and embed a Brotli sibling in place of the gzip one.
- `src/network/webclientassets.cpp`: negotiate `br` in place of `gzip`, serve the verified Brotli body, and serve versioned emoji and map images from the host's resources.
- `src/webclient/CMakeLists.txt`: stop embedding `emoji.qrc` and the map textures; add the emoji key index. The `--only-bytecode` measurement build was tried here and removed (task 6.2).
- `qml/Theme.qml` and `src/core/emojiassets.{h,cpp}`: resolve emoji images through a per-platform base URL (`qrc:/emoji/` on native, the versioned host path in the browser), with availability answered from an embedded key index in the browser.
- `qml/components/ShotMapScreensaver.qml`: resolve map textures through the same per-platform base.
- `resources/webclient/index.html`: supply Qt's `qt.module` hook from Cache Storage.
- New build prerequisite: the `brotli` command-line tool, wherever the Wasm bundle is packaged (the ubuntu-24.04 Wasm CI job, and local `DECENZA_WEB_BUNDLE_MODE=LOCAL` builds). Like the other Wasm toolchain prerequisites, a missing tool is a setup error.
- No change to the remote route allowlist: the images are served under `/wasm/`, which is already forwarded.
