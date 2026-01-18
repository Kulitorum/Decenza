# iOS CI/CD System for Claude Code

This document explains how to build and deploy the iOS app to the App Store from any machine (including Windows PC) using GitHub Actions.

## Overview

The iOS build runs entirely on GitHub's macOS servers. You don't need a Mac, Xcode, or Qt installed locally. You just trigger the workflow and GitHub does the rest.

**Workflow file:** `.github/workflows/ios-release.yml`

## How It Works

1. **Trigger**: You trigger the workflow via GitHub CLI or the GitHub website
2. **GitHub spins up a macOS VM** with Xcode pre-installed
3. **Qt 6.10.1 for iOS** is installed (cached after first run, takes ~20 seconds)
4. **Signing credentials** are decoded from GitHub Secrets and installed
5. **CMake configures** the project using Qt's `qt-cmake`
6. **Xcode builds and archives** the app
7. **IPA is exported** and uploaded to App Store Connect
8. **Artifact is saved** for 30 days (downloadable from GitHub)

## Triggering a Build

### Option 1: GitHub CLI (Recommended)

```bash
gh workflow run ios-release.yml --repo Kulitorum/de1-qt
```

Or with the upload option disabled (just build, don't upload):
```bash
gh workflow run ios-release.yml --repo Kulitorum/de1-qt -f upload_to_appstore=false
```

### Option 2: Git Tag

Pushing a version tag automatically triggers the workflow:
```bash
git tag v1.2.0
git push --tags
```

### Option 3: GitHub Website

1. Go to https://github.com/Kulitorum/de1-qt/actions
2. Click "iOS App Store Build" in the left sidebar
3. Click "Run workflow" dropdown
4. Choose options and click "Run workflow"

## Monitoring Build Progress

### Check status via CLI:
```bash
gh run list --repo Kulitorum/de1-qt --limit 5
```

### Watch live logs:
```bash
gh run watch --repo Kulitorum/de1-qt
```

### Get logs from a failed run:
```bash
# List runs to get the run ID
gh run list --repo Kulitorum/de1-qt --limit 5

# View logs for a specific run
gh run view <RUN_ID> --repo Kulitorum/de1-qt --log

# View only failed step logs
gh run view <RUN_ID> --repo Kulitorum/de1-qt --log-failed
```

### Open in browser:
```bash
# Windows
start https://github.com/Kulitorum/de1-qt/actions

# Or use gh
gh run view --repo Kulitorum/de1-qt --web
```

## Build Artifacts

After a successful build, the IPA file is saved as an artifact:

### Download via CLI:
```bash
gh run download <RUN_ID> --repo Kulitorum/de1-qt
```

### Download via browser:
1. Go to the completed workflow run
2. Scroll to "Artifacts" section
3. Click "Decenza_DE1-iOS" to download

Artifacts are retained for 30 days.

## What Gets Uploaded to App Store Connect

When `upload_to_appstore` is true (default), the workflow:
1. Exports a signed IPA
2. Uses `xcrun altool` with the App Store Connect API key
3. Uploads directly to App Store Connect

After upload, the build appears in App Store Connect within ~5-15 minutes for processing.

## GitHub Secrets Reference

These secrets are configured in the repository and should NOT be modified unless credentials expire:

| Secret | Purpose | Expiration |
|--------|---------|------------|
| `P12_CERTIFICATE_BASE64` | Distribution certificate | 1 year from creation |
| `P12_PASSWORD` | Password for .p12 file | Never |
| `PROVISIONING_PROFILE_BASE64` | App Store provisioning profile | 1 year |
| `PROVISIONING_PROFILE_NAME` | Profile name: "Decenza App Store Manual" | Never |
| `KEYCHAIN_PASSWORD` | Temporary keychain password | Never |
| `APPLE_TEAM_ID` | Team ID: HKHN2RK2P4 | Never |
| `APP_STORE_CONNECT_API_KEY_ID` | API Key ID: 7G779Y38W5 | Never (unless revoked) |
| `APP_STORE_CONNECT_API_ISSUER_ID` | API Issuer ID | Never |
| `APP_STORE_CONNECT_API_KEY_BASE64` | API private key (.p8) | Never (unless revoked) |

## Common Issues and Solutions

### 1. "No signing certificate found"

**Cause:** Certificate expired or doesn't match provisioning profile.

**Solution:**
- Check certificate expiration in Apple Developer Portal
- Re-export certificate and update `P12_CERTIFICATE_BASE64` secret

### 2. "Provisioning profile doesn't match bundle identifier"

**Cause:** The app's bundle ID doesn't match the provisioning profile.

**Current bundle ID:** `io.github.kulitorum.decenza` (set in CMakeLists.txt line 434)

**Solution:**
- Verify CMakeLists.txt has: `XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "io.github.kulitorum.decenza"`
- If changed, create new provisioning profile in Apple Developer Portal

### 3. "API key not authorized"

**Cause:** App Store Connect API key revoked or wrong issuer ID.

**Solution:**
- Check API key exists at https://appstoreconnect.apple.com/access/integrations/api
- Verify Issuer ID matches (shown at top of that page)
- Create new key if needed and update secrets

### 4. Build succeeds but upload fails

**Cause:** Usually network issues or App Store Connect problems.

**Solution:**
- Download the IPA artifact manually
- Upload using Transporter app (available on Mac App Store) or via web

### 5. Qt installation fails

**Cause:** Qt version not available or network issues.

**Solution:**
- Check https://github.com/jurplel/install-qt-action for supported versions
- The workflow uses Qt 6.10.1 - if unavailable, update version in workflow

## Updating Credentials

### When certificate expires (yearly):

On a Mac:
1. Create new certificate at https://developer.apple.com/account/resources/certificates/add
2. Download and install in Keychain
3. Export as .p12: `Keychain Access → My Certificates → Right-click → Export`
4. Convert to base64: `base64 -i certificate.p12 | tr -d '\n'`
5. Update `P12_CERTIFICATE_BASE64` secret in GitHub
6. Create new provisioning profile that includes the new certificate
7. Update `PROVISIONING_PROFILE_BASE64` secret

### When provisioning profile expires (yearly):

1. Go to https://developer.apple.com/account/resources/profiles
2. Create new App Store profile for `io.github.kulitorum.decenza`
3. Download and convert: `base64 -i profile.mobileprovision | tr -d '\n'`
4. Update `PROVISIONING_PROFILE_BASE64` secret
5. Update `PROVISIONING_PROFILE_NAME` if name changed

## Version Management

The app version is managed in two places:

1. **Display version** (e.g., "1.1.38"): Set in `CMakeLists.txt` line 2
   ```cmake
   project(Decenza_DE1 VERSION 1.1.38 LANGUAGES CXX)
   ```

2. **Build number** (e.g., 1971): Auto-increments in `versioncode.txt`
   - Increments on every build (any platform)
   - Never manually reset this

### To release a new version:

1. Update VERSION in CMakeLists.txt:
   ```bash
   # Edit CMakeLists.txt line 2 to new version
   ```

2. Commit and push:
   ```bash
   git add CMakeLists.txt
   git commit -m "Bump version to 1.2.0"
   git push
   ```

3. Trigger iOS build:
   ```bash
   gh workflow run ios-release.yml --repo Kulitorum/de1-qt
   ```

4. Optionally tag the release:
   ```bash
   git tag v1.2.0
   git push --tags
   ```

## Workflow File Location

The workflow is defined in:
```
.github/workflows/ios-release.yml
```

Key sections:
- **Lines 28-35**: Qt installation with caching
- **Lines 37-65**: Certificate and profile installation
- **Lines 67-72**: CMake configuration
- **Lines 74-89**: Xcode build and archive
- **Lines 91-114**: IPA export
- **Lines 116-132**: App Store Connect upload

## Testing Changes

To test workflow changes without uploading to App Store:

```bash
gh workflow run ios-release.yml --repo Kulitorum/de1-qt -f upload_to_appstore=false
```

This builds and archives but skips the upload step. You can download the IPA artifact to verify the build.

## Architecture Notes

- **Bundle ID**: `io.github.kulitorum.decenza` (Android uses `io.github.kulitorum.decenza_de1`)
- **Team ID**: `HKHN2RK2P4`
- **Minimum iOS**: 14.0 (set in CMakeLists.txt)
- **Qt Version**: 6.10.1
- **Signing**: Manual signing with explicit certificate and profile

## Quick Reference Commands

```bash
# Trigger build with upload
gh workflow run ios-release.yml --repo Kulitorum/de1-qt

# Trigger build without upload
gh workflow run ios-release.yml --repo Kulitorum/de1-qt -f upload_to_appstore=false

# Check build status
gh run list --repo Kulitorum/de1-qt --limit 5

# Watch live
gh run watch --repo Kulitorum/de1-qt

# View failed logs
gh run view --repo Kulitorum/de1-qt --log-failed

# Download artifact
gh run download <RUN_ID> --repo Kulitorum/de1-qt

# Open Actions in browser
gh browse --repo Kulitorum/de1-qt -- actions
```
