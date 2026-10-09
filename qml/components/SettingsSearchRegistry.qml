pragma Singleton
import QtQuick
import Decenza

// The two closed lists settings search draws from. scripts/settings_search_index.py reads both
// from this file, so keep them literal.
QtObject {
    // Platform/build conditions a SettingsCard or `SettingsSearch.availability` may name. The same
    // name decides whether the card is shown and whether search offers it.
    readonly property var conditions: ({
        "android": Qt.platform.os === "android",
        "simulator": Settings.app.simulatorAvailable,
        "debug": Settings.app.isDebugBuild
    })

    // Out-of-settings destinations a `SettingsSearch.route` may name. SettingsPage dispatches each.
    readonly property var routes: ["profileSelector"]

    function isAvailable(condition) {
        return !condition || conditions[condition] === true
    }
}
