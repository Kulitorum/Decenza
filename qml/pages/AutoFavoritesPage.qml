import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DecenzaDE1
import "../components"

Page {
    id: autoFavoritesPage
    objectName: "autoFavoritesPage"
    background: Rectangle { color: Theme.backgroundColor }

    Component.onCompleted: {
        root.currentPageTitle = TranslationManager.translate("autofavorites.title", "Auto-Favorites")
        loadFavorites()
    }

    StackView.onActivated: {
        root.currentPageTitle = TranslationManager.translate("autofavorites.title", "Auto-Favorites")
        loadFavorites()
    }

    function loadFavorites() {
        favoritesModel.clear()
        var favorites = MainController.shotHistory.getAutoFavorites(
            Settings.autoFavoritesGroupBy,
            Settings.autoFavoritesMaxItems
        )
        for (var i = 0; i < favorites.length; i++) {
            favoritesModel.append(favorites[i])
        }
    }

    // Refresh when a new shot is saved
    Connections {
        target: MainController.shotHistory
        function onShotSaved(shotId) {
            if (autoFavoritesPage.visible) {
                loadFavorites()
            }
        }
    }

    ListModel {
        id: favoritesModel
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.standardMargin
        anchors.topMargin: Theme.pageTopMargin
        anchors.bottomMargin: Theme.bottomBarHeight + Theme.spacingMedium
        spacing: Theme.spacingMedium

        // Header with count and settings access
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingMedium

            Text {
                text: favoritesModel.count + " " +
                      TranslationManager.translate("autofavorites.combinations", "combinations")
                font.family: Theme.labelFont.family
                font.pixelSize: Theme.labelFont.pixelSize
                color: Theme.textSecondaryColor
            }

            Item { Layout.fillWidth: true }

            // Settings button (gear icon)
            Rectangle {
                width: Theme.scaled(36)
                height: Theme.scaled(36)
                radius: Theme.scaled(18)
                color: Theme.surfaceColor

                Image {
                    anchors.centerIn: parent
                    source: "qrc:/icons/settings.svg"
                    sourceSize.width: Theme.scaled(20)
                    sourceSize.height: Theme.scaled(20)
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: settingsPopup.open()
                }
            }
        }

        // Favorites list
        ListView {
            id: favoritesListView
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: Theme.spacingSmall
            model: favoritesModel

            delegate: Rectangle {
                id: favoriteDelegate
                width: favoritesListView.width
                height: Theme.scaled(100)
                radius: Theme.cardRadius
                color: Theme.surfaceColor

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.spacingMedium
                    spacing: Theme.spacingMedium

                    // Main info
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(4)

                        // Bean info (primary line)
                        Text {
                            text: (model.beanBrand || "") +
                                  (model.beanType ? " - " + model.beanType : "")
                            font.family: Theme.subtitleFont.family
                            font.pixelSize: Theme.subtitleFont.pixelSize
                            color: Theme.textColor
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            visible: model.beanBrand || model.beanType
                        }

                        // Profile name with info button
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.scaled(6)

                            Text {
                                text: model.profileName || ""
                                font.family: Theme.labelFont.family
                                font.pixelSize: Theme.labelFont.pixelSize
                                color: Theme.primaryColor
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                            }

                            ProfileInfoButton {
                                visible: model.profileName && model.profileName.length > 0
                                buttonSize: Theme.scaled(22)
                                profileFilename: MainController.findProfileByTitle(model.profileName || "")
                                profileName: model.profileName || ""

                                onClicked: {
                                    pageStack.push(Qt.resolvedUrl("ProfileInfoPage.qml"), {
                                        profileFilename: MainController.findProfileByTitle(model.profileName || ""),
                                        profileName: model.profileName || ""
                                    })
                                }
                            }
                        }

                        // Grinder info (if included in grouping)
                        Text {
                            text: (model.grinderModel || "") +
                                  (model.grinderSetting ? " @ " + model.grinderSetting : "")
                            font.family: Theme.captionFont.family
                            font.pixelSize: Theme.captionFont.pixelSize
                            color: Theme.textSecondaryColor
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            visible: Settings.autoFavoritesGroupBy.indexOf("grinder") >= 0 &&
                                     (model.grinderModel || model.grinderSetting)
                        }

                        // Recipe summary
                        RowLayout {
                            spacing: Theme.spacingLarge

                            Text {
                                text: (model.doseWeight || 0).toFixed(1) + "g \u2192 " +
                                      (model.finalWeight || 0).toFixed(1) + "g"
                                font.family: Theme.captionFont.family
                                font.pixelSize: Theme.captionFont.pixelSize
                                color: Theme.textSecondaryColor
                            }

                            Text {
                                text: model.shotCount + " " +
                                      TranslationManager.translate("autofavorites.shots", "shots")
                                font.family: Theme.captionFont.family
                                font.pixelSize: Theme.captionFont.pixelSize
                                color: Theme.textSecondaryColor
                            }

                            Text {
                                text: model.avgEnjoyment > 0 ? model.avgEnjoyment + "%" : ""
                                font.family: Theme.captionFont.family
                                font.pixelSize: Theme.captionFont.pixelSize
                                color: Theme.warningColor
                                visible: model.avgEnjoyment > 0
                            }
                        }
                    }

                    // Load button
                    Rectangle {
                        width: Theme.scaled(70)
                        height: Theme.scaled(50)
                        radius: Theme.scaled(25)
                        color: Theme.primaryColor

                        Text {
                            anchors.centerIn: parent
                            text: TranslationManager.translate("autofavorites.load", "Load")
                            font.pixelSize: Theme.scaled(14)
                            font.bold: true
                            color: "white"
                        }

                        MouseArea {
                            anchors.fill: parent
                            onClicked: {
                                MainController.loadShotWithMetadata(model.shotId)
                                // Don't reset selectedFavoriteProfile - loadShotWithMetadata
                                // will set it correctly if the profile is a favorite
                                pageStack.pop()
                            }
                        }
                    }
                }
            }

            // Empty state
            Text {
                anchors.centerIn: parent
                text: TranslationManager.translate("autofavorites.empty",
                      "No shots yet. Make some espresso to build your favorites!")
                font.family: Theme.bodyFont.family
                font.pixelSize: Theme.bodyFont.pixelSize
                color: Theme.textSecondaryColor
                visible: favoritesModel.count === 0
                wrapMode: Text.Wrap
                width: parent.width - Theme.scaled(40)
                horizontalAlignment: Text.AlignHCenter
            }
        }
    }

    // Settings popup
    Popup {
        id: settingsPopup
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Theme.scaled(320)
        modal: true
        padding: Theme.scaled(20)

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            border.color: Theme.borderColor
            border.width: 1
        }

        contentItem: ColumnLayout {
            spacing: Theme.spacingMedium

            Text {
                text: TranslationManager.translate("autofavorites.settings", "Auto-Favorites Settings")
                font.family: Theme.subtitleFont.family
                font.pixelSize: Theme.subtitleFont.pixelSize
                color: Theme.textColor
            }

            // Group by setting
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.scaled(4)

                Text {
                    text: TranslationManager.translate("autofavorites.groupby", "Group by")
                    font.family: Theme.labelFont.family
                    font.pixelSize: Theme.labelFont.pixelSize
                    color: Theme.textSecondaryColor
                }

                StyledComboBox {
                    Layout.fillWidth: true
                    model: [
                        TranslationManager.translate("autofavorites.groupby.bean", "Bean only"),
                        TranslationManager.translate("autofavorites.groupby.profile", "Profile only"),
                        TranslationManager.translate("autofavorites.groupby.beanprofile", "Bean + Profile"),
                        TranslationManager.translate("autofavorites.groupby.all", "Bean + Profile + Grinder")
                    ]
                    currentIndex: {
                        switch(Settings.autoFavoritesGroupBy) {
                            case "bean": return 0
                            case "profile": return 1
                            case "bean_profile_grinder": return 3
                            default: return 2  // bean_profile
                        }
                    }
                    onActivated: {
                        var values = ["bean", "profile", "bean_profile", "bean_profile_grinder"]
                        Settings.autoFavoritesGroupBy = values[currentIndex]
                        loadFavorites()
                    }
                }
            }

            // Max items setting
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingMedium

                Text {
                    text: TranslationManager.translate("autofavorites.maxitems", "Max items")
                    font.family: Theme.labelFont.family
                    font.pixelSize: Theme.labelFont.pixelSize
                    color: Theme.textSecondaryColor
                    Layout.fillWidth: true
                }

                ValueInput {
                    value: Settings.autoFavoritesMaxItems
                    from: 5
                    to: 50
                    stepSize: 5
                    accessibleName: "Maximum favorites to show"
                    onValueModified: function(newValue) {
                        Settings.autoFavoritesMaxItems = newValue
                        loadFavorites()
                    }
                }
            }

            // Close button
            AccessibleButton {
                Layout.fillWidth: true
                text: TranslationManager.translate("common.close", "Close")
                accessibleName: "Close settings"
                onClicked: settingsPopup.close()
            }
        }
    }

    // Bottom bar
    BottomBar {
        title: TranslationManager.translate("autofavorites.title", "Auto-Favorites")
        onBackClicked: root.goBack()
    }
}
