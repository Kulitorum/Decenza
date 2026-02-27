# iOS TestFlight Setup

All iOS CI builds (including pre-releases) are uploaded to App Store Connect automatically. Once uploaded, builds appear in TestFlight for distribution to testers. App Store submission remains a separate manual step.

## One-Time Setup in App Store Connect

### 1. Create a Test Group

1. Go to [App Store Connect](https://appstoreconnect.apple.com/) → My Apps → Decenza → TestFlight
2. Click **+** next to "Internal Testing" or "External Testing"
   - **Internal**: Up to 100 Apple Developer team members, no review needed
   - **External**: Up to 10,000 testers, first build requires Apple review
3. Name the group (e.g., "Beta Testers")

### 2. Add Testers

- **Internal**: Add team members by Apple ID (they must be part of your Apple Developer account)
- **External**: Add testers by email — they'll receive an invite to install TestFlight

### 3. Enable Automatic Distribution (Optional)

In the test group settings, enable **"Automatically distribute new builds"**. This makes every CI upload immediately available to testers without manual approval in App Store Connect.

Without this, each new build must be manually enabled for distribution in the TestFlight tab.

## How CI Builds Flow to TestFlight

1. A `v*` tag is pushed (or workflow is triggered manually)
2. `ios-release.yml` builds, signs, and uploads the IPA to App Store Connect
3. App Store Connect processes the build (usually 5–15 minutes)
4. If automatic distribution is enabled, testers get a notification in TestFlight
5. Testers install/update via the TestFlight app on their iOS device

## Submitting a Stable Build to the App Store

Uploading to App Store Connect does **not** publish to the App Store. To release publicly:

1. Go to App Store Connect → My Apps → Decenza
2. Click **+** next to the version number (or create a new version)
3. In the Build section, select the build you want to release
4. Fill in release notes and any required metadata
5. Submit for App Review
6. Once approved, choose manual release or automatic release

## Troubleshooting

### Build not appearing in TestFlight
- Processing can take 5–15 minutes after upload
- Check the "Activity" tab in App Store Connect for processing status
- Ensure the build's version/build number is newer than existing builds

### Testers not receiving notifications
- Verify testers accepted the TestFlight invite
- Check that automatic distribution is enabled for the test group
- Testers must have the TestFlight app installed

### Upload fails in CI
- Verify App Store Connect API credentials are not expired (`APP_STORE_CONNECT_API_KEY_*` secrets)
- Check that the provisioning profile and certificate are still valid
- See `docs/IOS_CI_FOR_CLAUDE.md` for credential renewal steps
