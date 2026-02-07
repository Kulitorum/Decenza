import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ".."
import "EmojiData.js" as EmojiData

Item {
    id: root

    property string selectedValue: ""

    signal selected(string value)
    signal cleared()

    implicitHeight: pickerColumn.implicitHeight

    property int _activeCategory: 0

    // Decenza SVG icons (first category)
    readonly property var decenzaCategory: ({
        name: "Decenza",
        isSvg: true,
        items: [
            { value: "qrc:/icons/espresso.svg", label: "Espresso" },
            { value: "qrc:/icons/steam.svg", label: "Steam" },
            { value: "qrc:/icons/water.svg", label: "Water" },
            { value: "qrc:/icons/flush.svg", label: "Flush" },
            { value: "qrc:/icons/coffeebeans.svg", label: "Beans" },
            { value: "qrc:/icons/sleep.svg", label: "Sleep" },
            { value: "qrc:/icons/settings.svg", label: "Settings" },
            { value: "qrc:/icons/history.svg", label: "History" },
            { value: "qrc:/icons/star.svg", label: "Star" },
            { value: "qrc:/icons/star-outline.svg", label: "Star" },
            { value: "qrc:/icons/temperature.svg", label: "Temp" },
            { value: "qrc:/icons/tea.svg", label: "Tea" },
            { value: "qrc:/icons/grind.svg", label: "Grind" },
            { value: "qrc:/icons/filter.svg", label: "Filter" },
            { value: "qrc:/icons/bluetooth.svg", label: "BT" },
            { value: "qrc:/icons/wifi.svg", label: "WiFi" },
            { value: "qrc:/icons/edit.svg", label: "Edit" },
            { value: "qrc:/icons/sparkle.svg", label: "AI" },
            { value: "qrc:/icons/hand.svg", label: "Hand" },
            { value: "qrc:/icons/tick.svg", label: "Tick" },
            { value: "qrc:/icons/cross.svg", label: "Cross" },
            { value: "qrc:/icons/decent-de1.svg", label: "DE1" },
            { value: "qrc:/icons/scale.svg", label: "Scale" },
            { value: "qrc:/icons/quit.svg", label: "Quit" },
            { value: "qrc:/icons/Graph.svg", label: "Graph" }
        ]
    })

    // Build all categories: Decenza SVGs + emoji from EmojiData.js
    readonly property var categories: {
        var result = [decenzaCategory]
        for (var i = 0; i < EmojiData.categories.length; i++) {
            result.push({
                name: EmojiData.categories[i].name,
                isSvg: false,
                items: EmojiData.categories[i].emoji
            })
        }
        return result
    }

    ColumnLayout {
        id: pickerColumn
        anchors.fill: parent
        spacing: Theme.scaled(4)

        // Category tabs (scrollable for many categories)
        Flickable {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.scaled(28)
            contentWidth: tabRow.implicitWidth
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            flickableDirection: Flickable.HorizontalFlick

            RowLayout {
                id: tabRow
                spacing: Theme.scaled(2)

                Repeater {
                    model: root.categories
                    Rectangle {
                        implicitWidth: tabLabel.implicitWidth + Theme.scaled(16)
                        height: Theme.scaled(28)
                        radius: Theme.scaled(4)
                        color: root._activeCategory === index ? Theme.primaryColor : Theme.backgroundColor
                        border.color: Theme.borderColor
                        border.width: 1

                        Text {
                            id: tabLabel
                            anchors.centerIn: parent
                            text: modelData.name
                            color: root._activeCategory === index ? "white" : Theme.textColor
                            font: Theme.captionFont
                        }

                        MouseArea {
                            anchors.fill: parent
                            onClicked: root._activeCategory = index
                        }
                    }
                }

                // Clear button
                Rectangle {
                    width: Theme.scaled(28)
                    height: Theme.scaled(28)
                    radius: Theme.scaled(4)
                    color: clearMa.pressed ? Theme.errorColor : Theme.backgroundColor
                    border.color: Theme.errorColor
                    border.width: 1

                    Text {
                        anchors.centerIn: parent
                        text: "\u00D7"
                        color: clearMa.pressed ? "white" : Theme.errorColor
                        font.pixelSize: Theme.scaled(16)
                        font.bold: true
                    }

                    MouseArea {
                        id: clearMa
                        anchors.fill: parent
                        onClicked: {
                            root.selectedValue = ""
                            root.cleared()
                        }
                    }
                }
            }
        }

        // Grid of icons/emoji
        GridView {
            id: grid
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.scaled(160)
            cellWidth: root.categories[root._activeCategory].isSvg ? Theme.scaled(52) : Theme.scaled(44)
            cellHeight: root.categories[root._activeCategory].isSvg ? Theme.scaled(52) : Theme.scaled(44)
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            model: root.categories[root._activeCategory].items

            delegate: Rectangle {
                // Handle both object items (SVG: {value, label}) and string items (emoji)
                readonly property string itemValue: typeof modelData === "object" ? modelData.value : modelData
                readonly property bool isSelected: root.selectedValue === itemValue

                width: grid.cellWidth - Theme.scaled(4)
                height: grid.cellHeight - Theme.scaled(4)
                radius: Theme.scaled(6)
                color: isSelected
                    ? Theme.primaryColor
                    : (itemMa.containsMouse ? Qt.rgba(Theme.primaryColor.r, Theme.primaryColor.g, Theme.primaryColor.b, 0.12) : "transparent")
                border.color: isSelected ? Theme.primaryColor : "transparent"
                border.width: 1

                // Emoji/icon image (both SVG icons and emoji use Image now)
                Image {
                    anchors.centerIn: parent
                    source: Theme.emojiToImage(parent.itemValue)
                    sourceSize.width: Theme.scaled(28)
                    sourceSize.height: Theme.scaled(28)
                }

                MouseArea {
                    id: itemMa
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: {
                        root.selectedValue = parent.itemValue
                        root.selected(parent.itemValue)
                    }
                }
            }
        }
    }
}
