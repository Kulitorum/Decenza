## CI/CD (GitHub Actions)

All platforms build automatically when a `v*` tag is pushed. Each workflow can also be triggered manually via `workflow_dispatch` for **test builds only** (no version bump, no uploads by default).

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

**IMPORTANT**: Release notes should only include **user-experience changes** (new features, UI changes, bug fixes users would notice). Skip internal changes like code refactoring, developer tools, translation system improvements, or debug logging changes.

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
git tag vX.Y.Z
git push origin vX.Y.Z
```

This triggers all 6 platform builds simultaneously. Each workflow will:
- Bump the version code
- Build the binary
- Upload the artifact to the existing GitHub Release
- Android workflow commits the bumped version code back to main
- Android workflow injects `Build: XXXX` into the release notes
- iOS workflow uploads to App Store Connect

#### Updating an existing pre-release
To rebuild an existing pre-release at the current HEAD:
```bash
# Delete old tag and recreate at HEAD
git tag -d vX.Y.Z
git push origin :refs/tags/vX.Y.Z
git tag vX.Y.Z
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

### Notes
- **Always use tag pushes** — never `workflow_dispatch` — for release builds
- **Always review `git log <prev-release>..HEAD`** to include all changes in release notes
- `Build: XXXX` is injected automatically by CI — do not add manually
- Always include direct APK link in release notes (old browsers can't see Assets section)
- Remove `--prerelease` flag for stable releases
- APK files are for direct distribution (sideloading)
- AAB files are only for Google Play Store uploads
- Users cannot install AAB files directly
