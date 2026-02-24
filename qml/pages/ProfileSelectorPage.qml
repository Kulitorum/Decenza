import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DecenzaDE1
import "../components"

Page {
    id: profileSelectorPage
    objectName: "profileSelectorPage"
    background: Rectangle { color: Theme.backgroundColor }

    Component.onCompleted: root.currentPageTitle = TranslationManager.translate("profileselector.title", "Profiles")
    StackView.onActivated: root.currentPageTitle = TranslationManager.translate("profileselector.title", "Profiles")

    RowLayout {
        anchors.fill: parent
        anchors.margins: Theme.standardMargin
        anchors.topMargin: Theme.pageTopMargin
        anchors.bottomMargin: Theme.pageTopMargin
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

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(10)

                    StyledComboBox {
                        id: viewFilter
                        Layout.preferredWidth: Theme.scaled(230)
                        Layout.preferredHeight: Theme.scaled(44)
                        model: [
                            TranslationManager.translate("profileselector.filter.selected", "Selected"),
                            TranslationManager.translate("profileselector.filter.cleaning", "Cleaning/Descale"),
                            TranslationManager.translate("profileselector.filter.builtin", "Decent Built-in"),
                            TranslationManager.translate("profileselector.filter.downloaded", "Downloaded"),
                            TranslationManager.translate("profileselector.filter.user", "User Created"),
                            TranslationManager.translate("profileselector.filter.all", "All Profiles")
                        ]
                        currentIndex: 0
                        onCurrentIndexChanged: profileSearchField.text = ""

                        background: Rectangle {
                            radius: Theme.scaled(6)
                            color: Theme.surfaceColor
                            border.color: Theme.borderColor
                            border.width: 1
                        }

                        contentItem: Text {
                            text: viewFilter.displayText
                            color: Theme.textColor
                            font: Theme.bodyFont
                            verticalAlignment: Text.AlignVCenter
                            leftPadding: Theme.scaled(12)
                            rightPadding: Theme.scaled(30)
                            Accessible.ignored: true
                        }

                        indicator: Text {
                            x: viewFilter.width - width - Theme.scaled(10)
                            y: (viewFilter.height - height) / 2
                            text: "\u25BC"
                            color: Theme.textSecondaryColor
                            font.pixelSize: Theme.scaled(10)
                            Accessible.ignored: true
                        }

                        accessibleLabel: TranslationManager.translate("profileselector.filter.label", "Profile filter")
                    }

                    // Search bar for "All Profiles" view
                    StyledTextField {
                        id: profileSearchField
                        visible: viewFilter.currentIndex === 5  // Only on "All Profiles" view
                        Layout.fillWidth: true
                        Layout.preferredHeight: Theme.scaled(44)
                        placeholder: TranslationManager.translate("profileselector.search.placeholder", "Search profiles...")
                        font.pixelSize: Theme.scaled(16)
                        onTextChanged: allProfilesList.searchFilter = text.toLowerCase()
                    }

                    Item { Layout.fillWidth: true; visible: viewFilter.currentIndex !== 5 }

                    AccessibleButton {
                        visible: viewFilter.currentIndex === 1  // Cleaning/Descale view
                        text: TranslationManager.translate("profileselector.button.descaling_wizard", "Descaling Wizard")
                        accessibleName: TranslationManager.translate("profileSelector.openDescalingWizard", "Open descaling wizard to clean your machine")
                        Layout.preferredHeight: Theme.scaled(44)
                        onClicked: root.goToDescaling()
                    }

                    AccessibleButton {
                        visible: viewFilter.currentIndex !== 1 && viewFilter.currentIndex !== 5
                        text: TranslationManager.translate("profileselector.button.import_visualizer_short", "Visualizer")
                        accessibleName: TranslationManager.translate("profileSelector.importFromVisualizer", "Import profiles from Visualizer website")
                        primary: true
                        Layout.preferredHeight: Theme.scaled(44)
                        leftPadding: Theme.scaled(10)
                        rightPadding: Theme.scaled(10)
                        onClicked: root.goToVisualizerBrowser()
                    }

                    AccessibleButton {
                        visible: viewFilter.currentIndex !== 1 && viewFilter.currentIndex !== 5
                        text: Qt.platform.os === "ios" ?
                              TranslationManager.translate("profileselector.button.import_file_short", "File") :
                              TranslationManager.translate("profileselector.button.import_tablet_short", "Tablet")
                        accessibleName: Qt.platform.os === "ios" ?
                              TranslationManager.translate("profileSelector.importFromFiles", "Import a profile file from Files app") :
                              TranslationManager.translate("profileSelector.importFromTablet", "Import profiles from Decent tablet")
                        primary: true
                        Layout.preferredHeight: Theme.scaled(44)
                        leftPadding: Theme.scaled(10)
                        rightPadding: Theme.scaled(10)
                        onClicked: root.goToProfileImport()
                    }

                    AccessibleButton {
                        visible: viewFilter.currentIndex !== 1 && viewFilter.currentIndex !== 5
                        text: "+"
                        accessibleName: TranslationManager.translate("profileSelector.createNewProfile", "Create new profile")
                        primary: true
                        Layout.preferredHeight: Theme.scaled(44)
                        Layout.preferredWidth: Theme.scaled(44)
                        leftPadding: Theme.scaled(4)
                        rightPadding: Theme.scaled(4)
                        contentItem: Text {
                            text: "+"
                            font.pixelSize: Theme.scaled(22)
                            font.bold: true
                            font.family: Theme.bodyFont.family
                            color: "white"
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            Accessible.ignored: true
                        }
                        onClicked: newProfileDialog.open()
                    }
                }

                ListView {
                    id: allProfilesList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true

                    property string searchFilter: ""

                    model: {
                        var filter = searchFilter  // Create binding dependency
                        switch (viewFilter.currentIndex) {
                            case 0: return MainController.selectedProfiles      // "Selected"
                            case 1: return MainController.cleaningProfiles      // "Cleaning/Descale"
                            case 2: return MainController.allBuiltInProfiles    // "Decent Built-in"
                            case 3: return MainController.downloadedProfiles    // "Downloaded"
                            case 4: return MainController.userCreatedProfiles   // "User Created"
                            case 5: {
                                var all = MainController.allProfilesList
                                if (filter === "") return all
                                var result = []
                                for (var i = 0; i < all.length; i++) {
                                    if (all[i].title.toLowerCase().indexOf(filter) >= 0) {
                                        result.push(all[i])
                                    }
                                }
                                return result
                            }
                            default: return MainController.selectedProfiles
                        }
                    }
                    spacing: Theme.scaled(4)

                    // Category for grouping in "Decent Built-in" view
                    property string currentCategory: ""

                    // Helper to get display category from beverageType
                    function getCategoryName(beverageType) {
                        switch (beverageType) {
                            case "espresso": return TranslationManager.translate("profileselector.category.espresso", "Espresso")
                            case "tea":
                            case "tea_portafilter": return TranslationManager.translate("profileselector.category.tea", "Tea")
                            case "pourover":
                            case "filter": return TranslationManager.translate("profileselector.category.pourover", "Pour Over")
                            case "cleaning":
                            case "calibrate":
                            case "manual": return TranslationManager.translate("profileselector.category.utility", "Utility")
                            default: return TranslationManager.translate("profileselector.category.other", "Other")
                        }
                    }

                    delegate: Rectangle {
                        id: profileDelegate
                        width: allProfilesList.width
                        height: Math.max(Theme.scaled(60), profileContentRow.implicitHeight + Theme.scaled(10) * 2)
                        radius: Theme.scaled(6)

                        // ProfileSource enum: 0=BuiltIn, 1=Downloaded, 2=UserCreated
                        property int profileSource: modelData.source || 0
                        property bool isBuiltIn: profileSource === 0
                        property bool isDownloaded: profileSource === 1
                        property bool isUserCreated: profileSource === 2
                        // Use binding blocks to ensure re-evaluation when lists change
                        property bool isSelected: {
                            if (isBuiltIn) {
                                var list = Settings.selectedBuiltInProfiles  // Create dependency
                                return Settings.isSelectedBuiltInProfile(modelData.name)
                            } else {
                                var hidden = Settings.hiddenProfiles  // Create dependency
                                return !Settings.isHiddenProfile(modelData.name)
                            }
                        }
                        property bool isFavorite: {
                            var list = Settings.favoriteProfiles  // Create dependency
                            return Settings.isFavoriteProfile(modelData.name)
                        }
                        property bool isCurrentProfile: modelData.name === MainController.currentProfile

                        // Source-based colors
                        property color sourceColor: isBuiltIn ? Theme.sourceBadgeBlueColor :      // Blue for Decent
                                                    isDownloaded ? Theme.sourceBadgeGreenColor :   // Green for Downloaded
                                                    Theme.sourceBadgeOrangeColor                     // Orange for User

                        // Row background with source tint
                        color: {
                            if (isCurrentProfile) {
                                return Qt.rgba(Theme.primaryColor.r, Theme.primaryColor.g, Theme.primaryColor.b, 0.25)
                            }
                            // Subtle source color tint
                            var baseColor = index % 2 === 0 ? Theme.rowAlternateColor : Theme.rowAlternateLightColor
                            return Qt.tint(baseColor, Qt.rgba(sourceColor.r, sourceColor.g, sourceColor.b, 0.15))
                        }

                        RowLayout {
                            id: profileContentRow
                            anchors.fill: parent
                            anchors.margins: Theme.scaled(10)
                            spacing: Theme.scaled(10)

                            // Source icon: D=Decent, V=Visualizer download, U=User
                            Text {
                                Layout.preferredWidth: Theme.scaled(24)
                                Layout.alignment: Qt.AlignVCenter
                                text: profileDelegate.isBuiltIn ? "D" :
                                      profileDelegate.isDownloaded ? "V" :
                                      "U"
                                font.pixelSize: Theme.scaled(16)
                                font.bold: true
                                color: profileDelegate.sourceColor
                                horizontalAlignment: Text.AlignHCenter

                                // Accessibility
                                Accessible.role: Accessible.StaticText
                                Accessible.name: profileDelegate.isBuiltIn ? TranslationManager.translate("profileselector.accessible.decent_profile", "Decent profile") :
                                                 profileDelegate.isDownloaded ? TranslationManager.translate("profileselector.accessible.downloaded_profile", "Downloaded from Visualizer") :
                                                 TranslationManager.translate("profileselector.accessible.user_profile", "User profile")
                            }

                            // Profile name
                            Text {
                                Layout.fillWidth: true
                                Layout.alignment: Qt.AlignVCenter
                                text: modelData.title
                                color: Theme.textColor
                                font: Theme.bodyFont
                                elide: Text.ElideRight
                            }

                            // Profile info button
                            ProfileInfoButton {
                                Layout.preferredWidth: Theme.scaled(28)
                                Layout.preferredHeight: Theme.scaled(28)
                                Layout.alignment: Qt.AlignVCenter
                                profileFilename: modelData.name
                                profileName: modelData.title

                                onClicked: {
                                    pageStack.push(Qt.resolvedUrl("ProfileInfoPage.qml"), {
                                        profileFilename: modelData.name,
                                        profileName: modelData.title
                                    })
                                }
                            }

                            // === Select/Unselect toggle (add/remove from "Selected" list) ===
                            StyledIconButton {
                                id: selectToggleButton
                                visible: viewFilter.currentIndex !== 0  // All views except "Selected"
                                Layout.preferredWidth: Theme.scaled(40)
                                Layout.preferredHeight: Theme.scaled(40)
                                Layout.alignment: Qt.AlignVCenter
                                text: profileDelegate.isSelected ? "\u2605" : "\u2606"
                                active: profileDelegate.isSelected
                                accessibleName: profileDelegate.isSelected ? TranslationManager.translate("profileselector.accessible.remove_from_selected", "Remove from selected") : TranslationManager.translate("profileselector.accessible.add_to_selected", "Add to selected")

                                onClicked: {
                                    if (profileDelegate.isBuiltIn) {
                                        if (profileDelegate.isSelected) {
                                            Settings.removeSelectedBuiltInProfile(modelData.name)
                                            AccessibilityManager.announce("Removed from selected")
                                        } else {
                                            Settings.addSelectedBuiltInProfile(modelData.name)
                                            AccessibilityManager.announce("Added to selected")
                                        }
                                    } else {
                                        if (profileDelegate.isSelected) {
                                            Settings.addHiddenProfile(modelData.name)
                                            AccessibilityManager.announce("Removed from selected")
                                        } else {
                                            Settings.removeHiddenProfile(modelData.name)
                                            AccessibilityManager.announce("Added to selected")
                                        }
                                    }
                                }
                            }

                            // === "Selected" view: Favorite toggle button (hollow/filled star) ===
                            StyledIconButton {
                                id: favoriteToggleButton
                                visible: viewFilter.currentIndex === 0  // Only in "Selected" view
                                Layout.preferredWidth: Theme.scaled(40)
                                Layout.preferredHeight: Theme.scaled(40)
                                Layout.alignment: Qt.AlignVCenter
                                enabled: profileDelegate.isFavorite || Settings.favoriteProfiles.length < 50
                                text: profileDelegate.isFavorite ? "\u2605" : "\u2606"
                                active: profileDelegate.isFavorite
                                accessibleName: profileDelegate.isFavorite ? TranslationManager.translate("profileselector.accessible.remove_from_favorites", "Remove from favorites") : TranslationManager.translate("profileselector.accessible.add_to_favorites", "Add to favorites")

                                onClicked: {
                                    if (profileDelegate.isFavorite) {
                                        // Find and remove from favorites
                                        var favs = Settings.favoriteProfiles
                                        for (var i = 0; i < favs.length; i++) {
                                            if (favs[i].filename === modelData.name) {
                                                Settings.removeFavoriteProfile(i)
                                                break
                                            }
                                        }
                                    } else {
                                        Settings.addFavoriteProfile(modelData.title, modelData.name)
                                    }
                                }
                            }

                            // === Overflow menu button (edit, remove, delete) ===
                            StyledIconButton {
                                id: overflowButton
                                visible: true  // All views
                                Layout.preferredWidth: Theme.scaled(40)
                                Layout.preferredHeight: Theme.scaled(40)
                                Layout.alignment: Qt.AlignVCenter
                                text: "\u22EE"  // Vertical ellipsis ⋮
                                font.pixelSize: Theme.scaled(24)
                                inactiveColor: Theme.textColor
                                accessibleName: TranslationManager.translate("profileselector.accessible.more_options", "More options for") + " " + modelData.title

                                onClicked: {
                                    var pos = mapToItem(profileDelegate, 0, height)
                                    overflowMenu.x = pos.x
                                    overflowMenu.y = pos.y
                                    overflowMenu.open()
                                }
                            }
                        }

                        // Overflow menu (outside the RowLayout for proper positioning)
                        Menu {
                            id: overflowMenu
                            width: Theme.scaled(220)

                            background: Rectangle {
                                color: Theme.surfaceColor
                                border.color: Theme.borderColor
                                radius: Theme.scaled(6)
                            }

                            MenuItem {
                                text: "\u270E  " + TranslationManager.translate("profileselector.menu.edit", "Edit Profile")
                                onTriggered: {
                                    MainController.loadProfile(modelData.name)
                                    root.goToProfileEditor()
                                }

                                contentItem: Text {
                                    text: parent.text
                                    color: Theme.textColor
                                    font: Theme.bodyFont
                                    leftPadding: Theme.scaled(8)
                                }
                                background: Rectangle {
                                    color: parent.highlighted ? Qt.rgba(Theme.primaryColor.r, Theme.primaryColor.g, Theme.primaryColor.b, 0.2) : "transparent"
                                }

                                Accessible.role: Accessible.MenuItem
                                Accessible.name: TranslationManager.translate("profileselector.accessible.edit_profile", "Edit profile")
                            }

                            MenuSeparator {
                                visible: viewFilter.currentIndex === 0 || !profileDelegate.isBuiltIn
                                contentItem: Rectangle {
                                    implicitHeight: Theme.scaled(1)
                                    color: Theme.borderColor
                                }
                            }

                            MenuItem {
                                visible: viewFilter.currentIndex === 0  // Only on "Selected" view
                                text: "\u2212  " + TranslationManager.translate("profileselector.menu.remove_from_selected", "Remove from Selected")
                                onTriggered: {
                                    if (profileDelegate.isBuiltIn) {
                                        Settings.removeSelectedBuiltInProfile(modelData.name)
                                    } else {
                                        Settings.addHiddenProfile(modelData.name)
                                    }
                                }

                                contentItem: Text {
                                    text: parent.text
                                    color: Theme.errorColor
                                    font: Theme.bodyFont
                                    leftPadding: Theme.scaled(8)
                                }
                                background: Rectangle {
                                    color: parent.highlighted ? Qt.rgba(Theme.errorColor.r, Theme.errorColor.g, Theme.errorColor.b, 0.2) : "transparent"
                                }

                                Accessible.role: Accessible.MenuItem
                                Accessible.name: TranslationManager.translate("profileselector.accessible.remove_from_list", "Remove from selected list")
                            }

                            MenuItem {
                                visible: !profileDelegate.isBuiltIn
                                text: "\u2717  " + TranslationManager.translate("profileselector.menu.delete", "Delete Profile")
                                onTriggered: {
                                    deleteDialog.profileName = modelData.name
                                    deleteDialog.profileTitle = modelData.title
                                    deleteDialog.isFavorite = profileDelegate.isFavorite
                                    deleteDialog.open()
                                }

                                contentItem: Text {
                                    text: parent.text
                                    color: Theme.errorColor
                                    font: Theme.bodyFont
                                    leftPadding: Theme.scaled(8)
                                }
                                background: Rectangle {
                                    color: parent.highlighted ? Qt.rgba(Theme.errorColor.r, Theme.errorColor.g, Theme.errorColor.b, 0.2) : "transparent"
                                }

                                Accessible.role: Accessible.MenuItem
                                Accessible.name: TranslationManager.translate("profileselector.accessible.delete_permanently", "Delete profile permanently")
                            }
                        }

                        MouseArea {
                            anchors.fill: parent
                            z: -1
                            onClicked: {
                                if (!modelData) return
                                // Check if this is the descale wizard (special profile)
                                if (modelData.name === "descale_wizard.json" || modelData.beverageType === "descale") {
                                    root.goToDescaling()
                                    return
                                }
                                MainController.loadProfile(modelData.name)
                            }
                        }

                        Accessible.role: Accessible.ListItem
                        Accessible.name: {
                            var source = profileDelegate.isBuiltIn ? TranslationManager.translate("profileselector.accessible.source_decent", "Decent") :
                                         profileDelegate.isDownloaded ? TranslationManager.translate("profileselector.accessible.source_downloaded", "Downloaded") : TranslationManager.translate("profileselector.accessible.source_custom", "Custom")
                            var fav = profileDelegate.isFavorite ? ", " + TranslationManager.translate("profileselector.accessible.favorite", "favorite") : ""
                            var current = profileDelegate.isCurrentProfile ? ", " + TranslationManager.translate("profileselector.accessible.currently_selected", "currently selected") : ""
                            return source + " " + TranslationManager.translate("profileselector.accessible.profile_label", "profile:") + " " + modelData.title + fav + current
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

                    Tr {
                        key: "profileselector.favorites.title"
                        fallback: "Favorites"
                        font: Theme.subtitleFont
                        color: Theme.textColor
                    }

                    Text {
                        text: "(" + Settings.favoriteProfiles.length + ")"
                        font: Theme.subtitleFont
                        color: Theme.textColor
                    }

                    Item { Layout.fillWidth: true }

                    Tr {
                        visible: Settings.favoriteProfiles.length > 1
                        key: "profileselector.favorites.drag_hint"
                        fallback: "Drag to reorder"
                        font: Theme.captionFont
                        color: Theme.textSecondaryColor
                    }
                }

                // Empty state
                Tr {
                    visible: Settings.favoriteProfiles.length === 0 && Settings.selectedFavoriteProfile !== -1
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    key: "profileselector.favorites.empty"
                    fallback: "No favorites yet.\nUse the \u22EE menu on a profile\nto add it to favorites."
                    color: Theme.textSecondaryColor
                    font: Theme.bodyFont
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    wrapMode: Text.Wrap
                }

                // Non-favorite profile loaded from history (green pill)
                Rectangle {
                    id: nonFavoritePill
                    Layout.fillWidth: true
                    Layout.preferredHeight: Theme.scaled(60)
                    visible: Settings.selectedFavoriteProfile === -1
                    radius: Theme.scaled(8)
                    color: Theme.successColor
                    border.color: Theme.successColor
                    border.width: 2

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: Theme.scaled(10)
                        spacing: Theme.scaled(8)

                        // Profile name
                        Text {
                            Layout.fillWidth: true
                            text: MainController.currentProfileName || "Loaded Profile"
                            color: "white"
                            font.family: Theme.bodyFont.family
                            font.pixelSize: Theme.bodyFont.pixelSize
                            font.bold: true
                            elide: Text.ElideRight
                        }

                        // Edit button - opens in profile editor
                        StyledIconButton {
                            Layout.preferredWidth: Theme.scaled(36)
                            Layout.preferredHeight: Theme.scaled(36)
                            icon.source: "qrc:/icons/edit.svg"
                            icon.width: Theme.scaled(18)
                            icon.height: Theme.scaled(18)
                            icon.color: "white"
                            accessibleName: TranslationManager.translate("profileselector.accessible.edit_profile", "Edit profile")

                            onClicked: {
                                // Profile is already loaded, just open editor
                                root.goToProfileEditor()
                            }
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        z: -1
                        onClicked: {
                            // Already selected, open editor
                            root.goToProfileEditor()
                        }
                    }
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
                        height: Math.max(Theme.scaled(60), favContentRow.implicitHeight + Theme.scaled(10) * 2)

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
                                id: favContentRow
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
                                        preventStealing: true
                                        drag.target: favoritePill
                                        drag.axis: Drag.YAxis

                                        property int startIndex: -1

                                        onPressed: {
                                            startIndex = favoriteDelegate.favoriteIndex
                                            favoritePill.anchors.fill = undefined
                                        }

                                        onReleased: {
                                            // Read position BEFORE restoring anchors (anchors.fill resets y to 0)
                                            var contentPos = favoriteDelegate.y + favoritePill.y
                                            var newIndex = Math.floor((contentPos + favoritePill.height/2) / (favoriteDelegate.height + favoritesList.spacing))
                                            newIndex = Math.max(0, Math.min(newIndex, Settings.favoriteProfiles.length - 1))
                                            favoritePill.anchors.fill = favoriteDelegate
                                            if (newIndex !== startIndex && startIndex >= 0) {
                                                Settings.moveFavoriteProfile(startIndex, newIndex)
                                            }
                                        }

                                        onCanceled: {
                                            favoritePill.anchors.fill = favoriteDelegate
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
                                StyledIconButton {
                                    id: editFavoriteButton
                                    Layout.preferredWidth: Theme.scaled(36)
                                    Layout.preferredHeight: Theme.scaled(36)
                                    icon.source: "qrc:/icons/edit.svg"
                                    icon.width: Theme.scaled(18)
                                    icon.height: Theme.scaled(18)
                                    icon.color: index === Settings.selectedFavoriteProfile ? "white" : Theme.textColor
                                    accessibleName: modelData ? (TranslationManager.translate("profileselector.accessible.edit", "Edit") + " " + root.cleanForSpeech(modelData.name)) : ""

                                    onClicked: {
                                        if (!modelData) return
                                        Settings.selectedFavoriteProfile = index
                                        MainController.loadProfile(modelData.filename)
                                        root.goToProfileEditor()
                                    }
                                }

                                // Remove button
                                StyledIconButton {
                                    id: removeFavoriteButton
                                    Layout.preferredWidth: Theme.scaled(36)
                                    Layout.preferredHeight: Theme.scaled(36)
                                    text: "\u00D7"  // × multiplication sign
                                    font.pixelSize: Theme.scaled(20)
                                    inactiveColor: Theme.errorColor
                                    accessibleName: modelData ? (TranslationManager.translate("profileselector.accessible.remove", "Remove") + " " + root.cleanForSpeech(modelData.name) + " " + TranslationManager.translate("profileselector.accessible.from_favorites", "from favorites")) : ""

                                    onClicked: {
                                        if (!modelData) return
                                        var name = root.cleanForSpeech(modelData.name)
                                        Settings.removeFavoriteProfile(index)
                                        if (typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled) {
                                            AccessibilityManager.announce(name + " " + TranslationManager.translate("profileselector.accessible.removed_from_favorites", "removed from favorites"))
                                        }
                                    }
                                }
                            }

                            // Using TapHandler for better touch responsiveness
                            AccessibleTapHandler {
                                anchors.fill: parent
                                z: -1
                                accessibleName: modelData ? (root.cleanForSpeech(modelData.name) + (index === Settings.selectedFavoriteProfile ? ", " + TranslationManager.translate("profileselector.accessible.selected_favorite", "selected favorite") : ", " + TranslationManager.translate("profileselector.accessible.favorite", "favorite"))) : ""
                                accessibleItem: favoritePill
                                onAccessibleClicked: {
                                    if (!modelData) return
                                    // Always load the profile when clicking
                                    MainController.loadProfile(modelData.filename)
                                    if (index === Settings.selectedFavoriteProfile) {
                                        // Already selected - open editor
                                        root.goToProfileEditor()
                                    } else {
                                        // Select it (first click)
                                        if (typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled) {
                                            AccessibilityManager.announce(root.cleanForSpeech(modelData.name) + " " + TranslationManager.translate("profileSelector.selected", "selected"))
                                        }
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

    // Delete confirmation dialog
    Dialog {
        id: deleteDialog
        anchors.centerIn: parent
        width: Theme.scaled(350)
        padding: 0
        modal: true

        property string profileName: ""
        property string profileTitle: ""
        property bool isFavorite: false

        header: Item {
            implicitHeight: Theme.scaled(50)

            Text {
                anchors.left: parent.left
                anchors.leftMargin: Theme.scaled(20)
                anchors.verticalCenter: parent.verticalCenter
                text: TranslationManager.translate("profileselector.dialog.delete_title", "Delete Profile")
                font: Theme.titleFont
                color: Theme.textColor
            }

            Rectangle {
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                height: 1
                color: Theme.borderColor
            }
        }

        contentItem: ColumnLayout {
            spacing: Theme.scaled(15)

            Text {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.scaled(20)
                Layout.rightMargin: Theme.scaled(20)
                Layout.topMargin: Theme.scaled(15)
                text: deleteDialog.isFavorite ?
                      "\"" + deleteDialog.profileTitle + "\" " + TranslationManager.translate("profileselector.dialog.delete_favorite_msg", "is in your favorites.\n\nDeleting will also remove it from favorites.\n\nAre you sure you want to delete this profile?") :
                      TranslationManager.translate("profileselector.dialog.delete_confirm_prefix", "Are you sure you want to delete") + " \"" + deleteDialog.profileTitle + "\"?\n\n" + TranslationManager.translate("profileselector.dialog.delete_confirm_suffix", "This cannot be undone.")
                color: Theme.textColor
                font: Theme.bodyFont
                wrapMode: Text.WordWrap
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.scaled(20)
                Layout.rightMargin: Theme.scaled(20)
                Layout.bottomMargin: Theme.scaled(15)
                spacing: Theme.scaled(10)

                AccessibleButton {
                    Layout.fillWidth: true
                    text: TranslationManager.translate("profileselector.button.cancel", "Cancel")
                    accessibleName: TranslationManager.translate("profileSelector.cancelDeletion", "Cancel deletion and keep profile")
                    onClicked: deleteDialog.close()
                }

                AccessibleButton {
                    Layout.fillWidth: true
                    text: TranslationManager.translate("profileselector.button.delete", "Delete")
                    accessibleName: TranslationManager.translate("profileSelector.permanentlyDeleteProfile", "Permanently delete this profile")
                    destructive: true
                    onClicked: {
                        MainController.deleteProfile(deleteDialog.profileName)
                        deleteDialog.close()
                    }
                }
            }
        }

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.scaled(8)
            border.color: Theme.borderColor
        }
    }

    // New profile type picker dialog
    Dialog {
        id: newProfileDialog
        anchors.centerIn: parent
        width: Theme.scaled(350)
        padding: 0
        modal: true

        header: Item {
            implicitHeight: Theme.scaled(50)

            Text {
                anchors.left: parent.left
                anchors.leftMargin: Theme.scaled(20)
                anchors.verticalCenter: parent.verticalCenter
                text: TranslationManager.translate("profileselector.newProfile.title", "New Profile")
                font: Theme.titleFont
                color: Theme.textColor
            }

            Rectangle {
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                height: 1
                color: Theme.borderColor
            }
        }

        contentItem: ColumnLayout {
            spacing: Theme.scaled(8)

            Repeater {
                model: [
                    { label: TranslationManager.translate("profileselector.newProfile.pressure", "Pressure Profile"), type: "pressure" },
                    { label: TranslationManager.translate("profileselector.newProfile.flow", "Flow Profile"), type: "flow" },
                    { label: TranslationManager.translate("profileselector.newProfile.dflow", "D-Flow"), type: "dflow" },
                    { label: TranslationManager.translate("profileselector.newProfile.aflow", "A-Flow"), type: "aflow" },
                    { label: TranslationManager.translate("profileselector.newProfile.advanced", "Advanced"), type: "advanced" }
                ]

                delegate: Rectangle {
                    Layout.fillWidth: true
                    Layout.leftMargin: Theme.scaled(12)
                    Layout.rightMargin: Theme.scaled(12)
                    Layout.preferredHeight: Theme.scaled(48)
                    radius: Theme.scaled(6)
                    color: typeMouseArea.containsMouse ? Qt.rgba(Theme.primaryColor.r, Theme.primaryColor.g, Theme.primaryColor.b, 0.2) : Theme.backgroundColor
                    Accessible.role: Accessible.Button
                    Accessible.name: modelData.label
                    Accessible.focusable: true
                    Accessible.onPressAction: typeMouseArea.clicked(null)

                    Text {
                        anchors.fill: parent
                        anchors.leftMargin: Theme.scaled(16)
                        text: modelData.label
                        color: Theme.textColor
                        font: Theme.bodyFont
                        verticalAlignment: Text.AlignVCenter
                        Accessible.ignored: true
                    }

                    MouseArea {
                        id: typeMouseArea
                        anchors.fill: parent
                        hoverEnabled: true
                        Accessible.ignored: true
                        onClicked: {
                            newProfileDialog.close()
                            var profileType = modelData.type
                            if (profileType === "pressure") {
                                MainController.createNewPressureProfile("New Pressure Profile")
                                root.goToProfileEditor()
                            } else if (profileType === "flow") {
                                MainController.createNewFlowProfile("New Flow Profile")
                                root.goToProfileEditor()
                            } else if (profileType === "dflow") {
                                MainController.createNewRecipe("D-Flow / New Recipe")
                                root.goToProfileEditor()
                            } else if (profileType === "aflow") {
                                MainController.createNewAFlowRecipe("A-Flow / New Recipe")
                                root.goToProfileEditor()
                            } else {
                                MainController.createNewProfile("New Profile")
                                root.goToProfileEditor()
                            }
                        }
                    }
                }
            }

            Item { Layout.preferredHeight: Theme.scaled(4) }
        }

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.scaled(8)
            border.color: Theme.borderColor
        }
    }

    // Bottom bar
    BottomBar {
        title: TranslationManager.translate("profileselector.title", "Profiles")
        rightText: TranslationManager.translate("profileselector.current_prefix", "Current:") + " " + MainController.currentProfileName
        onBackClicked: root.goBack()
    }
}
