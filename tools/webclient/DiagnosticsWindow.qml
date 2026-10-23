pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decenza

ApplicationWindow {
    id: window
    visible: true
    width: 1100
    height: 900
    minimumWidth: 600
    minimumHeight: 500
    title: "Decenza — diagnostics"
    font: Theme.bodyFont
    color: Theme.backgroundColor

    ThemeViewport { viewportWidth: window.width; viewportHeight: window.height }

    palette.window: Theme.backgroundColor
    palette.windowText: Theme.textColor
    palette.base: Theme.insetBackgroundColor
    palette.text: Theme.textColor
    palette.button: Theme.surfaceColor
    palette.buttonText: Theme.textColor
    palette.highlight: Theme.accentColor
    palette.placeholderText: Theme.textSecondaryColor

    onFrameSwapped: rendering.trace.framePresented()

    header: ColumnLayout {
        width: parent.width
        spacing: 4
        Label {
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.topMargin: 12
            text: "Decenza diagnostics"
            font: Theme.titleFont
        }
    }

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth
        Column {
            x: 20
            width: Math.max(0, parent.availableWidth - 40)
            topPadding: 16
            bottomPadding: 16
            spacing: 20

            Label {
                width: parent.width
                text: "Shared controls and recorded graph preview"
                font.pixelSize: 22
                wrapMode: Text.WordWrap
            }
            FormProbe { width: parent.width }
            RenderingProbe { id: rendering; width: parent.width }
        }
    }
}
