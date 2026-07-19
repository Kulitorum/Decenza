## CI/CD (GitHub Actions)

CI runs at two moments: a **nightly sanitizer job** on `main`, and **all platforms build** when a `v*` tag is pushed. Each release workflow can also be triggered manually via `workflow_dispatch` for **test builds only** (no version bump, no uploads by default).

All workflows have concurrency controls — if the same workflow triggers twice for the same ref, the older run is cancelled. Artifacts use 1-day retention with overwrite, so only the latest artifact per platform exists at any time. Dependabot (`.github/dependabot.yml`) checks weekly for Actions dependency updates.

### Workflows

| Platform | Workflow | Runner | Output |
|----------|----------|--------|--------|
| Android | `android-release.yml` | ubuntu-24.04 | Signed APK |
| iOS | `ios-release.yml` | macos-15 | IPA → App Store |
| macOS | `macos-release.yml` | macos-15 | Signed + notarized DMG |
| Windows | `windows-release.yml` | windows-latest | Inno Setup installer |
| Linux | `linux-release.yml` | ubuntu-24.04 | AppImage |
| Linux ARM64 | `linux-arm64-release.yml` | ubuntu-24.04-arm | AppImage (aarch64) |

On tag push: all workflows bump version code and build. All except iOS upload to GitHub Release; iOS uploads to App Store Connect instead. On `workflow_dispatch`: build only, no version bump, no upload (unless explicitly opted in).

### Nightly sanitizers (`nightly-sanitizers.yml`)

Runs at 04:23 UTC on `main` (plus `workflow_dispatch`): two independent Linux x64 builds, one under **UBSan** and one under **ASan**, each running the full `ctest` suite. Uploads nothing, never touches `versioncode.txt`, never interacts with a Release.

**There is deliberately no pull-request gate.** A `pre-merge.yml` existed briefly and was removed, and the reason is worth keeping. Three detectors (UBSan, ASan, `-Wall -Wextra`) were run for the first time across eight months of previously unexamined code — the moment a new tool's harvest should be largest — and they surfaced **no pre-existing runtime defects**. The only two failures that job ever produced were problems it introduced itself. A near-empty first harvest is evidence the codebase is clean on those axes, so the expected future yield is low too, and a low-yield detector does not belong on the critical path of every push. It costs ~3 minutes per push plus cache budget; nightly costs idle CI nobody waits on and still catches a regression within a day.

The full test suite is already run locally before every pull request, so tests are gated by process rather than by CI.

**What a green night does not mean:** one platform (Linux x64) — #1558 was inside `#ifdef Q_OS_IOS` and would not be caught here; only code the 83 tests execute, and coverage is unmeasured; and nothing about data races, since ThreadSanitizer is unusable against an uninstrumented Qt (see `TESTING.md`).

**Compiler diagnostics are not in CI at all, by design.** `-Wall -Wextra -Werror` is on in every build, so a warning is an error on the developer's own machine — which is where it should be found, not in a log read once a day.

**Six-platform evidence rule for promoting a diagnostic to `-Werror`.** Before adding any `-Werror=<name>`, show a green build on all six platforms (Windows, macOS, iOS, Android, Linux x64, Linux arm64). This exists because `-Werror=unused-result` (#1553) was verified on macOS and Android only, then broke the iOS release build on code inside `#ifdef Q_OS_IOS`. Platform-guarded code is invisible to every platform that does not compile it, so evidence from a subset is not evidence.

**Compiler enforcement has an annotation boundary — a clean build is not a clean codebase.** `-Werror=unused-result` caught a discarded `SecRandomCopyBytes` result because Apple annotates it `warn_unused_result`; the identical defect on the OpenSSL path — a discarded `RAND_bytes`, compiled by five of six platforms — produced no diagnostic at all, because OpenSSL does not annotate it. Where a checked result is deliberately ignored, write `(void)call();` with a comment saying why.

### Nightly platform builds (`nightly-platforms.yml`)

Runs at 02:11 UTC on `main`: dispatches all six release workflows in build-only mode (`upload_to_release=false`, `use_cache=false`). It does not run tests — `nightly-sanitizers.yml` does that — it answers one question: does it still compile everywhere?

This exists because the five platforms nobody compiles locally are otherwise first built at release-tag time, after merge. Not hypothetical: enabling `-Wall -Wextra -Werror` produced findings that exactly one platform each could see (GCC's stricter `-Wshadow`, an iOS-only unused lambda capture behind `#ifdef Q_OS_IOS`, Android-only `-Wcast-align` and JNI narrowing). Two of those scored **zero** in the macOS measurement because that code is behind `#ifdef`.

**`use_cache`** is a `workflow_dispatch` input on all six release workflows, default `true`. Nightly passes `false` so the builds are genuinely cold — nobody waits on them, so a compiler cache would only buy wall-clock nobody measures while writing a fresh timestamped generation into a cache store already near the 10 GB cap. The guard is `github.event_name != 'workflow_dispatch' || inputs.use_cache`, so a **tag push always caches**; without the event check, `inputs` is empty on a tag push and every release would silently go cold. The Qt install cache is unaffected — it is a stable-keyed download, not a compiler cache.

**Cache pruning:** `prune-caches.yml` fires whenever a build workflow (including `nightly-sanitizers.yml`) completes (plus a daily cron fallback, since `workflow_run` for tag-triggered runs is an under-documented edge); it skips while any build is still queued/running (the last one to finish re-triggers it), then deletes all but the newest copy of each timestamped ccache/sccache entry per (prefix, ref). Stable-keyed `qt-*`/`openssl-*` caches are never touched. This keeps the repo's cache store (10 GB GitHub cap) from filling with stale compiler-cache generations; it replaced the old KEEP=2 prune step inside `macos-release.yml`, which ran before late-finishing builds (and macOS itself) had saved their fresh caches.

### Quick commands
```bash
# Trigger individual TEST builds (no upload, no version bump)
gh workflow run android-release.yml --repo Kulitorum/Decenza -f upload_to_release=false
gh workflow run ios-release.yml --repo Kulitorum/Decenza -f upload_to_appstore=false
gh workflow run windows-release.yml --repo Kulitorum/Decenza -f upload_to_release=false
gh workflow run macos-release.yml --repo Kulitorum/Decenza -f upload_to_release=false
gh workflow run linux-release.yml --repo Kulitorum/Decenza -f upload_to_release=false
gh workflow run linux-arm64-release.yml --repo Kulitorum/Decenza -f upload_to_release=false

# Check build status
gh run list --repo Kulitorum/Decenza --limit 5

# Watch live logs
gh run watch --repo Kulitorum/Decenza

# View failed logs
gh run view --repo Kulitorum/Decenza --log-failed
```

### Release all platforms at once
See "Publishing Releases" section below for the full process. In short:
```bash
# 1. Create the GitHub Release FIRST (so CI finds it)
gh release create vX.Y.Z --title "Decenza vX.Y.Z" --prerelease --notes "..."
# 2. Then push the tag to trigger all 6 builds
git tag vX.Y.Z
git push origin vX.Y.Z
```

### GitHub Secrets

**Android:**
- `ANDROID_KEYSTORE_BASE64` — Base64-encoded `.jks` keystore
- `ANDROID_KEYSTORE_PASSWORD` — Keystore password

**iOS:**
- `P12_CERTIFICATE_BASE64`, `P12_PASSWORD` — iPhone Distribution certificate
- `PROVISIONING_PROFILE_BASE64`, `PROVISIONING_PROFILE_NAME` — App Store profile
- `KEYCHAIN_PASSWORD` — Temporary keychain password
- `APPLE_TEAM_ID` — Apple Developer Team ID
- `APP_STORE_CONNECT_API_KEY_ID`, `APP_STORE_CONNECT_API_ISSUER_ID`, `APP_STORE_CONNECT_API_KEY_BASE64` — App Store upload

**macOS:**
- `MACOS_DEVELOPER_ID_P12_BASE64`, `MACOS_DEVELOPER_ID_P12_PASSWORD` — Developer ID cert
- `APPLE_ID`, `APPLE_ID_APP_PASSWORD` — For notarization

### Platform notes
- iOS bundle ID: `io.github.kulitorum.decenza` (differs from Android: `io.github.kulitorum.decenza_de1`)
- iOS signing credentials expire yearly — see `docs/IOS_CI_FOR_CLAUDE.md` for renewal
- iOS tag-push builds upload to App Store Connect automatically (available in TestFlight). Manual `workflow_dispatch` builds default to `upload_to_appstore=false` (test only). App Store submission remains a manual step in App Store Connect. See `docs/IOS_TESTFLIGHT_SETUP.md` for setup instructions.
- Android keystore path is configurable via `ANDROID_KEYSTORE_PATH` env var (falls back to local path)
- Android build uses `build.gradle` post-build hook for signing and versioned APK naming

## Publishing Releases

### Prerequisites
- GitHub CLI (`gh`) installed: `winget install GitHub.cli`
- Authenticated: `gh auth login`

### Release Process

**IMPORTANT: Always use tag pushes to build releases.** Never use `workflow_dispatch` for release builds — it skips version code bumps and causes duplicate upload errors (especially iOS App Store). The `workflow_dispatch` trigger is only for test builds that don't upload anywhere.

**IMPORTANT**: Release notes should only include **user-experience changes** (new features, UI changes, bug fixes users would notice). Skip internal changes like code refactoring, developer tools, translation system improvements, or debug logging changes. Use sectioned format: `### New Features`, `### Improvements`, `### Bug Fixes`.

#### Step 1: Review changes since last release
```bash
gh release list --limit 5
git log <previous-tag>..HEAD --oneline
```

#### Step 2: Create the GitHub Release FIRST (before pushing the tag)
**You must create the release before pushing the tag.** If the release doesn't exist when CI runs, behavior varies: Android and Linux workflows auto-create a non-prerelease with auto-generated notes (losing your custom notes and prerelease flag), while macOS and Windows silently skip the upload (artifacts lost). Creating it first ensures all platforms upload correctly with your release notes and prerelease flag.

The `Build: XXXX` line is injected automatically by CI after the Android build completes. Do NOT add it manually.

For beta/prerelease builds, add `--prerelease` flag. Users with "Beta updates" enabled in Settings will get these. Omit `--prerelease` for stable releases.

```bash
gh release create vX.Y.Z \
  --title "Decenza vX.Y.Z" \
  --prerelease \
  --notes "$(cat <<'EOF'
## Changes

### New Features
- Feature 1 (from commit messages)
- Feature 2

### Improvements
- Improvement 1

### Bug Fixes
- Fix 1
- Fix 2

## Installation

**Direct APK download:** https://github.com/Kulitorum/Decenza/releases/download/vX.Y.Z/Decenza_X.Y.Z.apk

Install on your Android device (allow unknown sources).
EOF
)"
```

#### Step 3: Push the tag to trigger builds
```bash
# IMPORTANT: Verify local main is synced with origin BEFORE tagging.
# Stale tracking branches or failed pulls can leave HEAD on the wrong commit.
git fetch origin && git reset --hard origin/main
git rev-parse HEAD  # Verify this matches the expected commit

git tag vX.Y.Z
git rev-parse vX.Y.Z  # Must match HEAD above
git push origin vX.Y.Z
```

This triggers all 6 platform builds simultaneously. Each workflow will:
- Bump the version code
- Build the binary
- Upload the artifact to the existing GitHub Release
- Android workflow commits the bumped version code back to main
- Android workflow injects `Build: XXXX` into the release notes
- iOS workflow uploads to App Store Connect

**Cache warming:** GitHub Actions cache *restore* is ref-scoped — a tag-push build can only restore caches from its own tag ref or `main`. To seed `main`-scoped Qt + ccache caches so the next release can restore them instead of building fully cold, the dedicated `warm-main-cache.yml` workflow listens for the `release: released` event (fired when any release is published as non-prerelease — typically a pre-release promoted to a full release, see "Promoting a pre-release to stable" below) and dispatches all 6 build workflows on `--ref main`. Warming runs when a stable release exists rather than at tag-push time, because at tag-push time the release is still a pre-release and the build's caches are tag-ref-scoped, so a `main`-scoped warm build is not reachable then. See issue #1213.

#### Updating an existing pre-release
To rebuild an existing pre-release at the current HEAD:
```bash
# IMPORTANT: Verify local main is synced with origin BEFORE tagging.
git fetch origin && git reset --hard origin/main
git rev-parse HEAD  # Verify this matches the expected commit

# Delete old tag and recreate at HEAD
git tag -d vX.Y.Z
git push origin :refs/tags/vX.Y.Z
git tag vX.Y.Z
git rev-parse vX.Y.Z  # Must match HEAD above
git push origin vX.Y.Z

# IMPORTANT: Deleting the remote tag automatically converts the release to a draft.
# You MUST run this after pushing the new tag to restore it as a visible pre-release:
gh release edit vX.Y.Z --draft=false --prerelease
```
**Note:** Do NOT delete the GitHub Release — only the tag. The release persists and CI will upload new artifacts to it. Draft releases are invisible to users and the auto-update system, so the `--draft=false` step is mandatory.

### Updating Release Notes
```bash
gh release edit vX.Y.Z --notes "$(cat <<'EOF'
Updated notes here...
EOF
)"
```

### Auto-Update System
- **Check interval**: Every 60 minutes (configurable in Settings → Updates)
- **Version detection**: Compares display version (`X.Y.Z`), then falls back to build number if versions are equal
- **Build number source**: Parsed from release notes using pattern `Build: XXXX` (or `Build XXXX`)
- **Beta channel**: Users opt-in via Settings → Updates → "Beta updates". Prereleases are only shown to opted-in users.
- **Platforms**: Android auto-downloads APK; iOS directs to App Store; desktop shows release page

### Updating a pre-release with new commits
When adding commits to an existing pre-release (same version, new builds):
```bash
# 1. Update the release notes
gh release edit vX.Y.Z --notes "..."
# 2. Move the tag to HEAD and force-push to trigger all 6 builds
git tag -f vX.Y.Z HEAD && git push origin vX.Y.Z --force
```
Both steps are required — updating release notes alone does NOT trigger builds. The tag must be moved to include the new commits, and the force-push is what triggers CI.

### Promoting a pre-release to stable
When promoting a pre-release to a full release, you must also set it as "latest" — GitHub does not do this automatically:
```bash
gh release edit vX.Y.Z --prerelease=false --latest
```
Without `--latest`, the previous stable release remains the "latest" and the auto-update system won't see the new version. Note: promoting does NOT re-trigger the release builds — the artifacts from the pre-release tag push are already attached. It *does* fire the `release: released` event, which triggers `warm-main-cache.yml` to dispatch all 6 build workflows on `main` (no upload, no version bump) purely to warm `main`-scoped caches for the next release.

### Notes
- **Always use tag pushes** — never `workflow_dispatch` — for release builds
- **Always review `git log <prev-release>..HEAD`** to include all changes in release notes
- `Build: XXXX` is injected automatically by CI — do not add manually
- Always include direct APK link in release notes (old browsers can't see Assets section)
- APK files are for direct distribution (sideloading)
- AAB files are only for Google Play Store uploads
- Users cannot install AAB files directly
