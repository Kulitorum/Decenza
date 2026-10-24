pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts

// One complete equipment card for native and Wasm. Hosts supply data, resolved
// appearance/localization and action handlers; all visible controls live here.
Rectangle {
    id: root
    required property EquipmentCardStyle cardStyle
    required property SharedButtonStyle buttonStyle
    required property SharedButtonInteraction interaction
    property var pkg: ({})
    property var puckPrepLabels: []
    property bool archivedCard: false
    property Component calibrationComponent
    property string restoreText: "Restore"
    property string restoreAccessibleName: "Restore: put this equipment back in inventory"
    property bool selected: false
    property bool actionsEnabled: true
    property bool noticeVisible: false
    property string removeText: "Remove"
    property string removeAccessibleName: "Remove this equipment from inventory; history is kept"
    property string editAccessibleName: "Edit equipment details"
    property string deleteAccessibleName: "Delete equipment"
    property string deleteAccessibleDescription: "Deletes this unused equipment package entirely"
    property string selectedText: "selected"
    property string noticeText: "This package is used by bags or shots — use Remove instead"
    property alias basketTemplate: summary.basketTemplate
    property alias puckPrepTemplate: summary.puckPrepTemplate
    property alias richTextFor: summary.richTextFor
    readonly property string accessibleSummary: summary.accessibleSummary
    readonly property EquipmentSummaryView summaryItem: summary
    readonly property bool hasReferences: (pkg.shotCount ?? 0) > 0
    function showDeleteRefused() {
        noticeVisible = true
        noticeTimer.restart()
        if (interaction.accessibilityEnabled) interaction.announce(noticeText)
    }
    Timer {
        id: noticeTimer
        interval: 4000
        onTriggered: root.noticeVisible = false
    }
    signal activated()
    signal editRequested(var pkg)
    signal restoreRequested(var pkg)
    signal removeRequested(var pkg)
    signal deleteRequested(var pkg)

    opacity: archivedCard ? 0.7 : 1.0
    color: cardStyle.backgroundColor
    radius: cardStyle.cardRadius
    border.width: selected ? cardStyle.selectedBorderWidth : cardStyle.normalBorderWidth
    border.color: selected ? cardStyle.selectedBorderColor : cardStyle.borderColor
    implicitWidth: cardStyle.cardWidth
    implicitHeight: cardColumn.implicitHeight + cardStyle.outerVerticalPadding
    Accessible.ignored: true

    SharedCardTapArea {
        interaction: root.interaction
        enabled: root.actionsEnabled && !root.archivedCard
        accessibleName: root.accessibleSummary + (root.selected ? ", " + root.selectedText : "")
        accessibleItem: root
        onAccessibleClicked: root.activated()
    }

    ColumnLayout {
        id: cardColumn
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: root.cardStyle.contentPadding
        spacing: root.cardStyle.contentSpacing

        EquipmentSummaryView {
            id: summary
            objectName: "sharedEquipmentCardSummary"
            Layout.fillWidth: true
            grinderName: root.pkg.name || ""
            grinderBrand: root.pkg.grinderBrand || ""
            grinderModel: root.pkg.grinderModel || ""
            grinderBurrs: root.pkg.grinderBurrs || ""
            basketBrand: root.pkg.basketBrand || ""
            basketModel: root.pkg.basketModel || ""
            puckPrepLabels: root.puckPrepLabels
            bodyFont: root.cardStyle.bodyFont
            labelFont: root.cardStyle.labelFont
            captionFont: root.cardStyle.captionFont
            titlePixelSize: root.cardStyle.subtitleFont.pixelSize
            primaryTextColor: root.cardStyle.textColor
            secondaryTextColor: root.cardStyle.textSecondaryColor
            lineSpacing: root.cardStyle.summaryLineSpacing
        }

        Loader {
            Layout.fillWidth: true
            active: root.calibrationComponent !== null
            sourceComponent: root.calibrationComponent
        }

        Text {
            objectName: "sharedEquipmentCardNotice"
            Layout.fillWidth: true
            visible: root.noticeVisible
            text: root.noticeText
            font: root.cardStyle.captionFont
            color: root.cardStyle.warningColor
            wrapMode: Text.Wrap
            Accessible.role: Accessible.StaticText
            Accessible.name: text
        }

        Flow {
            objectName: "sharedEquipmentCardActions"
            Layout.fillWidth: true
            spacing: root.cardStyle.contentSpacing

            SharedAccessibleButton {
                objectName: "sharedEquipmentRestore"
                buttonStyle: root.buttonStyle
                interaction: root.interaction
                visible: root.archivedCard
                enabled: root.actionsEnabled
                height: root.cardStyle.actionSize
                _customFontSize: root.cardStyle.captionFont.pixelSize
                leftPadding: root.cardStyle.removeHorizontalPadding
                rightPadding: root.cardStyle.removeHorizontalPadding
                text: root.restoreText
                accessibleName: root.restoreAccessibleName
                onClicked: root.restoreRequested(root.pkg)
            }
            SharedAccessibleButton {
                objectName: "sharedEquipmentRemove"
                buttonStyle: root.buttonStyle
                interaction: root.interaction
                visible: !root.archivedCard && root.hasReferences
                enabled: root.actionsEnabled
                height: root.cardStyle.actionSize
                _customFontSize: root.cardStyle.captionFont.pixelSize
                leftPadding: root.cardStyle.removeHorizontalPadding
                rightPadding: root.cardStyle.removeHorizontalPadding
                text: root.removeText
                accessibleName: root.removeAccessibleName
                onClicked: root.removeRequested(root.pkg)
            }
            SharedIconButton {
                objectName: "sharedEquipmentEdit"
                visible: !root.archivedCard
                buttonStyle: root.buttonStyle
                interaction: root.interaction
                enabled: root.actionsEnabled
                width: root.cardStyle.actionSize
                height: root.cardStyle.actionSize
                icon.source: "qrc:/icons/edit.svg"
                accessibleName: root.editAccessibleName
                onClicked: root.editRequested(root.pkg)
            }
            SharedIconButton {
                objectName: "sharedEquipmentDelete"
                buttonStyle: root.buttonStyle
                interaction: root.interaction
                visible: !root.archivedCard && !root.hasReferences
                enabled: root.actionsEnabled
                width: root.cardStyle.actionSize
                height: root.cardStyle.actionSize
                icon.source: "qrc:/icons/trash.svg"
                accessibleName: root.deleteAccessibleName
                accessibleDescription: root.deleteAccessibleDescription
                onClicked: root.deleteRequested(root.pkg)
            }
        }
    }
}
