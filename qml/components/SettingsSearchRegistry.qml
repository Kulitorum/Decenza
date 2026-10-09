pragma Singleton
import QtQuick
import Decenza

// The two closed lists settings search draws from. scripts/settings_search_index.py reads both
// from this file, so keep them literal.
QtObject {
    // Platform/build conditions a SettingsCard's `availability` or an isAvailable() call in a row's
    // `visible:` may name. The same name decides whether it is shown and whether search offers it.
    readonly property var conditions: ({
        "android": Qt.platform.os === "android",
        "windows": Qt.platform.os === "windows",
        "simulator": Settings.app.simulatorAvailable,
        "debug": Settings.app.isDebugBuild
    })

    // Out-of-settings destinations a `SettingsSearch.route` may name. SettingsPage dispatches each.
    readonly property var routes: ["profileSelector"]

    function isAvailable(condition) {
        if (!condition)
            return true
        if (!(condition in conditions)) {
            WebDebugLogger.warn("App", "SettingsSearchRegistry", ["Unknown availability condition '" + condition + "'"].map(String).join(" "))
            return false
        }
        return conditions[condition] === true
    }
}
