# Design

## Context

See proposal.md for the measurements. What shapes the approach:

- `scripts/package_webclient.py` hashes each staged asset and writes a gzip sibling, recording `gzipSize` and `gzipSha256` in `package.json`. `src/network/webclientassets.cpp` negotiates `Accept-Encoding`, re-verifies the sibling against the manifest on every read, and serves it with `Content-Encoding: gzip` under `/wasm/<packageId>/…` with `private, max-age=31536000, immutable`.
- Browsers only offer `br` on secure origins, and every origin that serves the client is secure. The Tailscale route is HTTPS, and the LAN server redirects every plain-HTTP connection to HTTPS (`HttpRedirectSslServer`, `shotservertls.h:72`); only `/mcp` is served unencrypted (`shotservertls.h:32-42`).
- The native app embeds the whole verified bundle (`cmake/webclient_bundle.cmake`), so every packaged encoding adds to every native install, Android and iOS included.
- `src/webclient/CMakeLists.txt` embeds `resources/emoji.qrc` (4,009 SVGs, a 9.8 MB `.rcc` that gzips to 3.6 MB) and three map textures (1.15 MB, already-compressed images). The native app embeds the same files in its own resources (`CMakeLists.txt:1363`, `resources/resources.qrc:125-127`). So the host already has every image the client would fetch.
- `Theme.emojiToImage()` returns `qrc:/emoji/<key>.svg`. `EmojiAssets.has()` builds its key set by scanning `:/emoji` and is an invokable on purpose, because the set never changes at runtime (`emojiassets.h:44-48`).
- Qt Quick handles remote images: `Image` and inline `<img>` in `Text.StyledText` load through `QQuickPixmap` asynchronously and re-lay out when the image arrives (`qquicktext.cpp:1331-1336`). On Wasm, network requests go through `emscripten_fetch` on XHR (`qnetworkreplywasmimpl.cpp:271`). A same-origin XHR sends cookies, so the remote admission cookie comes along.
- qmlcachegen's `--only-bytecode` is a per-module switch (`Qt6QmlMacros.cmake:1291`). All shared QML is one module, so it can't be applied to individual screens.

## Goals / Non-Goals

**Goals:**
- Cut the bytes a cold load must move before the first usable screen, and the bytes re-downloaded after a host update.
- Keep emoji behaviour identical on native and in the browser: the same emoji are shown or stripped.

**Non-Goals:**
- Splitting the Qt runtime or app code into separately cached modules (wasm-split or Qt's experimental dynamic linking). Too invasive for the first step.
- Deferring fonts. The four Decenza Sans faces are needed for the first frame; Noto Sans Math (990 KB) is a fallback for symbols. Deferring a font means text re-lays out mid-session for a sub-megabyte saving.
- Changing the remote route allowlist. Everything new is served under `/wasm/`.

## Decisions

### 1. Precompressed Brotli replaces gzip, with quality chosen per build

The packager writes `<asset>.br` with the `brotli` CLI (window `-w 24`) in place of the `.gz` sibling, and records `brSize` and `brSha256` in place of the gzip fields. It verifies the copy by decompressing it and comparing the result with the original's hash, as it does for gzip today. The host serves `br` when the browser's quality value for it is above zero. It re-verifies the copy on every read as it does gzip today. An asset with no Brotli copy is served uncompressed. A copy that fails verification is a packaging defect, so it gets the existing integrity `503` rather than a quiet substitute.

Gzip is dropped rather than kept beside Brotli because no browser would ever receive it: every serving origin is HTTPS, and every supported browser offers `br` there. Keeping it would cost about 29.6 MB in every native install for no reader. Replacing it leaves the native app about 11 MB smaller than today. Uncompressed stays as the encoding for assets packaged without a Brotli copy.

Measured on the current 101 MB fast-link wasm (M-series Mac):

| Encoding | Size | Time to compress |
|---|---|---|
| gzip -9 (today) | 29.6 MB | 6.6 s |
| brotli q5 | 22.1 MB | 0.9 s |
| brotli q10 | 19.5 MB | 75 s |
| brotli q11 | 18.8 MB | 139 s |
| zstd -19 --long | 19.9 MB | – |

Release packaging uses q11, a one-time cost per release on the Wasm job. Local packaging uses q5, which is smaller and faster than the gzip step it replaces. That keeps the incremental local build loop no slower than today. The Wasm build's CMake passes the quality to the packager, so the choice lives in one place.

Alternatives considered:
- **zstd:** compresses worse than q11 here (19.9 MB), and iOS Safari support for `zstd` content encoding hasn't been confirmed, so it would need a fallback anyway.
- **Compressing on the fly in the host:** costs tablet CPU on every cold load, and q11 is far too slow to run per request.
- **Keeping gzip beside Brotli:** about 29.6 MB more in every native install, for an encoding no browser is ever served (see above).

Packaging requires the `brotli` CLI; a missing tool is a setup error, like the other Wasm prerequisites. The Python Brotli module was rejected because it isn't in the standard library, and CMake runs the packager with whichever interpreter it finds.

### 2. Emoji and map images served by the host, fetched by URL on first display

The Wasm build stops embedding `emoji.qrc` and the map textures. The host serves them from its own resources:

- `/wasm/emoji/<emojiSetHash>/<key>.svg`, where `key` must match `^[0-9a-f]+(-[0-9a-f]+)*$` and be in the index (below).
- `/wasm/maps/<mapSetHash>/<file>` for the three named textures only.

Both use the existing immutable cache header and the existing asset-path checks. A request whose set hash isn't the host's current one gets a 404.

Each set hash is a content hash over that set's files, generated during the build into a header that both the native and Wasm targets include. Builds from the same tree therefore agree, and an update that doesn't touch a set keeps its URLs and the browser's cached copies. Emoji and maps get separate hashes, so a map change doesn't invalidate thousands of emoji.

Shared QML stops spelling `qrc:/emoji/` and `qrc:/maps/` itself. One C++ object exposes a constant base URL per image set: `qrc:/emoji/` and `qrc:/maps/` on native, and on Wasm `<page origin>/wasm/emoji/<hash>/` and the maps equivalent. `Theme.emojiToImage()` and `ShotMapScreensaver.qml` build their URLs from it. The origin is read once from the page at startup. A QML-relative URL would resolve against `qrc:`, not the host.

Only the emoji a screen actually shows get requested, and each one is cached separately. That is exactly "load more as we need it" at the finest grain. A screen with a dozen emoji makes a dozen small parallel requests, which arrive after the screen is already usable.

Alternatives considered:
- **One separately fetched emoji `.rcc` registered at runtime:** the 3.5 MB would still be downloaded on first use of any emoji, almost certainly on the home screen. Until registration, `qrc:` emoji images fail without retrying, so every emoji binding would need to re-evaluate when the pack arrives.
- **Keeping emoji in the binary and shrinking only the set:** removes emoji the native app shows, which breaks the parity requirement.

### 3. The emoji key index stays embedded; one index for both platforms

A build step writes the bundled keys, one per line (about 50 KB raw, about 10 KB compressed), as a resource generated from `emoji.qrc`. `EmojiAssets` reads that index on both platforms instead of scanning `:/emoji`. So `has()` stays synchronous, exact, and identical between native and browser, and its "fixed at build time" premise still holds, making the invokable still correct. A native test asserts the index equals the `:/emoji` file set, so the two can't drift.

A key in the index whose image fails to load renders blank, never as a text glyph. The text the image replaces has already been rewritten to `<img>`, so nothing reaches the platform renderer (the macOS colour-glyph crash path stays closed).

### 4. Bytecode-only QML is a gated measurement, decided in this change

Build `decenza-web` once with `QT_QMLCACHEGEN_ARGUMENTS --only-bytecode`; the native module keeps AOT. Record the Brotli size of both variants. Then on the iPhone, measure:
- the input budget (p95 visible response ≤ 100 ms over 30 interactions on the home screen and recipe editor);
- the graph budget (p95 frame ≤ 33 ms on a 60 s live or replayed extraction).

Keep bytecode-only only if both budgets hold and the saving is at least 2 MB compressed. Otherwise revert the switch and record the numbers in the evaluation. On Wasm, the QML engine has no JIT, so an uncompiled function runs in the interpreter. About 25% of functions already do (`BUILD_PERFORMANCE.md`, AOT coverage). The risk sits in the bindings that run at 5 Hz during a shot.

### 5. The page keeps the runtime in Cache Storage, because Safari's HTTP cache does not

Measured 2026-10-26 in everyday iOS 27.2 Safari, Web Inspector attached: both a reload and a fresh navigation re-downloaded `decenza-web.wasm` in full (28,925 kB, 25 s over Tailscale; Resource Timing agreed). The `.js` files beside it came from cache. The response headers are correct (`private, max-age=31536000, immutable`), and Emscripten fetches with the default cache mode (`decenza-web.js:349`). WebKit's network cache has no per-record size limit (`NetworkCacheStorage.cpp`), so the cause is not established. Eviction of a ~30 MB entry from the small iOS cache is the leading guess. The HTTP cache cannot be relied on for this file either way.

`index.html` supplies Qt's supported module hook (`config.qt.module`, `qtloader.js:114-124`) with a promise that:
1. opens the `decenza-runtime` cache and looks up the runtime's absolute URL. The entry document's `<base>` already makes `decenza-web.wasm` resolve to the hash-versioned URL;
2. on a hit, compiles the stored response with `WebAssembly.compileStreaming`;
3. on a miss, or a hit that fails to compile (deleted first), fetches normally and compiles the response while storing a clone of it, so the body is not buffered twice;
4. before storing, deletes every other key in that cache, so only the current version is kept and an old copy never holds the quota the new one needs.

Every storage call is wrapped so that a rejection (no Cache API, quota, eviction mid-read) falls through to the plain network path, and a failed store costs only a download on the next visit. A copy stored from a download that then fails to compile is deleted at step 2 on the next visit. Cache Storage needs a secure context, and every serving origin is HTTPS.

Alternatives considered:
- **A service worker:** does the same job with a lifecycle, update and scope model that has to be managed, for one file the page can handle itself.
- **Storing the compiled `WebAssembly.Module` in IndexedDB:** WebKit doesn't support structured-cloning a Module into IndexedDB, and the cost being removed is the download, not the 3 s compile.
- **Fixing the HTTP cache from the server side:** no header change is known to make Safari keep the file. The headers are already the strongest available.

## Risks / Trade-offs

- **[Safari clears script-written storage for sites unused for 7 days]** → The next visit downloads once and stores again; it is never slower than today. Home Screen web apps are exempt.
- **[A stored copy is corrupt]** → Compilation fails, the entry is deleted, and the client downloads normally. The package-identity check on the live session still guards against a runtime that doesn't match the host.

- **[q11 lengthens the release Wasm job]** → It runs once per release on ubuntu-24.04, probably a few minutes on the runner. If it lands on the critical path enough to matter, q10 costs 0.7 MB.
- **[Many small emoji requests over a high-latency tunnel]** → Emoji are never on the critical path: the screen is usable without them and each request is cached forever after. A hot screen's emoji count is in the tens, not the hundreds.
- **[Native and Wasm set hashes disagree]** → They come from the same generated header in one tree, and the package identity already requires the client and host to come from the same build. A disagreement shows as blank emoji, not a crash, and the 404 is in the host log.
- **[Brotli CLI missing on a contributor's machine]** → A setup error names the tool and how to install it, like the existing Emscripten checks.

## Migration Plan

Nothing persists. The host and native app always ship from one build, so there is no older bundle to keep serving. The host still serves a manifest without `br` entries (uncompressed), and release packaging fails without them. Rolling back means reverting the change.
