# Tasks

## 1. Baseline

- [x] 1.1 On the iPhone, open the web interface in a normal Safari tab over Tailscale. With Mac Safari → Develop → the phone → Network, reload and record whether `decenza-web.wasm` comes from cache or is transferred again. Record the result in `replace-web-interface-with-wasm/evaluation.md` beside the 2026-10-26 Tailscale measurements from proposal.md. Verified by the recorded entry.

## 2. Brotli delivery

- [x] 2.1 In `scripts/package_webclient.py`, replace the gzip sibling with Brotli: write `<asset>.br` with the `brotli` CLI (`-w 24`, quality from a `--brotli-quality` argument), record `brSize`/`brSha256` in place of the gzip fields, verify by decompressing and comparing to the original hash, and fail with a named setup error when the CLI is missing. Manifest loading accepts the optional `br` pair under the rules gzip had, and an install with `--require-brotli` fails when the Wasm runtime has none. Verified by `tools/webclient/test_package.py` cases: round-trip, incomplete metadata, a corrupt `.br`, the missing CLI, a bad quality, and the required-but-absent case.
- [x] 2.2 Pass the quality from `src/webclient/CMakeLists.txt` (`DECENZA_WEB_BROTLI_QUALITY`, default 5; the release Wasm job sets 11). Update the embedded file list in `scripts/build_webclient.py` and `cmake/webclient_bundle.cmake` to the `.br` names, and have release installs (`DECENZA_REQUIRE_WEB_BUNDLE=ON`) pass `--require-brotli`. CMake locates the tool (`DECENZA_BROTLI_EXECUTABLE`), since an IDE-launched build does not inherit the shell's PATH. Verified by Qt Creator build `1792869330654` producing and embedding `decenza-web.wasm.br`, and by `tools/webclient/README.md` stating the new prerequisite and the two quality levels.
- [x] 2.3 Install `brotli` and set quality 11 in the `wasm-bundle` job of `.github/workflows/release.yml`, and fail the job unless the installed `libbrotli1` carries the CVE-2026-111497 fix (noble `1.1.0-2ubuntu0.1` or later; its CLI still reports 1.1.0, so the package version is checked). Verified by reading the step and by the packager failing loudly without the tool (task 2.1 test).
- [x] 2.4 In `src/network/webclientassets.cpp`, negotiate `br` in place of `gzip`. Serve the verified Brotli body with `Content-Encoding: br`, serve the verified uncompressed bytes when the asset has no Brotli copy, answer a copy that fails verification with the integrity `503`, and keep `Vary: Accept-Encoding`. Verified by `tst_webclientassets` (full run `1792869330741`, 207/207) cases for each spec scenario under "Compressed static asset delivery": br served, br refused at q=0 (uncompressed), a corrupt `.br` refused with 503, and a manifest without `br`.
- [x] 2.5 Confirm against the running local build that the wasm is served `Content-Encoding: br` with the smaller transfer. Verified 2026-10-26 against build `1792869330654`: `Content-Length: 20741363`, `Content-Encoding: br`, immutable cache header (29,580,165 bytes as gzip before); an emoji at a wrong set hash returns 404.

## 3. Emoji key index

- [x] 3.1 Generate the emoji key index (one key per line) from `resources/emoji.qrc` as a build-time resource for both the native and Wasm targets, and switch `EmojiAssets` to read it instead of scanning `:/emoji`. Update the invokable's comment to say the set is the embedded index. Verified by `tst_emojiassets` asserting the index equals the `:/emoji` file set on native, and that `count()` is unchanged.

## 4. Host-served emoji

- [x] 4.1 Generate the emoji set's content hash into a header (`cmake/emoji_index.cmake`, `scripts/generate_emoji_index.py`) included by the host and the Wasm client, so builds from one tree agree. Verified by the configure step writing `decenza_emoji_set.h` and both targets compiling against it.
- [x] 4.2 Serve `/wasm/emoji/<hash>/<key>.svg` from the host's own `:/emoji`: validate the key pattern, require the current set hash, bound the size, return 404 otherwise, send the immutable cache header, and answer inline outside the four-slot runtime download pool. Verified by `tst_webclientassets::servesBundledEmojiOnlyAtTheCurrentSetUrl`: a valid key, a keycap key, an unknown key, another set's hash, wrong extensions, traversal attempts and a malformed key.
- [x] 4.3 Confirm the emoji path sits under the existing `/wasm/` remote prefix and needs no allowlist change. Verified by the `emoji image` row in `tst_remoteaccess::remoteRouteServesOnlyTheCurrentInterface`.

## 5. Client resolves emoji by URL

- [x] 5.1 Expose a constant per-platform emoji base on `EmojiAssets` (`imageBase`): `qrc:/emoji/` natively; in the Wasm client the page origin plus `/wasm/emoji/<hash>/`, with the page-origin read shared with the session backend (`src/core/browserorigin.{h,cpp}`). Verified by `tst_emojiassets::imageBaseIsTheLocalResourceNatively`.
- [x] 5.2 Make `Theme._emojiAssetPath()` build URLs from `EmojiAssets.imageBase`, and route the four QML files that hard-coded `qrc:/emoji/` through `Theme.emojiToImage()`. Verified by a grep returning no `qrc:/emoji` in `qml/`, and `tst_textescaping` passing with the base in its stub.
- [x] 5.3 Remove `emoji.qrc` from `src/webclient/CMakeLists.txt` and from the resource-closure check. Verified by Qt Creator build `1792869330654` (closure check passed): the wasm fell from 101.4 MB to 91.6 MB raw, and its local Brotli q5 copy is 20.7 MB against 29.6 MB gzip before.
- [ ] 5.4 In the browser, check the image spec scenarios: a screen with emoji renders usable before its images arrive and only its emoji are requested (Network tab); a reload serves them from cache; an emoji newer than the set is stripped with no request; a blocked emoji request leaves a blank, not a glyph. Verified by the recorded observations.
- [ ] 5.5 Decide the map textures (1.15 MB): the globe screensaver loads them through a Quick3D `Texture`, which reads only local or `qrc:` paths (`qquick3dtexture.cpp:1065`), so host-served maps need the globe to texture from an `Image` item. Either do that, check the globe natively and in the browser, and serve `/wasm/maps/<hash>/<file>`; or keep them embedded and record why. Verified by the recorded decision and, if moved, the globe rendering on both platforms.

## 6. Bytecode-only QML measurement

- [ ] 6.1 Build `decenza-web` with `QT_QMLCACHEGEN_ARGUMENTS --only-bytecode` and record its Brotli q11 size against the AOT build. Verified by both sizes recorded in the evaluation.
- [ ] 6.2 On the iPhone, measure both builds against the input budget (p95 ≤ 100 ms over 30 interactions on the home screen and recipe editor) and the graph budget (p95 frame ≤ 33 ms on a 60 s live or replayed extraction). Keep `--only-bytecode` only if both budgets hold and the saving is at least 2 MB compressed; otherwise remove it. Verified by the recorded numbers and decision in the evaluation and the build file reflecting that decision.
- [ ] 6.3 Link the release Wasm with `-Os` and with `-Oz` instead of the current speed-optimized link, record each Brotli q11 size, and apply the 6.2 budgets on the iPhone. Keep the smallest variant that holds both budgets (combined with the 6.2 decision). Verified by the recorded sizes, numbers and decision, and the build file reflecting it.

## 7. Client-retained runtime

- [x] 7.1 In `resources/webclient/index.html`, supply `qt.module` from the `decenza-runtime` Cache Storage as design decision 5 describes: hit → compile; miss or failed compile → network, compile, store; then prune other keys; every storage error falls through to the network. Verified 2026-10-26 on iOS 27.2 Safari (automation profile) against the Mac build over Tailscale: the first load stored one entry (91,551,506 bytes, `application/wasm`), a repeat navigation made no wasm request and finished loading in 1.9 s, and a deliberately corrupted entry was refetched and stored again; everyday Mac Safari also made no wasm request on a repeat load.
- [ ] 7.2 Confirm in everyday iOS Safari over Tailscale, with Web Inspector attached, that a repeat visit makes no `decenza-web.wasm` request. Verified by the recorded Network tab result.

## 8. Integration

- [ ] 8.1 Repeat the iPhone measurements over Tailscale against the tablet in everyday Safari: first visit after an update and repeat visit, wasm transfer size and time, and time to a usable home screen. Record them beside the baseline. Verified by the evaluation entry showing a first-visit transfer smaller than 28,925 kB and a repeat visit with no wasm transfer.
- [ ] 8.2 Run the full suite through `mcp__qtcreator__run_tests` (scope `all`) and confirm `text-invariants` passes on the PR. Verified by a green run.

## Workflow follow-up

- `replace-web-interface-with-wasm` must archive before this change does, because this change adds requirements to the `wasm-browser-client` capability it introduces.
- Archive this change with `openspec archive shrink-web-client-first-load --yes` as the PR's last commit.
