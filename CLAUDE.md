
# Decenza

Qt/C++ cross-platform controller for the Decent Espresso DE1 machine with BLE connectivity.

## User Manual

The end-user manual lives in the GitHub wiki at https://github.com/Kulitorum/Decenza/wiki/Manual. Consult it when working on user-visible behaviour to confirm documented expectations or the official wording for features. The wiki is a separate git repo (`Kulitorum/Decenza.wiki.git`) — clone it locally if you need to edit a manual page.

**When adding or changing a user-visible feature, update the wiki manual as part of that work** — add a task for it in the change's `tasks.md` (or do it directly for small fixes). Don't leave it as an afterthought; a shipped feature with no manual entry is incomplete.

## Reference Documents

Detailed documentation lives in `docs/CLAUDE_MD/`. Read these when working in the relevant domain:

| Document | When to read |
|----------|-------------|
| `PROJECT_STRUCTURE.md` | Map of `src/`, `qml/`, `resources/`, signal/slot flow, profile pipeline |
| `CI_CD.md` | Release process, GitHub Actions workflows, version bumping |
| `PLATFORM_BUILD.md` | CLI build commands (Windows/macOS/iOS), Windows installer, Android signing, tablet quirks |
| `RECIPE_PROFILES.md` | Recipe Editor, D-Flow/A-Flow/Pressure/Flow types, frame generation, JSON format, stop limits, profile_sync tool |
| `RECIPES.md` | Drink recipes (add-recipes): data model, recipe-owned grind, steam block, single activation path, promote-from-shot, MCP/web surfaces. NOT the profile Recipe Editor — that is `RECIPE_PROFILES.md` |
| `TESTING.md` | Test framework, mock strategy, adding new tests, **`shot_eval` harness + regression corpus** |
| `BLE_PROTOCOL.md` | BLE UUIDs, retry mechanism, shot debug logging, battery/steam control |
| `VISUALIZER.md` | DYE metadata, profile import/export, ProfileSaveHelper, filename generation |
| `DATA_MIGRATION.md` | Device-to-device transfer architecture and REST endpoints |
| `STEAM_CALIBRATION.md` | Postmortem on the removed steam calibration feature |
| `CUP_FILL_VIEW.md` | CupFillView layer stack, GPU shaders, updating cup images |
| `EMOJI_SYSTEM.md` | Twemoji SVG rendering, adding/switching emoji sets |
| `ACCESSIBILITY.md` | TalkBack/VoiceOver rules, focus order, anti-patterns, implementation plan |
| `AUTO_FLOW_CALIBRATION.md` | Auto flow calibration algorithm, batched median updates, windowing, convergence |
| `SAW_LEARNING.md` | Per-(profile, scale) stop-at-weight learning |
| `FIRMWARE_UPDATE.md` | DE1 firmware update flow, source URL, validation rules, failure modes |
| `MCP_SERVER.md` | Full MCP tool list, access levels, architecture, data conventions |
| `AI_ADVISOR.md` | AI dialing assistant design |
| `BEAN_BASE.md` | Loffee Labs Bean Base integration: API quirks (whole-word search, tier-gated fields, 429 classification), snapshot-not-reference rule, lock-follows-the-data UI, Visualizer canonical-id architecture |
| `SETTINGS.md` | Settings architecture: 7 domain sub-objects, how to add properties/domains, QML access pattern, build-blast rules |
| `QML_GOTCHAS.md` | QML bug-prone patterns with code samples (font conflict, reserved names, IME drop, etc.) |
| `QML_NAVIGATION.md` | StackView page navigation, phase-change handler, operation-page conventions |
| `SHOTSERVER.md` | ShotServer file split, async community endpoints, JS `fetch()` rules |
| `WIDGET_SNAPSHOT.md` | iOS/Android Home Screen widget: snapshot JSON schema, transport, phase-label table, display/staleness rules |

Read [`docs/SHOT_REVIEW.md`](https://github.com/Kulitorum/Decenza/blob/main/docs/SHOT_REVIEW.md) when working on the post-shot review / shot detail pages, the five quality-badge detectors (pour truncated, channeling, grind issue, temperature unstable, skip-first-frame), the Shot Summary dialog, badge persistence, or `src/ai/shotanalysis.{h,cpp}`. It is the source of truth for detector internals, gate semantics, and the recompute-on-load contract; keep it in sync when changing any of the above.

## Development Environment

- **ADB path**: `/c/Users/Micro/AppData/Local/Android/Sdk/platform-tools/adb.exe`
- **Uninstall app**: `adb uninstall io.github.kulitorum.decenza_de1`
- **WiFi debugging**: `192.168.1.212:5555` (reconnect: `adb connect 192.168.1.212:5555`). The DHCP lease can rotate — if reconnect fails, plug in USB and run `adb shell ip route | grep wlan` to read the current IP, then `adb tcpip 5555` + `adb connect <ip>:5555`.
- **Qt version**: 6.11.1
- **Qt path**: `C:/Qt/6.11.1/msvc2022_64`
- **Qt sources**: `~/Qt/6.11.1/Src` (macOS) — the full Qt source tree for the exact version we build against (`qtbase`, `qtwebsockets`, …). **Read it instead of guessing at Qt behaviour.** Qt's own docs routinely omit the details that decide a bug, and its error handling in particular is not inferable from the enum names — several distinct `errno` values collapse onto one `QAbstractSocket::SocketError`, and the mapping differs per platform. Worked example: `qtbase/src/network/socket/qnativesocketengine_unix.cpp` shows `EINVAL` mapping to `ConnectionRefusedError`, and `qtbase/src/network/socket/qabstractsocket.cpp` shows that `connectToHost()` with an IP literal resolves inline, so `open()`/`connectToHost()` can emit `errorOccurred` **synchronously** rather than on a later event-loop turn. Both facts change what correct code looks like, and neither is in the class documentation.
- **qtbase checkout**: `~/Development/GitHub/qtbase` — separate git clone with the Gerrit remote, for upstream patches. Not the copy to read for reference (it sits on whatever contribution branch is in flight); use `~/Qt/6.11.1/Src` for that.
- **C++ standard**: C++17
- **de1app source**: `C:\code\de1app` (Windows) or `/Users/jeffreyh/Development/GitHub/de1app` (macOS) — original Tcl/Tk DE1 app for reference
- **IMPORTANT**: Use relative paths (e.g., `src/main.cpp`) instead of absolute paths (e.g., `C:\CODE\de1-qt\src\main.cpp`) to avoid "Error: UNKNOWN: unknown error, open" when editing files

## Building

**An assistant builds and runs tests through the Qt Creator MCP tools — `mcp__qtcreator__build` and `mcp__qtcreator__run_tests` — and through nothing else.** Not `cmake --build`, not `ctest`, not a `./tests/tst_*` binary, from any shell. That holds for the full pre-PR suite, for a single target, and when an MCP call times out (the call's wait can abort while Qt Creator keeps building — poll `get_build_status`, don't shell out). If the MCP path is blocked — wrong startup project, app holding the binary, tool unavailable — **stop and ask**. Qt Creator is also ~50× faster than a CLI build, and it is the environment the maintainer is watching while you work.

The `cmake`/`ctest` invocations in `docs/CLAUDE_MD/TESTING.md` and `docs/CLAUDE_MD/PLATFORM_BUILD.md` are **reference for humans and CI**, not instructions for an assistant. Run their equivalent through the MCP.

**There is no pull-request CI gate — run the full suite locally before opening a PR.** That is the gate — via `mcp__qtcreator__run_tests` (scope `all`). `nightly-sanitizers.yml` re-runs the suite on `main` each night under UBSan and ASan. Platform-guarded code (`#ifdef Q_OS_IOS` etc.) is compiled only by the tag-push release workflows, so verify platform-specific changes with a CI test build of that platform (see `docs/CLAUDE_MD/CI_CD.md`).

**Debug builds are sanitizer-instrumented automatically** — ASan *and* UBSan on every desktop platform including macOS, so a normal local test run already reports undefined behaviour and memory errors. UBSan is in recovering mode there (it reports and continues); an explicit `-DENABLE_UBSAN=ON` gives the halting mode CI uses. Release builds are untouched.

**But LeakSanitizer does not exist on macOS, so a green local run says nothing about leaks.** `ASAN_OPTIONS=detect_leaks=1` is refused outright — `AddressSanitizer: detect_leaks is not supported on this platform`. LSan is Linux-only, so on a Mac ASan covers use-after-free, buffer overflow and double-free, and cannot see a leak at all. The nightly Linux ASan job is the only place leaks are detected; it found two the local suite had passed over for months. To chase a leak on macOS use `leaks <pid>` or `MallocStackLogging=1`, not ASan.

## Project Structure

See `docs/CLAUDE_MD/PROJECT_STRUCTURE.md` for the full source tree, signal/slot flow, scale system, machine phases, AI/MCP overview, and profile pipeline. Top-level: `src/` (C++), `qml/` (UI), `resources/`, `shaders/`, `tests/`, `docs/`, `openspec/`, `android/`, `installer/`.

## Conventions

### Design Principles
- **Never use timers as guards/workarounds.** Timers are fragile heuristics that break on slow devices and hide the real problem. Use event-based flags and conditions instead. For example, "suppress X until Y has happened" should be a boolean cleared by the Y event, not a timer. Only use timers for genuinely periodic tasks (polling, animation, heartbeats) and **UI auto-dismiss** (toasts/banners that hide after N seconds). Everything else — including debounce — should use event-based flags.
- **Never run database or disk I/O on the main thread.** Use `QThread::create()` with a `QMetaObject::invokeMethod(..., Qt::QueuedConnection)` callback to run queries on a background thread and deliver results back to the main thread. See `ShotHistoryStorage::requestShot()` for the canonical pattern. For database connections inside background threads, always use the `withTempDb()` helper from `src/core/dbutils.h` — it handles unique connection naming, `busy_timeout`, `foreign_keys` pragmas, and cleanup. Never manually call `QSqlDatabase::addDatabase()`/`removeDatabase()` when `withTempDb` can be used instead.
- **A configured API key is not permission to spend on it.** Any feature that calls a paid AI provider uses the provider AND model the user selected, and nothing else. Never fall back to another provider because one errored or rate-limited, and never pick a provider because it happens to have a key — a user with OpenAI selected must not get billed on Anthropic. When the selected provider fails, stop and report which provider failed at what; a run that reports success having silently used something else is worse than a run that ends. Decenza's bulk translator did exactly this for months, hard-ordering "Claude first (best quality), then OpenAI", and the silent substitution hid a retired Anthropic model that made every Anthropic request 404 — visible only to users with no second key.
- **Settings go in their domain sub-object, not on `Settings` directly.** `Settings` is a façade that owns 7 domain classes (`SettingsMqtt`, `SettingsTheme`, etc.). Add new properties to the matching `Settings<Domain>` class, or create a new sub-object if none fits. Never add a property back to `Settings` itself, and never `#include "settings_<domain>.h"` in `settings.h` — both undo the recompile-blast win the split was built for. New sub-objects also require `qmlRegisterUncreatableType<Settings<Domain>>(...)` in `main.cpp` or QML resolves `Settings.<domain>.<prop>` to `undefined` at runtime. QML access is **always** `Settings.<domain>.<prop>`, never the flat `Settings.<prop>`. See `docs/CLAUDE_MD/SETTINGS.md` for the full checklist.

### C++
- Classes: `PascalCase`; methods/variables: `camelCase`; members: `m_` prefix; slots: `onEventName()`
- Use `Q_PROPERTY` with `NOTIFY` for bindable properties
- Use `qsizetype` (not `int`) for container sizes — `QVector::size()`, `QList::size()`, `QString::size()` etc. return `qsizetype` (64-bit on iOS/macOS). Assigning to `int` causes `-Wshorten-64-to-32` warnings.
- A discarded `[[nodiscard]]`/`warn_unused_result` value is a build **error** (`-Werror=unused-result`). To deliberately ignore one, write `(void)call();` with a comment saying why losing that failure is tolerable — never a bare call, never a file-wide suppression. Note the compiler only enforces this for *annotated* APIs: it caught a dropped `SecRandomCopyBytes` (Apple annotates it) and said nothing about the identical dropped `RAND_bytes` (OpenSSL doesn't) — so check unannotated results yourself.

### QML
- Files: `PascalCase.qml` — new QML files **must** be added to `CMakeLists.txt` (in the `qt_add_qml_module` file list) to be included in the Qt resource system. Without this, the file won't be found at runtime.
- **New layout widgets** require registration in 3 places: (1) `CMakeLists.txt` file list, (2) `LayoutItemDelegate.qml` switch, (3) the widget catalog table (`widgetCatalogTable()` in `settings_network.cpp`) — the in-app palette, chip labels, library card, and web editor all derive from that one table. Optionally add to `LayoutCenterZone.qml` if the widget should be allowed in center/idle zones. If the widget has per-instance options, also declare its option keys (and non-text display-mode default, if any) in the readout capability schema (`readoutOptionSchema()` / `displayModeDefaults()` in the same file) — the gear indicator, the unified `ReadoutOptionsPopup`, and the web editor's option forms all derive from it. A non-text display default additionally requires the widget's item component to call `defaultDisplayModeForType()` (item components otherwise hard-code the "text" default).
- IDs/properties: `camelCase`
- Use `Theme.qml` singleton for all styling — never hardcode colors, font sizes, spacing, or radii. Use `Theme.textColor`, `Theme.bodyFont`, `Theme.subtitleFont`, `Theme.spacingMedium`, `Theme.cardRadius`, etc.
- All user-visible text must be internationalized. Use `TranslationManager.translate("section.key", "Fallback text")` for property bindings and inline expressions. Use the `Tr` component for standalone visible text (`Tr { key: "section.name"; fallback: "English text" }`). For text used in properties via `Tr`, use a hidden instance: `Tr { id: trMyLabel; key: "my.key"; fallback: "Label"; visible: false }` then `text: trMyLabel.text`. Reuse existing keys like `common.button.ok` and `common.accessibility.dismissDialog` where applicable.
  - **A binding over `translate()` re-evaluates on a language change, and the plain call above is all you need.** This is worth stating because it was NOT always true: `translate` used to be a `Q_INVOKABLE`, a binding calling it recorded no dependency, and 3,248 call sites written exactly as documented here froze on whatever language was active when the page was built. It is now a `Q_PROPERTY` holding a callable, so reading `TranslationManager.translate` establishes the dependency — see `translationmanager.h`.
  - Do **not** add a `var _ = TranslationManager.translationVersion` line to new code. It is redundant now; `Tr.qml` keeps one only as a historical marker. If you find yourself reaching for it because text is stale, the mechanism is broken — `tests/tst_translationreactivity.cpp` should be failing, and that is the thing to fix.
- Use `StyledTextField` instead of `TextField` to avoid Material floating label
- `ActionButton` dims icon (50% opacity) and text (secondary color) when disabled

### Using emoji well

The app ships the complete Twemoji set (~4,000 SVGs, MIT), resolved locally with no network
access. Emoji are cheap, render identically on every platform, and are the one visual element
that survives translation unchanged. **Reach for them where they make a screen easier to read
or more pleasant to use** — an interface that is all grey text is not more professional, it is
just harder to scan.

**Where they earn their place:**
- Category and section markers, where a glyph makes a list scannable at a glance.
- Status and outcome, alongside the words rather than instead of them — `☕ Espresso`,
  `⚠️ Tank low`, `✅ Uploaded`.
- User-authored content: bean names, recipe names, widget labels, notes. Users already type
  emoji here and the picker offers the full set.
- Empty states and first-run screens, where a little warmth reads as care rather than noise.

**Where they do not:**
- **Never as the only carrier of meaning.** A screen reader announces the name, not the picture,
  and a stripped context (plain-text fields, exports, MCP responses) drops it entirely. Always
  pair with a word.
- **Not on destructive or error actions** in place of clear wording. `🗑️ Delete all shots` is
  fine; `🗑️` alone is not.
- **Not more than one per label,** and not decorating every row of a list — repetition turns a
  useful signal into visual noise, and a page where everything is marked marks nothing.
- **Not in place of a themed icon** for chrome — toolbar and navigation icons are monochrome
  SVGs that follow `Theme.iconColor` through `ThemedIcon`. Emoji carry fixed colours and will
  not adapt to light/dark or a custom palette.

**Mechanics — these are not optional:**
- Render through `Theme.emojiToImage()` (for an `Image`) or `Theme.replaceEmojiWithImg()` (for
  text with emoji inline). Putting an emoji in a plain `Text` lets a colour glyph reach the
  platform renderer, which **crashes the render thread on macOS**.
- No manual asset step. Using a new emoji needs no download and no `.qrc` edit — the full set
  already ships. `.github/workflows/emoji-pin-check.yml` reports when upstream has a newer
  release worth pulling in.
- An emoji with no bundled asset is silently stripped, so a sequence from a Unicode revision
  newer than the pin simply disappears. Don't build a layout that only makes sense if the emoji
  renders.
- For accessibility, give the element an `Accessible.name` with the word, not the picture.
  `Theme.toAccessibleText()` strips emoji and tags from a rendered string for exactly this.

### QML Gotchas (one-liners — full samples in `docs/CLAUDE_MD/QML_GOTCHAS.md`)

- **Font property conflict**: don't mix `font: Theme.bodyFont` with `font.bold: true` — assign sub-properties individually.
- **Reserved names in JS model data**: `name`, `parent`, `children`, `data`, `state`, `enabled`, `visible`, `width`, `height`, `x`, `y`, `z`, `focus`, `clip` collide with QML properties — use `label` etc.
- **IME last-word drop**: call `Qt.inputMethod.commit()` before reading any `TextField.text` from a button handler — otherwise the in-progress word is lost on mobile.
- **Keyboard handling**: wrap pages with text inputs in `KeyboardAwareContainer { textFields: [...] }`.
- **FINAL Qt properties**: don't redeclare `message`/`title` etc. on `Popup`/`Dialog` (Qt 6.10+) — pick a different name like `resultMessage`.
- **Numeric defaults**: use `value ?? 0.6`, not `value || 0.6` — `||` treats `0` as falsy.
- **`native` is reserved**: use `nativeName`.
- **Emoji are encouraged; non-emoji text symbols are not.** These are two different things and only one is safe:
  - **Emoji** (`☕` `⚙️` `⚠️` `🔒`) never reach the text renderer — the app ships the complete Twemoji set (~4,000 SVGs) and every emoji is rewritten to a bundled `<img>`. Metrics are identical on every platform because it is an image, not a glyph. **Use them where they earn their place** (see "Using emoji well").
  - **Non-emoji text symbols** (`→` `←` `↗` `↕` `▶` `◀` `⧉`) are ordinary font glyphs and are now **fine to use**. Decenza Sans has only 927 glyphs and none of these, so the app also bundles **Noto Sans Math** (SIL OFL) as a symbol fallback, chained after the UI family in `Theme.fontFamilies` and on the application font. Qt consults it only for codepoints the primary lacks, so symbols come from the bundle, render identically on every platform, and stay monochrome — they take the element's colour like the text around them, which is exactly what emoji cannot do.
    - Before using a symbol not already in the app, check it: `python3 scripts/check_font_glyph_coverage.py` reads both cmaps and reports anything that would still fall through to the host. If something you want is uncovered, add a second OFL face rather than reaching for an emoji.
    - **This was previously written as an absolute ban justified by #1537. That citation does not support the ban** — #1537 dropped the "fi" ligature from "Profile", a word entirely inside the bundled font, so whatever its cause, it was not a missing glyph and cannot justify a rule about fallbacks. Nothing here has ever been traced to a missing glyph. Note that #1537's actual cause is still open: `src/main.cpp` carries two candidate explanations and states plainly that they are not reconciled — do not repeat either as settled. Kept on the record so the ban is not reinstated from memory.
    - **Still avoid a bare U+FE0F on a symbol** (`▶️` rather than `▶`). That is an explicit request for colour-emoji presentation, and in a plain `Text` — which is what `AccessibleButton.text` and most labels are — it is the macOS render-thread crash path. Adding the variation selector makes a working symbol worse, not better.
    - A symbol is still not a substitute for an **icon** in chrome. `qrc:/icons/` SVGs follow `Theme.iconColor` and scale as artwork; a glyph is text that happens to look like a picture.
  - Rule of thumb: colour picture in the emoji keyboard → emoji, fine. Line-drawing symbol in your text colour → font glyph, also fine now, but confirm coverage with the script. Toolbar/navigation affordance → neither; use a themed SVG.
- **The bundled font covers Latin (incl. Extended), Greek and Cyrillic only.** In CJK, Arabic, Hebrew, Devanagari and Thai locales every glyph comes from a platform fallback, so the metric determinism the bundled font provides does **not** apply there. Layout tolerance (wrap/elide/content-driven sizing) is what keeps those UIs from clipping — never rely on a fixed width that only fits the design font.
- **`elide` is dead on `Text.RichText`**: use `Text.StyledText` for HTML-ish labels (elide works, and it's lighter); RichText silently disables `elide` → mid-glyph clipping.
- **Measuring text in a binding**: use `FontMetrics.advanceWidth(str)`, never a mutated `TextMetrics` (`.text=`/read `.width`) — the latter self-triggers a binding loop. Mutated `TextMetrics` is only safe in an imperative Timer/handler writing a plain property. Runtime-only; a clean build won't catch it.
- **Accessibility on interactive elements**: every interactive element needs `Accessible.role`, `Accessible.name`, `Accessible.focusable: true`, and `Accessible.onPressAction`. Prefer `AccessibleButton` / `AccessibleMouseArea` over raw `Rectangle+MouseArea`. Full rules in `docs/CLAUDE_MD/ACCESSIBILITY.md`.

### MCP Tool Responses (`src/mcp/`)

MCP tool responses are consumed by LLMs which cannot reliably interpret raw numbers. Follow these conventions:

- **Never return Unix timestamps.** Use ISO 8601 with timezone: `dt.toOffsetFromUtc(dt.offsetFromUtc()).toString(Qt::ISODate)` → `"2026-03-21T11:20:41-06:00"`
- **Include units in field names.** `doseG` (grams), `pressureBar`, `temperatureC`, `flowMlPerSec`, `durationSec`, `weightG`, `targetVolumeMl`. An AI seeing `"pressure": 9.0` doesn't know bar vs PSI vs kPa.
- **Include scale in field names for bounded values.** `enjoyment0to100` instead of `enjoyment`.
- **Use human-readable strings for enums.** Machine phases, editor types, and states as strings (`"idle"`, `"pouring"`), not numeric codes.

See `docs/CLAUDE_MD/MCP_SERVER.md` for the full data conventions section.

## Subsystem Pointers

- **Profiles, JSON format, stop limits, profile_sync**: `docs/CLAUDE_MD/RECIPE_PROFILES.md`
- **QML page navigation, operation pages, phase-change handler**: `docs/CLAUDE_MD/QML_NAVIGATION.md`
- **ShotServer (split files, async community endpoints, fetch rules)**: `docs/CLAUDE_MD/SHOTSERVER.md`
  - **ShotServer pages must match the app in look AND features, not look half-finished.** When a ShotServer web page mirrors an in-app screen (e.g. `/beans`, `/recipes`, `/equipment`, shot history), design it to closely match the app's clean version — same information hierarchy, card grammar, active-item highlight, empty states, and canonical page chrome (the `<header class="header">` logo + back + burger menu, the shared embedded-page style) — AND aim for feature parity: every field and action the app offers on that screen should be reachable from the web, rather than shipping a bare demo-style subset.
  - **Reuse, don't copy.** Build shared page style/shell/JS helpers instead of re-inlining per page, and reach parity features by reusing existing backends (e.g. `BeanBaseClient`, the storage classes, the patterns proven by the MCP tools and async community endpoints) rather than re-implementing them.
  - **Keep the two surfaces in sync.** When you change an in-app page that also exists in the ShotServer (or vice-versa), update the counterpart in the same change so they don't drift. Add a task for it in the change's `tasks.md`.
- **Emoji**: always `Image { source: Theme.emojiToImage(value) }` — never `Text` for emojis. See `EMOJI_SYSTEM.md` and "Using emoji well".
- **Cup Fill View**: `docs/CLAUDE_MD/CUP_FILL_VIEW.md`
- **Data migration (device-to-device)**: `docs/CLAUDE_MD/DATA_MIGRATION.md`
- **Visualizer integration**: `docs/CLAUDE_MD/VISUALIZER.md`
- **Unit testing** (Qt Test, `friend class` access behind `#ifdef DECENZA_TESTING`, build with `-DBUILD_TESTS=ON`, `shot_eval` harness, `tests/data/shots/` regression corpus): `docs/CLAUDE_MD/TESTING.md`
- **BLE protocol**: `docs/CLAUDE_MD/BLE_PROTOCOL.md`
- **CI/CD, releases, auto-update**: `docs/CLAUDE_MD/CI_CD.md`
- **Windows installer / Android build / tablet quirks**: `docs/CLAUDE_MD/PLATFORM_BUILD.md`

## Platforms

- Desktop: Windows, macOS, Linux
- Mobile: Android (API 28+), iOS (17.0+)
- Android needs Location permission for BLE scanning

## Versioning

- **Display version** (versionName): Set in `CMakeLists.txt` line 2: `project(Decenza VERSION x.y.z)`
- **Version code** (versionCode): Stored in `versioncode.txt`. Does **not** auto-increment during local builds. CI workflows bump it on tag push, and the Android workflow commits the new value back to `main`.
- **version.h**: Auto-generated from `src/version.h.in` with VERSION_STRING macro
- **AndroidManifest.xml**: Auto-generated from `android/AndroidManifest.xml.in` by CMake at build time (gitignored). Both `versionCode` and `versionName` come from `versioncode.txt` and `CMakeLists.txt` respectively.
- **installer/version.iss**: Auto-generated from `installer/version.iss.in` by CMake at build time (gitignored).
- To release a new version: Update VERSION in CMakeLists.txt, commit, then follow the "Publishing Releases" process in `docs/CLAUDE_MD/CI_CD.md` (create release first, then push tag)

## Git Workflow

- **Standard merge: squash + delete branch.** Every PR lands on `main` as a single squashed commit, and the feature branch is deleted on the remote (and locally if you're on it). The `merge-pr` skill (`.claude/skills/merge-pr/SKILL.md`) automates this — invoke it via `/merge-pr` or whenever the user says "merge". Equivalent CLI: `gh pr merge <num> --repo Kulitorum/Decenza --squash --delete-branch`. Do not use `--merge` (true merge commit) or `--rebase` unless the user explicitly asks for them.
- **Version codes are managed by CI** — local builds use `versioncode.txt` as-is (no auto-increment). All 6 CI workflows bump the code identically on tag push. The Android workflow commits the bumped value back to `main`.
- You do **not** need to manually commit version code files — only `versioncode.txt` is tracked. `android/AndroidManifest.xml` and `installer/version.iss` are generated from `.in` templates by CMake at build time and are gitignored.

## Accessibility (TalkBack/VoiceOver)

See `docs/CLAUDE_MD/ACCESSIBILITY.md` for the full reference: component rules, focus-order requirements, anti-patterns, common mistakes checklist, and the page-by-page implementation plan for [Kulitorum/Decenza#736](https://github.com/Kulitorum/Decenza/issues/736).

**Key rule for modifying existing components**: Fix pre-existing violations in any file you touch — do not dismiss them as "pre-existing". Issues compound over time and each change is an opportunity to fix them.
