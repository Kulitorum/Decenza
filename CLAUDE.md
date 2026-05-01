<!-- OPENSPEC:START -->
# OpenSpec Instructions

Instructions for AI coding assistants using OpenSpec for spec-driven development.

## TL;DR Quick Checklist

- Search existing work: `openspec spec list --long`, `openspec list` (use `rg` only for full-text search)
- Decide scope: new capability vs modify existing capability
- Pick a unique `change-id`: kebab-case, verb-led (`add-`, `update-`, `remove-`, `refactor-`)
- Scaffold: `proposal.md`, `tasks.md`, `design.md` (only if needed), and delta specs per affected capability
- Write deltas: use `## ADDED|MODIFIED|REMOVED|RENAMED Requirements`; include at least one `#### Scenario:` per requirement
- Validate: `openspec validate [change-id] --strict --no-interactive` and fix issues
- Request approval: Do not start implementation until proposal is approved

## When to Create a Proposal

**Create a proposal for:**
- New features or capabilities
- Breaking changes (API, schema)
- Architecture or pattern changes
- Security or significant performance changes

**Skip proposal for:**
- Bug fixes (restoring intended behavior)
- Typos, formatting, comments
- Dependency updates (non-breaking)
- Tests for existing behavior

## Three-Stage Workflow

### Stage 1: Creating Changes

1. Review `openspec/project.md`, `openspec list`, and `openspec list --specs`
2. Choose a unique verb-led `change-id`, scaffold `proposal.md`, `tasks.md`, optional `design.md`, and spec deltas under `openspec/changes/<id>/`
3. Write spec deltas using `## ADDED|MODIFIED|REMOVED Requirements` with at least one `#### Scenario:` per requirement
4. Run `openspec validate <id> --strict --no-interactive` and resolve issues

### Stage 2: Implementing Changes

1. Read `proposal.md` — understand what's being built
2. Read `design.md` (if exists) — review technical decisions
3. Read `tasks.md` — get implementation checklist
4. Implement tasks sequentially; confirm every item is done before updating statuses
5. Set every task to `- [x]` after all work is complete
6. **Do not start implementation until the proposal is reviewed and approved**

### Stage 3: Archiving Changes

After deployment, create a separate PR to:
- Move `changes/[name]/` → `changes/archive/YYYY-MM-DD-[name]/`
- Update `specs/` if capabilities changed
- Use `openspec archive <change-id> --skip-specs --yes` for tooling-only changes
- Run `openspec validate --strict --no-interactive` to confirm archived change passes

## Directory Structure

```
openspec/
├── project.md              # Project conventions
├── specs/                  # Current truth — what IS built
│   └── [capability]/
│       ├── spec.md         # Requirements and scenarios
│       └── design.md       # Technical patterns
├── changes/                # Proposals — what SHOULD change
│   ├── [change-name]/
│   │   ├── proposal.md     # Why, what, impact
│   │   ├── tasks.md        # Implementation checklist
│   │   ├── design.md       # Technical decisions (optional)
│   │   └── specs/[capability]/spec.md  # ADDED/MODIFIED/REMOVED deltas
│   └── archive/            # Completed changes
```

## Proposal Structure

**proposal.md:**
```markdown
# Change: [Brief description]
## Why
[1-2 sentences on problem/opportunity]
## What Changes
- [Bullet list; mark breaking changes with **BREAKING**]
## Impact
- Affected specs: [capabilities]
- Affected code: [key files/systems]
```

**spec delta format:**
```markdown
## ADDED Requirements
### Requirement: New Feature
The system SHALL provide...

#### Scenario: Success case
- **WHEN** user performs action
- **THEN** expected result

## MODIFIED Requirements
### Requirement: Existing Feature
[Full updated requirement — paste entire block from existing spec and edit]
```

**Critical**: Every requirement needs `#### Scenario:` (4 hashtags). MODIFIED must include the full existing requirement text, not just the changed parts.

**design.md** — create only when: cross-cutting change, new external dependency, significant data model change, security/migration complexity, or ambiguity requiring technical decisions before coding.

## Key CLI Commands

```bash
openspec list                    # Active changes
openspec list --specs            # Existing capabilities
openspec show [item]             # View change or spec details
openspec validate [item] --strict --no-interactive  # Validate
openspec archive <change-id> --yes  # Archive after deployment
```

## Delta Operations

- `## ADDED Requirements` — new capabilities
- `## MODIFIED Requirements` — changed behavior (always include full requirement text)
- `## REMOVED Requirements` — deprecated features (include reason + migration)
- `## RENAMED Requirements` — name-only changes; use with MODIFIED if behavior also changes

<!-- OPENSPEC:END -->

# Decenza

Qt/C++ cross-platform controller for the Decent Espresso DE1 machine with BLE connectivity.

## User Manual

The end-user manual lives in the GitHub wiki at https://github.com/Kulitorum/Decenza/wiki/Manual. Consult it when working on user-visible behaviour to confirm documented expectations or the official wording for features. The wiki is a separate git repo (`Kulitorum/Decenza.wiki.git`) — clone it locally if you need to edit a manual page.

## Reference Documents

Detailed documentation lives in `docs/CLAUDE_MD/`. Read these when working in the relevant domain:

| Document | When to read |
|----------|-------------|
| `PROJECT_STRUCTURE.md` | Map of `src/`, `qml/`, `resources/`, signal/slot flow, profile pipeline |
| `CI_CD.md` | Release process, GitHub Actions workflows, version bumping |
| `PLATFORM_BUILD.md` | CLI build commands (Windows/macOS/iOS), Windows installer, Android signing, tablet quirks |
| `RECIPE_PROFILES.md` | Recipe Editor, D-Flow/A-Flow/Pressure/Flow types, frame generation, JSON format, stop limits, profile_sync tool |
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
| `SETTINGS.md` | Settings architecture: 7 domain sub-objects, how to add properties/domains, QML access pattern, build-blast rules |
| `QML_GOTCHAS.md` | QML bug-prone patterns with code samples (font conflict, reserved names, IME drop, etc.) |
| `QML_NAVIGATION.md` | StackView page navigation, phase-change handler, operation-page conventions |
| `SHOTSERVER.md` | ShotServer file split, async community endpoints, JS `fetch()` rules |

Read [`docs/SHOT_REVIEW.md`](https://github.com/Kulitorum/Decenza/blob/main/docs/SHOT_REVIEW.md) when working on the post-shot review / shot detail pages, the five quality-badge detectors (pour truncated, channeling, grind issue, temperature unstable, skip-first-frame), the Shot Summary dialog, badge persistence, or `src/ai/shotanalysis.{h,cpp}`. It is the source of truth for detector internals, gate semantics, and the recompute-on-load contract; keep it in sync when changing any of the above.

## Development Environment

- **ADB path**: `/c/Users/Micro/AppData/Local/Android/Sdk/platform-tools/adb.exe`
- **Uninstall app**: `adb uninstall io.github.kulitorum.decenza_de1`
- **WiFi debugging**: `192.168.1.212:5555` (reconnect: `adb connect 192.168.1.212:5555`). The DHCP lease can rotate — if reconnect fails, plug in USB and run `adb shell ip route | grep wlan` to read the current IP, then `adb tcpip 5555` + `adb connect <ip>:5555`.
- **Qt version**: 6.10.3
- **Qt path**: `C:/Qt/6.10.3/msvc2022_64`
- **C++ standard**: C++17
- **de1app source**: `C:\code\de1app` (Windows) or `/Users/jeffreyh/Development/GitHub/de1app` (macOS) — original Tcl/Tk DE1 app for reference
- **IMPORTANT**: Use relative paths (e.g., `src/main.cpp`) instead of absolute paths (e.g., `C:\CODE\de1-qt\src\main.cpp`) to avoid "Error: UNKNOWN: unknown error, open" when editing files

## Building

**Don't build automatically** — let the user build in Qt Creator (~50× faster than CLI). Only run CLI builds when the user explicitly asks. CLI commands for Windows/macOS/iOS live in `docs/CLAUDE_MD/PLATFORM_BUILD.md`.

## Project Structure

See `docs/CLAUDE_MD/PROJECT_STRUCTURE.md` for the full source tree, signal/slot flow, scale system, machine phases, AI/MCP overview, and profile pipeline. Top-level: `src/` (C++), `qml/` (UI), `resources/`, `shaders/`, `tests/`, `docs/`, `openspec/`, `android/`, `installer/`.

## Conventions

### Design Principles
- **Never use timers as guards/workarounds.** Timers are fragile heuristics that break on slow devices and hide the real problem. Use event-based flags and conditions instead. For example, "suppress X until Y has happened" should be a boolean cleared by the Y event, not a timer. Only use timers for genuinely periodic tasks (polling, animation, heartbeats) and **UI auto-dismiss** (toasts/banners that hide after N seconds). Everything else — including debounce — should use event-based flags.
- **Never run database or disk I/O on the main thread.** Use `QThread::create()` with a `QMetaObject::invokeMethod(..., Qt::QueuedConnection)` callback to run queries on a background thread and deliver results back to the main thread. See `ShotHistoryStorage::requestShot()` for the canonical pattern. For database connections inside background threads, always use the `withTempDb()` helper from `src/core/dbutils.h` — it handles unique connection naming, `busy_timeout`, `foreign_keys` pragmas, and cleanup. Never manually call `QSqlDatabase::addDatabase()`/`removeDatabase()` when `withTempDb` can be used instead.
- **Settings go in their domain sub-object, not on `Settings` directly.** `Settings` is a façade that owns 7 domain classes (`SettingsMqtt`, `SettingsTheme`, etc.). Add new properties to the matching `Settings<Domain>` class, or create a new sub-object if none fits. Never add a property back to `Settings` itself, and never `#include "settings_<domain>.h"` in `settings.h` — both undo the recompile-blast win the split was built for. New sub-objects also require `qmlRegisterUncreatableType<Settings<Domain>>(...)` in `main.cpp` or QML resolves `Settings.<domain>.<prop>` to `undefined` at runtime. QML access is **always** `Settings.<domain>.<prop>`, never the flat `Settings.<prop>`. See `docs/CLAUDE_MD/SETTINGS.md` for the full checklist.

### C++
- Classes: `PascalCase`; methods/variables: `camelCase`; members: `m_` prefix; slots: `onEventName()`
- Use `Q_PROPERTY` with `NOTIFY` for bindable properties
- Use `qsizetype` (not `int`) for container sizes — `QVector::size()`, `QList::size()`, `QString::size()` etc. return `qsizetype` (64-bit on iOS/macOS). Assigning to `int` causes `-Wshorten-64-to-32` warnings.

### QML
- Files: `PascalCase.qml` — new QML files **must** be added to `CMakeLists.txt` (in the `qt_add_qml_module` file list) to be included in the Qt resource system. Without this, the file won't be found at runtime.
- **New layout widgets** require registration in 4 places: (1) `CMakeLists.txt` file list, (2) `LayoutItemDelegate.qml` switch, (3) `LayoutEditorZone.qml` widget palette + chip label map, (4) `shotserver_layout.cpp` web editor widget list. Optionally add to `LayoutCenterZone.qml` if the widget should be allowed in center/idle zones.
- IDs/properties: `camelCase`
- Use `Theme.qml` singleton for all styling — never hardcode colors, font sizes, spacing, or radii. Use `Theme.textColor`, `Theme.bodyFont`, `Theme.subtitleFont`, `Theme.spacingMedium`, `Theme.cardRadius`, etc.
- All user-visible text must be internationalized. Use `TranslationManager.translate("section.key", "Fallback text")` for property bindings and inline expressions. Use the `Tr` component for standalone visible text (`Tr { key: "section.name"; fallback: "English text" }`). For text used in properties via `Tr`, use a hidden instance: `Tr { id: trMyLabel; key: "my.key"; fallback: "Label"; visible: false }` then `text: trMyLabel.text`. Reuse existing keys like `common.button.ok` and `common.accessibility.dismissDialog` where applicable.
- Use `StyledTextField` instead of `TextField` to avoid Material floating label
- `ActionButton` dims icon (50% opacity) and text (secondary color) when disabled

### QML Gotchas (one-liners — full samples in `docs/CLAUDE_MD/QML_GOTCHAS.md`)

- **Font property conflict**: don't mix `font: Theme.bodyFont` with `font.bold: true` — assign sub-properties individually.
- **Reserved names in JS model data**: `name`, `parent`, `children`, `data`, `state`, `enabled`, `visible`, `width`, `height`, `x`, `y`, `z`, `focus`, `clip` collide with QML properties — use `label` etc.
- **IME last-word drop**: call `Qt.inputMethod.commit()` before reading any `TextField.text` from a button handler — otherwise the in-progress word is lost on mobile.
- **Keyboard handling**: wrap pages with text inputs in `KeyboardAwareContainer { textFields: [...] }`.
- **FINAL Qt properties**: don't redeclare `message`/`title` etc. on `Popup`/`Dialog` (Qt 6.10+) — pick a different name like `resultMessage`.
- **Numeric defaults**: use `value ?? 0.6`, not `value || 0.6` — `||` treats `0` as falsy.
- **`native` is reserved**: use `nativeName`.
- **No Unicode glyphs as icons** (`"✎"`, `"☰"`): use SVG `Image` from `qrc:/icons/`. Safe: `°`, `·`, `—`, `→`, `×`.
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
- **Emoji**: always `Image { source: Theme.emojiToImage(value) }` — never `Text` for emojis. See `EMOJI_SYSTEM.md`.
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
