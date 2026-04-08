# iOS CI/CD Setup for GitHub Actions

This guide explains how to set up automated iOS builds that can be triggered from anywhere (your PC, phone, or GitHub website).

## Overview

The workflow (`.github/workflows/ios-release.yml`) will:
1. Build the iOS app using Qt 6.10.3
2. Sign it with your distribution certificate
3. Upload to App Store Connect
4. Save the IPA as a downloadable artifact

## Required GitHub Secrets

Go to your repo → **Settings** → **Secrets and variables** → **Actions** → **New repository secret**

You need to add these 8 secrets:

| Secret Name | Description |
|-------------|-------------|
| `P12_CERTIFICATE_BASE64` | Your distribution certificate (base64 encoded) |
| `P12_PASSWORD` | Password for the .p12 file |
| `PROVISIONING_PROFILE_BASE64` | Your App Store provisioning profile (base64 encoded) |
| `PROVISIONING_PROFILE_NAME` | Name of the provisioning profile (as shown in Apple Developer Portal) |
| `KEYCHAIN_PASSWORD` | Any random password (just make one up, e.g., `gh-actions-keychain-2024`) |
| `APPLE_TEAM_ID` | Your Apple Developer Team ID (10 characters) |
| `APP_STORE_CONNECT_API_KEY_ID` | App Store Connect API Key ID |
| `APP_STORE_CONNECT_API_ISSUER_ID` | App Store Connect API Issuer ID |
| `APP_STORE_CONNECT_API_KEY_BASE64` | App Store Connect API private key (base64 encoded) |

## Step-by-Step Setup

### 1. Export Distribution Certificate (.p12)

On your Mac:

1. Open **Keychain Access**
2. Select **login** keychain → **My Certificates** category
3. Find your **"Apple Distribution: ..."** certificate
4. Right-click → **Export**
5. Save as `.p12` file with a password
6. Convert to base64:
   ```bash
   base64 -i certificate.p12 | pbcopy
   ```
7. Paste into GitHub secret: `P12_CERTIFICATE_BASE64`
8. Add the password to: `P12_PASSWORD`

### 2. Download Provisioning Profile

1. Go to [Apple Developer Portal](https://developer.apple.com/account/resources/profiles/list)
2. Find your **App Store** distribution profile for `io.github.kulitorum.decenza`
3. Download the `.mobileprovision` file
4. Note the profile name (e.g., "Decenza App Store") → add to `PROVISIONING_PROFILE_NAME`
5. Convert to base64:
   ```bash
   base64 -i profile.mobileprovision | pbcopy
   ```
6. Paste into GitHub secret: `PROVISIONING_PROFILE_BASE64`

### 3. Find Your Team ID

1. Go to [Apple Developer Membership](https://developer.apple.com/account/#!/membership)
2. Copy your **Team ID** (10-character alphanumeric)
3. Add to GitHub secret: `APPLE_TEAM_ID`

### 4. Create App Store Connect API Key

This is used for uploading to App Store Connect without your Apple ID password.

1. Go to [App Store Connect → Users and Access → Keys](https://appstoreconnect.apple.com/access/api)
2. Click **+** to create a new key
3. Name: `GitHub Actions` (or whatever you like)
4. Access: **App Manager** (minimum needed for uploads)
5. Click **Generate**
6. **Download the .p8 file immediately** (you can only download once!)
7. Note the **Key ID** (shown in the table) → add to `APP_STORE_CONNECT_API_KEY_ID`
8. Note the **Issuer ID** (shown at top of page) → add to `APP_STORE_CONNECT_API_ISSUER_ID`
9. Convert the .p8 to base64:
   ```bash
   base64 -i AuthKey_XXXXXXXXXX.p8 | pbcopy
   ```
10. Paste into GitHub secret: `APP_STORE_CONNECT_API_KEY_BASE64`

### 5. Set a Keychain Password

Just make up any password and add it to `KEYCHAIN_PASSWORD`. This is only used temporarily during the build.

Example: `github-actions-temp-keychain-12345`

## How to Trigger a Build

### Option A: Manual trigger (from GitHub website)
1. Go to your repo → **Actions** tab
2. Select **iOS App Store Build**
3. Click **Run workflow**
4. Choose whether to upload to App Store Connect
5. Click **Run workflow**

### Option B: Push a version tag
```bash
git tag v1.2.0
git push --tags
```

### Option C: GitHub CLI (from your PC)
```bash
gh workflow run ios-release.yml
```

## Troubleshooting

### "No signing certificate found"
- Make sure the certificate hasn't expired
- Verify the base64 encoding is correct (no newlines)

### "Provisioning profile doesn't match"
- Ensure the profile is for App Store distribution (not Development or Ad Hoc)
- Check the bundle ID matches: `io.github.kulitorum.decenza`

### "API key not authorized"
- Ensure the API key has at least "App Manager" role
- Verify the Issuer ID is correct (easy to confuse with Team ID)

### Build succeeds but upload fails
- The IPA is still saved as an artifact - you can download and upload manually via Transporter

## Quick Base64 Commands (Mac)

```bash
# Encode a file and copy to clipboard
base64 -i yourfile.p12 | pbcopy

# Encode and save to file (to transfer to PC)
base64 -i yourfile.p12 > yourfile.p12.base64.txt

# Verify encoding (decode and check)
base64 -d -i yourfile.p12.base64.txt -o decoded.p12
```

## Security Notes

- GitHub secrets are encrypted and never exposed in logs
- The keychain and certificate files are deleted after each build
- API keys can be revoked anytime from App Store Connect
- Consider using a dedicated App Store Connect API key just for CI
