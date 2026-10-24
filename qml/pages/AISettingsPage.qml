// Both the dedicated AI page and the Settings tab use the same controls and
// host-backed service contract.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as T
import Decenza

T.Page {
    id: aiSettingsPage
    readonly property string pageTitle: TranslationManager.translate(
        "aisettings.title", "AI Dialing Assistant")
    objectName: "aiSettingsPage"
    background: ThemedPageBackground {}

    SettingsAITab {
        anchors.fill: parent
    }
}
