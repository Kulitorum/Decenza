## Context

See `proposal.md` for motivation. The current updater separates the UI/state machine (`FirmwareUpdater`) from firmware source management (`FirmwareAssetCache`), but the source manager is network-oriented: it stores CDN URLs, ETags, a resumable cache file, and a `.meta.json` sidecar. The flash procedure already accepts a local file path plus parsed header, so the cleanest change is to make source selection produce a validated local bundled image and leave erase/upload/verify behavior alone.

Firmware handling is user-visible and machine-facing, so `docs/CLAUDE_MD/FIRMWARE_UPDATE.md` and the wiki manual need to move with the code. The requested UI wording is "Use early access firmware"; the historical nightly preference is removed during the one-time upgrade so existing users start on Stable.

## Goals / Non-Goals

**Goals:**

- Make Stable and Early access firmware images and their Decaid manifest metadata reproducible, pinned release assets inside Decenza.
- Preserve the existing `FirmwareUpdater` state machine, button semantics, downgrade/reflash behavior, simulator gate, and BLE flash procedure.
- Replace CDN availability/download behavior with bundled image selection and validation.
- Reset legacy nightly selections to Stable during a one-time upgrade while presenting "Early access" wording everywhere users see the opt-in channel.
- Add tests that fail if a bundled image, manifest entry, digest, version, release notes, or channel mapping drifts.

**Non-Goals:**

- No dynamic firmware update feed, remote manifest, or app hot-patching of firmware bytes.
- No change to the DE1 BLE flash protocol, pacing, reboot flow, or verification interpretation.
- No support for additional DE1 firmware versions beyond 1352 Stable and 1358 Early access in this change.
- No automatic migration of a user to Early access; Stable remains the default.

## Decisions

**D1 - Import Decaid's bundled firmware manifest as the source of truth.**

Pull in Decaid's firmware manifest beside the firmware assets, either verbatim if its schema is convenient for Qt/C++ consumption or via a tiny build-time/checked-in adaptation that preserves the Decaid fields. It has one entry per bundled image:

| Field | 1352 Stable | 1358 Early access |
|---|---:|---:|
| id | `de1-1352` | `de1-1358` |
| build | `1352` | `1358` |
| byteLength | `463872` | `463872` |
| sha256 | `d9433b85167566d7b457e03e2151e860c10ff5d3b4e41b163667b8314aeb2927` | `ada25161ebbd661b44b3aab2c7756f42d95064a931462b3d134e8db66d198747` |
| expectedHeaderBoardMarker | `3725590529` (`0xDE100001`) | `3725590529` (`0xDE100001`) |
| expectedBodyByteCount | `461824` | `461824` |
| expectedCpuByteCount | `298592` | `298880` |

The manifest should carry Decaid's `assetPath`, `versionLabel`, `channel`, release notes, supported models, and provenance. Decenza's product labels may map Decaid's 1358 entry to "Early access" even if the upstream Decaid manifest calls the artifact stable. Rationale: the app can validate the bundled files without depending on filenames or header parsing alone, tests can assert the exact release contents, and release notes stay close to the firmware bytes. Alternative considered: hard-code two entries in C++. That is simpler initially but makes the binary resources less auditable and easier to update incompletely.

**D1a - Surface release notes from the selected manifest entry in the Firmware tab.**

Expose the selected firmware entry's release notes through the updater/source object already used by QML. The Firmware tab should show the notes near the selected/available version information, with the channel and version context visible so users do not confuse Stable notes with Early access notes. Keep the copy short and plain; the manifest owns the release-note text, while QML owns only the label and layout. Alternative considered: put release notes only in documentation. That misses the moment when the user decides whether to flash.

**D2 - Keep `FirmwareAssetCache` as the updater-facing abstraction, but remove network behavior from the default firmware path.**

Retain the existing signal shape (`checkFinished`, `downloadFinished`, `downloadFailed`, progress where meaningful) so `FirmwareUpdater` stays mostly unchanged. Internally, `checkForUpdate(installedVersion)` reads the selected catalog entry and classifies Newer/Same/Older; `downloadIfNeeded()` becomes `load selected bundled file, validate, emit path/header`. It may keep the class name for scope control, even though it no longer caches remote assets.

Alternative considered: introduce a parallel `BundledFirmwareSource` and refactor `FirmwareUpdater` to an interface. That is cleaner in the abstract, but this change removes the old CDN source rather than supporting both, so a second runtime source would add indirection with no user-facing value.

**D3 - Do not copy firmware bytes to writable app data before flashing unless Qt resource access makes it necessary.**

Prefer opening the bundled firmware resource directly if the existing upload code can stream it by path or by `QFile`. If platform resource paths cannot satisfy the existing `validateFile(path)` / upload code, copy the selected resource to the existing app-data firmware location immediately before validation, then validate the copied file against the manifest digest and header. Rationale: avoid unnecessary disk writes on the normal path, but preserve a pragmatic fallback for APIs that require a filesystem path.

Alternative considered: always extract to app data. That would resemble the current cache path and minimize code churn, but it reintroduces stale-file concerns the bundling change is meant to eliminate. If extraction is used, the file should be overwritten from the resource each time the user starts an update, not reused as a cache.

**D4 - Replace the historical channel preference with a new Early access opt-in.**

The setting is exposed as `firmwareEarlyAccess` and persisted under `firmware/EA`. The default is `false`, selecting Stable. A one-time settings upgrade removes the historical `firmware/nightlyChannel` key, writes `firmware/EA = false`, and stamps a migration guard. Users who previously selected nightly start on Stable, while users who later select Early access retain that explicit new choice.

Alternative considered: reuse or migrate the old boolean. That would silently carry old nightly selections into Early access, which is not the requested upgrade behavior. Leaving the key inert was also rejected because it retains obsolete user data after the upgrade.

**D5 - Treat manifest or bundled-file mismatch as a non-retryable application packaging error.**

Network failures go away, but packaging drift becomes the primary failure mode. A missing resource, wrong size, digest mismatch, board-marker mismatch, or header/version mismatch should stop before any BLE write and present the existing invalid-firmware message. Rationale: Retry cannot fix an application bundle containing the wrong bytes.

## Risks / Trade-offs

- [Risk] App package size increases by roughly 928 KB plus resource overhead. -> Mitigation: keep only the two requested DE1 images and avoid bundling historical builds.
- [Risk] The Decaid PR #594 asset can change before merge. -> Mitigation: pin the exact blob metadata and SHA-256 used during implementation; if PR metadata changes, update the manifest and tests in the same change.
- [Risk] Qt resource access differs across platforms for large binary files. -> Mitigation: test direct resource validation; if any platform needs a real path, extract to app data with overwrite-and-validate semantics.
- [Risk] Existing users who enabled nightly may expect to remain on an experimental channel. -> Mitigation: start all users on Stable and make Early access a fresh, explicit opt-in through the Firmware tab.
- [Risk] Removing CDN behavior may leave tests named around HEAD/Range/cache concepts. -> Mitigation: replace those tests with catalog/source tests rather than preserving obsolete implementation vocabulary.

## Migration Plan

1. Add bundled assets and manifest under a resource path included by CMake on every platform.
2. Replace the firmware source logic and tests so the selected bundled image is the only source.
3. Replace the legacy nightly preference with the new `firmware/EA` Early access opt-in; a one-time upgrade removes the old key, resets the new setting to Stable, and records completion.
4. Update QML text, translations/fallbacks, accessibility label, and source note to say Early access and bundled firmware.
5. Update `docs/CLAUDE_MD/FIRMWARE_UPDATE.md` and the wiki manual.
6. Rollback is a normal code revert: remove bundled assets/manifest and restore CDN source behavior.
