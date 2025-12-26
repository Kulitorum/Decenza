import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DE1App
import "../components"

Page {
    id: profileSelectorPage
    objectName: "profileSelectorPage"
    background: Rectangle { color: Theme.backgroundColor }

    Component.onCompleted: root.currentPageTitle = "Profiles"

    RowLayout {
        anchors.fill: parent
        anchors.margins: Theme.standardMargin
        anchors.topMargin: 80
        anchors.bottomMargin: 80
        spacing: Theme.scaled(20)

        // LEFT SIDE: All available profiles
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Theme.surfaceColor
            radius: Theme.cardRadius

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: Theme.scaled(15)
                spacing: Theme.scaled(10)

                Text {
                    text: "All Profiles"
                    font: Theme.subtitleFont
                    color: Theme.textColor
                }

                ListView {
                    id: allProfilesList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: MainController.availableProfiles
                    spacing: Theme.scaled(4)

                    delegate: Rectangle {
                        width: allProfilesList.width
                        height: Theme.scaled(50)
                        radius: Theme.scaled(6)
                        color: modelData.name === MainController.currentProfile ?
                               Qt.rgba(Theme.primaryColor.r, Theme.primaryColor.g, Theme.primaryColor.b, 0.2) :
                               (index % 2 === 0 ? "#1a1a1a" : "#2a2a2a")

                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: Theme.scaled(10)
                            spacing: Theme.scaled(10)

                            Text {
                                Layout.fillWidth: true
                                text: modelData.title
                                color: Theme.textColor
                                font: Theme.bodyFont
                                elide: Text.ElideRight
                            }

                            // Add to favorites button (only if not already a favorite and under limit)
                            RoundButton {
                                visible: !Settings.isFavoriteProfile(modelData.name) && Settings.favoriteProfiles.length < 5
                                Layout.preferredWidth: Theme.scaled(40)
                                Layout.preferredHeight: Theme.scaled(40)
                                flat: true
                                text: "+"
                                font.pixelSize: Theme.scaled(24)
                                font.bold: true
                                onClicked: {
                                    Settings.addFavoriteProfile(modelData.title, modelData.name)
                                }
                                contentItem: Text {
                                    text: parent.text
                                    font: parent.font
                                    color: Theme.primaryColor
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                }
                                background: Rectangle {
                                    radius: width / 2
                                    color: "transparent"
                                    border.color: Theme.primaryColor
                                    border.width: 1
                                }
                            }

                            // Already favorite indicator
                            Text {
                                visible: Settings.isFavoriteProfile(modelData.name)
                                text: "\u2605"  // Star
                                color: Theme.primaryColor
                                font.pixelSize: Theme.scaled(20)
                            }
                        }

                        MouseArea {
                            anchors.fill: parent
                            z: -1
                            onClicked: {
                                MainController.loadProfile(modelData.name)
                            }
                        }
                    }
                }
            }
        }

        // RIGHT SIDE: Favorite profiles (max 5)
        Rectangle {
            Layout.preferredWidth: Theme.scaled(380)
            Layout.fillHeight: true
            color: Theme.surfaceColor
            radius: Theme.cardRadius

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: Theme.scaled(15)
                spacing: Theme.scaled(10)

                RowLayout {
                    Layout.fillWidth: true

                    Text {
                        text: "Favorites (" + Settings.favoriteProfiles.length + "/5)"
                        font: Theme.subtitleFont
                        color: Theme.textColor
                    }

                    Item { Layout.fillWidth: true }

                    Text {
                        visible: Settings.favoriteProfiles.length > 1
                        text: "Drag to reorder"
                        font: Theme.captionFont
                        color: Theme.textSecondaryColor
                    }
                }

                // Empty state
                Text {
                    visible: Settings.favoriteProfiles.length === 0
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    text: "No favorites yet.\nTap + on a profile to add it."
                    color: Theme.textSecondaryColor
                    font: Theme.bodyFont
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    wrapMode: Text.Wrap
                }

                ListView {
                    id: favoritesList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    visible: Settings.favoriteProfiles.length > 0
                    model: Settings.favoriteProfiles
                    spacing: Theme.scaled(8)

                    delegate: Item {
                        id: favoriteDelegate
                        width: favoritesList.width
                        height: Theme.scaled(60)

                        property int favoriteIndex: index

                        Rectangle {
                            id: favoritePill
                            anchors.fill: parent
                            radius: Theme.scaled(8)
                            color: index === Settings.selectedFavoriteProfile ?
                                   Theme.primaryColor : Theme.backgroundColor
                            border.color: Theme.textSecondaryColor
                            border.width: 1

                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: Theme.scaled(10)
                                spacing: Theme.scaled(8)

                                // Drag handle
                                Text {
                                    text: "\u2261"  // Hamburger menu icon
                                    font.pixelSize: Theme.scaled(24)
                                    color: index === Settings.selectedFavoriteProfile ?
                                           "white" : Theme.textSecondaryColor

                                    MouseArea {
                                        id: dragArea
                                        anchors.fill: parent
                                        drag.target: favoritePill
                                        drag.axis: Drag.YAxis

                                        property int startIndex: -1

                                        onPressed: {
                                            startIndex = favoriteDelegate.favoriteIndex
                                            favoritePill.anchors.fill = undefined
                                        }

                                        onReleased: {
                                            favoritePill.anchors.fill = parent
                                            // Calculate new position based on Y
                                            var newIndex = Math.floor((favoritePill.y + favoritePill.height/2) / (Theme.scaled(60) + Theme.scaled(8)))
                                            newIndex = Math.max(0, Math.min(newIndex, Settings.favoriteProfiles.length - 1))
                                            if (newIndex !== startIndex && startIndex >= 0) {
                                                Settings.moveFavoriteProfile(startIndex, newIndex)
                                            }
                                        }
                                    }
                                }

                                // Profile name
                                Text {
                                    Layout.fillWidth: true
                                    text: modelData.name
                                    color: index === Settings.selectedFavoriteProfile ?
                                           "white" : Theme.textColor
                                    font: Theme.bodyFont
                                    elide: Text.ElideRight
                                }

                                // Edit button
                                RoundButton {
                                    Layout.preferredWidth: Theme.scaled(36)
                                    Layout.preferredHeight: Theme.scaled(36)
                                    flat: true
                                    icon.source: "qrc:/icons/edit.svg"
                                    icon.width: Theme.scaled(18)
                                    icon.height: Theme.scaled(18)
                                    icon.color: index === Settings.selectedFavoriteProfile ?
                                               "white" : Theme.textColor
                                    onClicked: {
                                        Settings.selectedFavoriteProfile = index
                                        MainController.loadProfile(modelData.filename)
                                        root.goToProfileEditor()
                                    }
                                }

                                // Remove button
                                RoundButton {
                                    Layout.preferredWidth: Theme.scaled(36)
                                    Layout.preferredHeight: Theme.scaled(36)
                                    flat: true
                                    text: "\u00D7"  // × multiplication sign
                                    font.pixelSize: Theme.scaled(20)
                                    onClicked: Settings.removeFavoriteProfile(index)
                                    contentItem: Text {
                                        text: parent.text
                                        font: parent.font
                                        color: Theme.errorColor
                                        horizontalAlignment: Text.AlignHCenter
                                        verticalAlignment: Text.AlignVCenter
                                    }
                                    background: Rectangle {
                                        radius: width / 2
                                        color: "transparent"
                                    }
                                }
                            }

                            MouseArea {
                                anchors.fill: parent
                                z: -1
                                onClicked: {
                                    // Always load the profile when clicking
                                    MainController.loadProfile(modelData.filename)
                                    if (index === Settings.selectedFavoriteProfile) {
                                        // Already selected - open editor
                                        root.goToProfileEditor()
                                    } else {
                                        // Select it (first click)
                                        Settings.selectedFavoriteProfile = index
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // Bottom bar
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: Theme.scaled(70)
        color: Theme.primaryColor

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 10
            anchors.rightMargin: 20
            spacing: 15

            RoundButton {
                Layout.preferredWidth: 100
                Layout.preferredHeight: 70
                icon.source: "qrc:/icons/back.svg"
                icon.width: 28
                icon.height: 28
                flat: true
                icon.color: "white"
                onClicked: root.goBack()
            }

            Text {
                text: "Profiles"
                color: "white"
                font.pixelSize: 20
                font.bold: true
            }

            Item { Layout.fillWidth: true }

            Text {
                text: "Current: " + MainController.currentProfileName
                color: "white"
                font: Theme.bodyFont
            }
        }
    }
}
