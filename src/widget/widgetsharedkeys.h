#pragma once

// Single source of truth for the machine-status snapshot transport keys.
// The Android Java helper (MachineStatusWidget) and the iOS Swift widget
// (WidgetKeys) mirror these exact strings — keep all three in sync.
// See openspec/changes/add-machine-status-widget/snapshot-schema.md.

namespace WidgetSharedKeys {

// Bump only on an incompatible snapshot-schema change.
constexpr int kSchemaVersion = 1;

// iOS: App Group container shared between the app and the widget extension.
// Must be group.<bundle-id-prefix>; the app's default bundle id is
// io.github.kulitorum.decenza (see IOS_BUNDLE_ID in CMakeLists.txt).
constexpr const char* kIosAppGroupId = "group.io.github.kulitorum.decenza";
constexpr const char* kIosUserDefaultsKey = "machineStatusSnapshot";

// Android: SharedPreferences file + key the AppWidgetProvider reads.
constexpr const char* kAndroidPrefsName = "decenza_widget";
constexpr const char* kAndroidPrefsKey = "machine_status_snapshot";

// Desktop parity / manual testing: <AppDataLocation>/widget/machine_status.json
constexpr const char* kDesktopSubdir = "widget";
constexpr const char* kDesktopFileName = "machine_status.json";

} // namespace WidgetSharedKeys
