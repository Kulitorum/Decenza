pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Effects

Item {
    id: root

    required property SharedBottomBarStyle barStyle
    required property SharedButtonInteraction interaction

    property string title: ""
    property color barColor: barStyle.defaultBarColor
    property bool showBackButton: true
    property string rightText: ""
    property string backAccessibleName: "Back"
    property color resolvedContentColor: barStyle.iconColor
    default property alias content: contentRow.data
    property alias leftContent: leftContentRow.data

    readonly property color contentColor: resolvedContentColor
    // Keep a nonzero ellipsis target when the bar is crowded. Layout children
    // otherwise remain Fixed and can push the actions off-screen (Qt source:
    // qtdeclarative/src/quicklayouts/qquicklayout.cpp:1304-1332).
    readonly property real leftContentMaxWidth: Math.max(barStyle.scaled(48),
        width - titleRow.implicitWidth - contentRow.implicitWidth
              - (rightTextLabel.visible ? Math.min(rightTextLabel.implicitWidth, width * 0.4) : 0)
              - barStyle.chartMarginSmall - barStyle.spacingLarge
              - barStyle.spacingMedium * (rightTextLabel.visible ? 4 : 3))
    property alias color: bgRect.color

    signal backClicked()

    anchors.left: parent.left
    anchors.right: parent.right
    anchors.bottom: parent.bottom
    height: barStyle.barHeight

    Rectangle {
        id: bgRect
        anchors.fill: parent
        color: root.barStyle.glassChrome ? root.barStyle.glassBarColor : root.barColor
        // Keep the alpha pass so wallpaper can show through; see
        // docs/CLAUDE_MD/QML_GOTCHAS.md "Translucent element renders opaque".
        opacity: root.barStyle.glassChrome ? 0.99 : 1.0
    }

    Rectangle {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 1
        color: root.barStyle.borderColor
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: root.barStyle.chartMarginSmall
        anchors.rightMargin: root.barStyle.spacingLarge
        spacing: root.barStyle.spacingMedium

        RowLayout {
            id: titleRow
            spacing: 0

            Item {
                id: backButton
                visible: root.showBackButton
                Layout.preferredWidth: root.barStyle.barHeight
                Layout.preferredHeight: root.barStyle.barHeight
                activeFocusOnTab: true
                Accessible.ignored: true

                Rectangle {
                    anchors.fill: parent
                    anchors.margins: -root.barStyle.focusMargin
                    visible: backButton.activeFocus
                    color: "transparent"
                    border.width: root.barStyle.focusBorderWidth
                    border.color: root.barStyle.focusColor
                    radius: root.barStyle.focusRadius
                }

                Image {
                    id: backImage
                    anchors.centerIn: parent
                    source: "qrc:/icons/back.svg"
                    sourceSize.width: root.barStyle.backIconSize
                    sourceSize.height: root.barStyle.backIconSize
                    width: root.barStyle.backIconSize
                    height: root.barStyle.backIconSize
                    Accessible.ignored: true
                    layer.enabled: true
                    layer.smooth: true
                    layer.effect: MultiEffect {
                        colorization: 1.0
                        colorizationColor: root.contentColor
                    }
                }

                Keys.onReturnPressed: root.backClicked()
                Keys.onEnterPressed: root.backClicked()
                Keys.onEscapePressed: root.backClicked()

                SharedAccessibleTapArea {
                    anchors.fill: parent
                    accessibleName: root.backAccessibleName
                    accessibleItem: backButton
                    interaction: root.interaction
                    onAccessibleClicked: root.backClicked()
                }
            }

            Text {
                visible: root.title !== ""
                text: root.title
                color: root.contentColor
                font.pixelSize: root.barStyle.titleFontSize
                font.bold: true
                Layout.maximumWidth: root.width * 0.5
                elide: Text.ElideRight
            }
        }

        RowLayout {
            id: leftContentRow
            spacing: root.barStyle.spacingMedium
        }

        Item { Layout.fillWidth: true }

        RowLayout {
            id: contentRow
            spacing: root.barStyle.spacingMedium
        }

        Text {
            id: rightTextLabel
            visible: root.rightText !== ""
            text: root.rightText
            color: root.contentColor
            font: root.barStyle.bodyFont
            elide: Text.ElideRight
            Layout.maximumWidth: root.width * 0.4
        }
    }
}
