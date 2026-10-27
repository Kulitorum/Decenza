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
    readonly property bool browserHasUnsavedChanges: aiTab.browserHasUnsavedChanges
    function handleBack() { aiTab.requestLeave() }

    focus: true
    Keys.onReleased: function(event) {
        if (event.key === Qt.Key_Back || event.key === Qt.Key_Escape) {
            event.accepted = true
            aiSettingsPage.handleBack()
        }
    }

    SettingsAITab {
        id: aiTab
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: bottomBar.top
    }

    BottomBar {
        id: bottomBar
        title: aiSettingsPage.pageTitle
        onBackClicked: aiSettingsPage.handleBack()
    }
}
