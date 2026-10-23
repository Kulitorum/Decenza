pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Effects

// Shared action chrome for the shared autocomplete field. Host wrappers supply
// only resolved text, style tokens and the accessibility interaction bridge.
Item {
    id: root
    required property SharedSuggestionField field
    required property SharedButtonStyle buttonStyle
    required property SharedButtonInteraction interaction
    property bool accessibilityMode: false
    property string clearLabel: "Clear text"
    property string openLabel: "Open suggestions"
    readonly property int actionRowHeight: accessibilityMode
        && (field.textField.text.length > 0 || field.suggestions.length > 0)
        ? buttonStyle.scaled(48) : 0

    Row {
        // Keep actions beside the field: Qt maps EditableText to an HTML input
        // (qtbase/src/plugins/platforms/wasm/qwasmaccessibility.cpp:528-535),
        // whose nested buttons disappear from the browser accessibility tree.
        parent: root.field
        visible: !root.accessibilityMode
        x: root.field.textField.x + root.field.textField.width
           - width - root.buttonStyle.scaled(4)
        y: root.field.textField.y + (root.field.textField.height - height) / 2
        spacing: root.buttonStyle.scaled(4)

        Rectangle {
            id: clearButton
            visible: root.field.textField.text.length > 0
            width: root.buttonStyle.scaled(28)
            height: width
            radius: width / 2
            color: clearArea.isPressed ? root.buttonStyle.surfaceColor : "transparent"
            border.color: root.buttonStyle.textSecondaryColor
            border.width: 1
            Accessible.ignored: true
            Text {
                anchors.centerIn: parent
                text: "\u00D7"
                color: root.buttonStyle.textSecondaryColor
                font.pixelSize: root.buttonStyle.scaled(17)
                Accessible.ignored: true
            }
            SharedAccessibleTapArea {
                id: clearArea
                anchors.fill: parent
                interaction: root.interaction
                accessibleName: root.clearLabel
                accessibleItem: clearButton
                onAccessibleClicked: root.field.clearText()
            }
        }

        Rectangle {
            id: openButton
            width: root.buttonStyle.scaled(28)
            height: width
            radius: width / 2
            color: openArea.isPressed ? root.buttonStyle.surfaceColor : "transparent"
            Accessible.ignored: true
            Image {
                anchors.centerIn: parent
                source: "qrc:/icons/ArrowLeft.svg"
                sourceSize.width: root.buttonStyle.scaled(14)
                sourceSize.height: root.buttonStyle.scaled(14)
                rotation: root.field.popupVisible ? -90 : 90
                Accessible.ignored: true
                layer.enabled: true
                layer.smooth: true
                layer.effect: MultiEffect {
                    colorization: 1.0
                    colorizationColor: root.buttonStyle.textSecondaryColor
                }
            }
            SharedAccessibleTapArea {
                id: openArea
                anchors.fill: parent
                interaction: root.interaction
                accessibleName: root.openLabel
                accessibleItem: openButton
                onAccessibleClicked: {
                    if (root.field.popupVisible)
                        root.field.closeSuggestions()
                    else
                        root.field.openSuggestionsDialog()
                }
            }
        }
    }

    Row {
        parent: root.field
        visible: root.accessibilityMode
            && (root.field.textField.text.length > 0 || root.field.suggestions.length > 0)
        x: root.field.textField.x
        y: root.field.textField.y + root.field.textField.height + root.buttonStyle.scaled(4)
        width: root.field.textField.width
        spacing: root.buttonStyle.scaled(8)
        height: root.buttonStyle.scaled(44)

        SharedAccessibleButton {
            visible: root.field.textField.text.length > 0
            width: visible ? implicitWidth : 0
            height: root.buttonStyle.scaled(44)
            buttonStyle: root.buttonStyle
            interaction: root.interaction
            text: root.clearLabel
            accessibleName: root.clearLabel
            onClicked: {
                root.field.clearText()
                if (root.field.suggestions.length > 0)
                    root.field.openSuggestionsDialog()
            }
        }
        SharedAccessibleButton {
            visible: root.field.suggestions.length > 0
            width: visible ? implicitWidth : 0
            height: root.buttonStyle.scaled(44)
            buttonStyle: root.buttonStyle
            interaction: root.interaction
            text: root.openLabel
            accessibleName: root.openLabel
            onClicked: root.field.openSuggestionsDialog()
        }
    }
}
