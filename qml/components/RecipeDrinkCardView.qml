pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Service-free layout for the native and browser recipe cards. Data formatting,
// profile resolution, image loading, and host actions are supplied by adapters.
Rectangle {
    id: card

    required property RecipeDrinkCardStyle cardStyle
    property string recipeName: ""
    property bool active: false
    property string activeLabel: ""
    property Component iconComponent: null
    property string drinkLine: ""
    property string beanLine: ""
    property bool staleWarningVisible: false
    property string staleWarningText: ""
    property bool staleActionVisible: false
    property string staleActionText: ""
    property bool profileMissing: false
    property string profileMissingText: ""
    property bool planVisible: false
    property bool vesselVisible: false
    property string vesselLine: ""
    property Component thumbnailComponent: null
    property Component planComponent: null

    // Host-supplied content such as the native card's action row stays inside
    // the shared card surface, while ordinary children remain ordinary items.
    property alias footer: footerSlot.data

    signal staleActionClicked()
    signal planClicked()

    implicitWidth: cardColumn.implicitWidth + 2 * cardStyle.cardPadding
    implicitHeight: cardColumn.implicitHeight + 2 * cardStyle.cardPadding
    radius: cardStyle.cardRadius
    color: cardStyle.cardBackgroundColor
    border.color: active ? cardStyle.accentColor : cardStyle.borderColor
    border.width: active ? 2 : 1

    ColumnLayout {
        id: cardColumn
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: card.cardStyle.cardPadding
        spacing: card.cardStyle.smallSpacing

        RowLayout {
            Layout.fillWidth: true
            spacing: card.cardStyle.scaled(10)

            Loader {
                Layout.preferredWidth: card.cardStyle.scaled(card.cardStyle.thumbnailSize)
                Layout.preferredHeight: card.cardStyle.scaled(card.cardStyle.thumbnailSize)
                Layout.alignment: Qt.AlignTop
                active: card.thumbnailComponent !== null
                sourceComponent: card.thumbnailComponent
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: card.cardStyle.scaled(2)

                RowLayout {
                    Layout.fillWidth: true
                    spacing: card.cardStyle.smallSpacing
                    Label {
                        Layout.fillWidth: true
                        text: card.recipeName
                        textFormat: Text.PlainText
                        font: card.cardStyle.titleFont
                        color: card.cardStyle.textColor
                        wrapMode: Text.WordWrap
                    }
                    Label {
                        visible: card.active
                        Layout.alignment: Qt.AlignTop
                        text: card.activeLabel
                        textFormat: Text.PlainText
                        font: card.cardStyle.captionFont
                        color: card.cardStyle.accentColor
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: card.cardStyle.scaled(6)
                    Loader {
                        Layout.alignment: Qt.AlignTop
                        Layout.topMargin: card.cardStyle.scaled(1)
                        Layout.preferredWidth: card.cardStyle.scaled(card.cardStyle.iconSize)
                        Layout.preferredHeight: card.cardStyle.scaled(card.cardStyle.iconSize)
                        active: card.iconComponent !== null
                        sourceComponent: card.iconComponent
                    }
                    Label {
                        Layout.fillWidth: true
                        text: card.drinkLine
                        textFormat: Text.PlainText
                        font: card.cardStyle.captionFont
                        color: card.cardStyle.secondaryTextColor
                        wrapMode: Text.WordWrap
                    }
                }

                Label {
                    visible: !card.staleWarningVisible && !card.staleActionVisible
                             && card.beanLine !== ""
                    Layout.fillWidth: true
                    text: card.beanLine
                    textFormat: Text.PlainText
                    font: card.cardStyle.captionFont
                    color: card.cardStyle.secondaryTextColor
                    wrapMode: Text.WordWrap
                }
                Label {
                    visible: card.staleActionVisible
                    Layout.fillWidth: true
                    text: card.staleActionText
                    textFormat: Text.PlainText
                    font: card.cardStyle.captionFont
                    color: card.cardStyle.warningColor
                    wrapMode: Text.WordWrap
                    Accessible.role: Accessible.Button
                    Accessible.name: text
                    Accessible.focusable: true
                    activeFocusOnTab: true
                    Accessible.onPressAction: card.staleActionClicked()
                    Keys.onReturnPressed: function(event) {
                        card.staleActionClicked()
                        event.accepted = true
                    }
                    Keys.onEnterPressed: function(event) {
                        card.staleActionClicked()
                        event.accepted = true
                    }
                    Keys.onSpacePressed: function(event) {
                        card.staleActionClicked()
                        event.accepted = true
                    }
                    TapHandler { onTapped: card.staleActionClicked() }
                }
                Label {
                    visible: card.staleWarningVisible && !card.staleActionVisible
                    Layout.fillWidth: true
                    text: card.staleWarningText
                    textFormat: Text.PlainText
                    font: card.cardStyle.captionFont
                    color: card.cardStyle.warningColor
                    wrapMode: Text.WordWrap
                    Accessible.role: Accessible.StaticText
                    Accessible.name: text
                }

                Label {
                    visible: card.profileMissing
                    Layout.fillWidth: true
                    text: card.profileMissingText
                    textFormat: Text.PlainText
                    font: card.cardStyle.captionFont
                    color: card.cardStyle.warningColor
                    wrapMode: Text.WordWrap
                    Accessible.role: Accessible.StaticText
                    Accessible.name: text
                }

                Loader {
                    Layout.fillWidth: true
                    active: card.planVisible && card.planComponent !== null
                    visible: card.planVisible
                    sourceComponent: card.planComponent
                }

                Label {
                    visible: card.vesselVisible
                    Layout.fillWidth: true
                    text: card.vesselLine
                    textFormat: Text.PlainText
                    font: card.cardStyle.captionFont
                    color: card.cardStyle.secondaryTextColor
                    wrapMode: Text.WordWrap
                }
            }
        }

        ColumnLayout {
            id: footerSlot
            Layout.fillWidth: true
            spacing: card.cardStyle.smallSpacing
            visible: children.length > 0
        }
    }
}
