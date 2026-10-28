pragma Singleton
import QtQuick
import Decenza

// The two closed lists settings search draws from. scripts/settings_search_index.py reads both
// from this file, so keep them literal. Also holds recent picks, which must outlive SettingsPage
// (and the search dialog inside it): Back destroys it.
QtObject {
    // Search item ids, most recent first. In memory only: the use is a tweak-and-check loop
    // within one session, not a history.
    property var recentIds: []
    readonly property int recentMax: 5

    function noteRecent(id) {
        recentIds = [id].concat(recentIds.filter(function(r) { return r !== id })).slice(0, recentMax)
    }

    // Platform/build conditions a SettingsCard's `availability` or an isAvailable() call in a row's
    // `visible:` may name. The same name decides whether it is shown and whether search offers it.
    readonly property var conditions: ({
        "android": Qt.platform.os === "android",
        "windows": Qt.platform.os === "windows",
        "debugWindows": Qt.platform.os === "windows"
            && SettingsPreferencesService.preferences["app.isDebugBuild"] === true,
        "simulator": SettingsPreferencesService.preferences["app.simulatorAvailable"] === true,
        "debug": SettingsPreferencesService.preferences["app.isDebugBuild"] === true
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
