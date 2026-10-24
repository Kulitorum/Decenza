import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtWebView
import Decenza

// The Universal Grind Setting author's own guide, live from their site with its URL
// shown above it: credit where it is due (add-grinder-ugs-calibration D8). Not built
// on Linux, where Qt WebView needs Qt WebEngine; GrindCalibrationSection opens the
// browser there instead. Qt forbids overlapping the WebView with other items
// (qml-qtwebview-webview.html), so the URL bar and buttons sit above it, never on it.
DecenzaDialog {
    id: root
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: parent ? parent.width * 0.95 : Theme.scaled(800)
    height: parent ? parent.height * 0.9 : Theme.scaled(600)
    modal: true
    closePolicy: Dialog.CloseOnEscape
    padding: 0

    property url initialUrl
    // Set by the opener to unload this dialog once it closes.
    property var closeCallback: null

    Component.onCompleted: {
        webView.url = initialUrl
        open()
    }
    onClosed: if (closeCallback) closeCallback()

    background: Rectangle {
        color: Theme.surfaceColor
        radius: Theme.cardRadius
        border.color: Theme.borderColor
        border.width: 1
    }

    contentItem: ColumnLayout {
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: Theme.spacingSmall
            spacing: Theme.spacingSmall

            Text {
                Layout.fillWidth: true
                elide: Text.ElideMiddle
                text: webView.url.toString()
                font: Theme.captionFont
                color: Theme.textSecondaryColor
                Accessible.role: Accessible.StaticText
                Accessible.name: text
            }
            AccessibleButton {
                text: TranslationManager.translate("ugsguide.openInBrowser", "Open in browser")
                accessibleName: text
                onClicked: Qt.openUrlExternally(webView.url)
            }
            AccessibleButton {
                text: TranslationManager.translate("common.button.close", "Close")
                accessibleName: text
                onClicked: root.close()
            }
        }

        WebView {
            id: webView
            Layout.fillWidth: true
            Layout.fillHeight: true
        }
    }
}
