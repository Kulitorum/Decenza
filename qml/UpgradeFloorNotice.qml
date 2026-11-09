import QtQuick
import QtQuick.Controls

// Shown instead of the app when the shot database is below the upgrade floor. main() loads
// it before Settings exists, so it uses no Decenza singleton, and its text is
// English: TranslationManager needs Settings, and constructing Settings is what
// this check exists to avoid.
ApplicationWindow {
    id: window

    required property string steppingStoneRelease

    visible: true
    width: 560
    height: 360
    title: "Decenza"

    readonly property string message: "This version of Decenza can't open your data yet. It was last used "
        + "by an older release.\n\nInstall Decenza " + steppingStoneRelease
        + " first and open it once to bring your data up to date, then install this version again. "
        + "Nothing has been changed."

    onClosing: Qt.quit()

    Column {
        anchors.centerIn: parent
        width: Math.min(parent.width - 48, 480)
        spacing: 24

        Text {
            width: parent.width
            text: window.message
            color: window.palette.windowText
            wrapMode: Text.Wrap
            font.pixelSize: 18
            Accessible.role: Accessible.StaticText
            Accessible.name: window.message
        }

        Button {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "Close Decenza"
            focus: true
            onClicked: Qt.quit()
            Accessible.role: Accessible.Button
            Accessible.name: text
            Accessible.focusable: true
            Accessible.onPressAction: Qt.quit()
            Keys.onBackPressed: Qt.quit()
        }
    }
}
