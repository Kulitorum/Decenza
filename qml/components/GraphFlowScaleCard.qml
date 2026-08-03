pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import Decenza

// Flow scale selector: 1x, 2x or 3x, applied to the flow, weight-flow and flow-goal traces.
//
// Presented on both surfaces that offer graph options — the live screen's extraction view
// selector and the shot detail / post-shot review options menu — so it lives here rather
// than in either of them. It writes Settings.graph.flowMultiplier directly; every graph
// binds to that, so there is no value to route back through the host dialog.
ColumnLayout {
    id: flowScaleCard

    Layout.fillWidth: true
    spacing: Theme.scaled(2)

    Tr { id: trTitle; key: "graph.options.flowScale"; fallback: "Flow Scale"; visible: false }
    Tr {
        id: trDesc
        key: "graph.options.flowScaleDesc"
        fallback: "Magnify flow, weight flow and the flow goal. Pressure is unchanged."
        visible: false
    }

    Text {
        text: trTitle.text
        color: Theme.textColor
        font.family: Theme.bodyFont.family
        font.pixelSize: Theme.bodyFont.pixelSize
        Accessible.ignored: true
    }

    Text {
        text: trDesc.text
        color: Theme.textSecondaryColor
        font: Theme.captionFont
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        Accessible.ignored: true
    }

    RowLayout {
        Layout.fillWidth: true
        Layout.topMargin: Theme.scaled(4)
        spacing: Theme.spacingSmall

        Repeater {
            model: [1, 2, 3]

            Rectangle {
                id: option
                required property int modelData

                readonly property bool isActive: Settings.graph.flowMultiplier === modelData
                // Reads "1x" in every locale that uses Latin digits, and stays a single
                // string for those that do not.
                readonly property string optionLabel:
                    TranslationManager.translate("graph.options.flowScaleOption", "%1x").arg(modelData)

                Layout.fillWidth: true
                Layout.preferredHeight: Theme.scaled(40)
                radius: Theme.cardRadius
                color: isActive ? Theme.primaryColor : Theme.backgroundColor
                border.color: isActive ? Theme.primaryColor : Theme.borderColor
                border.width: 1

                Accessible.ignored: true

                Text {
                    anchors.centerIn: parent
                    text: option.optionLabel
                    color: option.isActive ? Theme.primaryContrastColor : Theme.textColor
                    font.family: Theme.bodyFont.family
                    font.pixelSize: Theme.bodyFont.pixelSize
                    Accessible.ignored: true
                }

                AccessibleMouseArea {
                    anchors.fill: parent
                    accessibleName: option.optionLabel
                    accessibleItem: option
                    accessibleRole: Accessible.RadioButton
                    accessibleChecked: option.isActive
                    onAccessibleClicked: Settings.graph.flowMultiplier = option.modelData
                }
            }
        }
    }
}
