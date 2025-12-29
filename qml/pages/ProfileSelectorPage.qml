import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DecenzaDE1
import "../components"

Page {
    id: profileSelectorPage
    objectName: "profileSelectorPage"
    background: Rectangle { color: Theme.backgroundColor }

    Component.onCompleted: root.currentPageTitle = "Profiles"
    StackView.onActivated: root.currentPageTitle = "Profiles"

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

                    ComboBox {
                        id: viewFilter
                        Layout.preferredWidth: Theme.scaled(230)
                        Layout.preferredHeight: Theme.scaled(44)
                        model: ["Selected", "Cleaning/Descale", "All Decent Profiles"]
                        currentIndex: 0

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
                        }

                        indicator: Text {
                            x: viewFilter.width - width - Theme.scaled(10)
                            y: (viewFilter.height - height) / 2
                            text: "\u25BC"
                            color: Theme.textSecondaryColor
                            font.pixelSize: Theme.scaled(10)
                        }

                        popup: Popup {
                            y: viewFilter.height + Theme.scaled(4)
                            width: viewFilter.width
                            padding: 0

                            contentItem: ListView {
                                implicitHeight: contentHeight
                                model: viewFilter.popup.visible ? viewFilter.delegateModel : null
                                clip: true
                            }

                            background: Rectangle {
                                color: Theme.surfaceColor
                                border.color: Theme.borderColor
                                border.width: 1
                                radius: Theme.scaled(6)
                            }
                        }

                        delegate: Rectangle {
                            width: viewFilter.width
                            height: Theme.scaled(44)
                            color: viewFilter.highlightedIndex === index ? Qt.rgba(Theme.primaryColor.r, Theme.primaryColor.g, Theme.primaryColor.b, 0.3) : "transparent"

                            Text {
                                anchors.fill: parent
                                anchors.leftMargin: Theme.scaled(12)
                                text: modelData
                                color: Theme.textColor
                                font: Theme.bodyFont
                                verticalAlignment: Text.AlignVCenter
                            }

                            MouseArea {
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: {
                                    viewFilter.currentIndex = index
                                    viewFilter.popup.close()
                                }
                            }
                        }
                    }

                    Item { Layout.fillWidth: true }

                    Button {
                        visible: viewFilter.currentIndex === 1  // Cleaning/Descale view
                        text: "Descaling Wizard"
                        Layout.preferredHeight: Theme.scaled(36)
                        onClicked: root.goToDescaling()

                        background: Rectangle {
                            radius: Theme.scaled(4)
                            color: Theme.surfaceColor
                            border.color: Theme.primaryColor
                            border.width: 1
                        }
                        contentItem: Text {
                            text: parent.text
                            color: Theme.primaryColor
                            font: Theme.captionFont
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            leftPadding: Theme.scaled(12)
                            rightPadding: Theme.scaled(12)
                        }
                    }

                    Button {
                        text: "Import from Visualizer"
                        Layout.preferredHeight: Theme.scaled(36)
                        onClicked: root.goToVisualizerBrowser()

                        background: Rectangle {
                            radius: Theme.scaled(4)
                            color: Theme.primaryColor
                        }
                        contentItem: Text {
                            text: parent.text
                            color: "white"
                            font: Theme.captionFont
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            leftPadding: Theme.scaled(12)
                            rightPadding: Theme.scaled(12)
                        }
                    }
                }

                ListView {
                    id: allProfilesList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: {
                        switch (viewFilter.currentIndex) {
                            case 0: return MainController.selectedProfiles      // "Selected"
                            case 1: return MainController.cleaningProfiles      // "Cleaning/Descale"
                            case 2: return MainController.allBuiltInProfiles    // "All Decent Profiles"
                            default: return MainController.selectedProfiles
                        }
                    }
                    spacing: Theme.scaled(4)

                    // Category for grouping in "All Decent Profiles" view
                    property string currentCategory: ""

                    // Helper to get display category from beverageType
                    function getCategoryName(beverageType) {
                        switch (beverageType) {
                            case "espresso": return "Espresso"
                            case "tea":
                            case "tea_portafilter": return "Tea"
                            case "pourover":
                            case "filter": return "Pour Over"
                            case "cleaning":
                            case "calibrate":
                            case "manual": return "Utility"
                            default: return "Other"
                        }
                    }

                    delegate: Rectangle {
                        id: profileDelegate
                        width: allProfilesList.width
                        height: Theme.scaled(60)
                        radius: Theme.scaled(6)

                        // ProfileSource enum: 0=BuiltIn, 1=Downloaded, 2=UserCreated
                        property int profileSource: modelData.source || 0
                        property bool isBuiltIn: profileSource === 0
                        property bool isDownloaded: profileSource === 1
                        property bool isUserCreated: profileSource === 2
                        // Use binding blocks to ensure re-evaluation when lists change
                        property bool isSelected: {
                            var list = Settings.selectedBuiltInProfiles  // Create dependency
                            return Settings.isSelectedBuiltInProfile(modelData.name)
                        }
                        property bool isFavorite: {
                            var list = Settings.favoriteProfiles  // Create dependency
                            return Settings.isFavoriteProfile(modelData.name)
                        }
                        property bool isCurrentProfile: modelData.name === MainController.currentProfile

                        // Source-based colors
                        property color sourceColor: isBuiltIn ? "#4a90d9" :      // Blue for Decent
                                                    isDownloaded ? "#4ad94a" :   // Green for Downloaded
                                                    "#d9a04a"                     // Orange for User

                        // Row background with source tint
                        color: {
                            if (isCurrentProfile) {
                                return Qt.rgba(Theme.primaryColor.r, Theme.primaryColor.g, Theme.primaryColor.b, 0.25)
                            }
                            // Subtle source color tint
                            var baseColor = index % 2 === 0 ? "#1a1a1a" : "#222222"
                            return Qt.tint(baseColor, Qt.rgba(sourceColor.r, sourceColor.g, sourceColor.b, 0.15))
                        }

                        RowLayout {
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
                                Accessible.name: profileDelegate.isBuiltIn ? "Decent profile" :
                                                 profileDelegate.isDownloaded ? "Downloaded from Visualizer" :
                                                 "User profile"
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

                            // === "All Decent Profiles" view: Select/Unselect toggle ===
                            RoundButton {
                                id: selectToggleButton
                                visible: viewFilter.currentIndex === 2  // Only in "All Decent Profiles"
                                Layout.preferredWidth: Theme.scaled(40)
                                Layout.preferredHeight: Theme.scaled(40)
                                Layout.alignment: Qt.AlignVCenter
                                flat: true

                                onClicked: {
                                    if (profileDelegate.isSelected) {
                                        Settings.removeSelectedBuiltInProfile(modelData.name)
                                    } else {
                                        Settings.addSelectedBuiltInProfile(modelData.name)
                                    }
                                }

                                contentItem: Text {
                                    text: profileDelegate.isSelected ? "\u2605" : "\u2606"
                                    font.pixelSize: Theme.scaled(20)
                                    color: profileDelegate.isSelected ? Theme.primaryColor : Theme.textSecondaryColor
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                }
                                background: Rectangle {
                                    radius: width / 2
                                    color: "transparent"
                                }

                                Accessible.role: Accessible.Button
                                Accessible.name: profileDelegate.isSelected ? "Remove from selected" : "Add to selected"
                            }

                            // === "Selected" view: Favorite toggle button (hollow/filled star) ===
                            RoundButton {
                                id: favoriteToggleButton
                                visible: viewFilter.currentIndex === 0  // Only in "Selected" view
                                Layout.preferredWidth: Theme.scaled(40)
                                Layout.preferredHeight: Theme.scaled(40)
                                Layout.alignment: Qt.AlignVCenter
                                flat: true
                                enabled: profileDelegate.isFavorite || Settings.favoriteProfiles.length < 50

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

                                contentItem: Text {
                                    text: profileDelegate.isFavorite ? "\u2605" : "\u2606"  // Filled or hollow star
                                    font.pixelSize: Theme.scaled(20)
                                    color: profileDelegate.isFavorite ? Theme.primaryColor : Theme.textSecondaryColor
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                }
                                background: Rectangle {
                                    radius: width / 2
                                    color: favoriteToggleButton.pressed ? Qt.rgba(1, 1, 1, 0.1) : "transparent"
                                }

                                Accessible.role: Accessible.Button
                                Accessible.name: profileDelegate.isFavorite ? "Remove from favorites" : "Add to favorites"
                            }

                            // === "Selected" view: Overflow menu button ===
                            RoundButton {
                                id: overflowButton
                                visible: viewFilter.currentIndex === 0  // Only in "Selected" view
                                Layout.preferredWidth: Theme.scaled(40)
                                Layout.preferredHeight: Theme.scaled(40)
                                Layout.alignment: Qt.AlignVCenter
                                flat: true

                                onClicked: {
                                    var pos = mapToItem(profileDelegate, 0, height)
                                    overflowMenu.x = pos.x
                                    overflowMenu.y = pos.y
                                    overflowMenu.open()
                                }

                                contentItem: Text {
                                    text: "\u22EE"  // Vertical ellipsis ⋮
                                    font.pixelSize: Theme.scaled(24)
                                    color: Theme.textColor
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                }
                                background: Rectangle {
                                    radius: width / 2
                                    color: overflowButton.pressed ? Qt.rgba(1, 1, 1, 0.1) : "transparent"
                                }

                                Accessible.role: Accessible.Button
                                Accessible.name: "More options for " + modelData.title
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
                                text: "\u270E  Edit Profile"
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
                                Accessible.name: "Edit profile"
                            }

                            MenuSeparator {
                                contentItem: Rectangle {
                                    implicitHeight: 1
                                    color: Theme.borderColor
                                }
                            }

                            MenuItem {
                                text: profileDelegate.isBuiltIn ? "\u2212  Remove from Selected" : "\u2717  Delete Profile"
                                onTriggered: {
                                    if (profileDelegate.isBuiltIn) {
                                        Settings.removeSelectedBuiltInProfile(modelData.name)
                                    } else {
                                        deleteDialog.profileName = modelData.name
                                        deleteDialog.profileTitle = modelData.title
                                        deleteDialog.isFavorite = profileDelegate.isFavorite
                                        deleteDialog.open()
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
                                Accessible.name: profileDelegate.isBuiltIn ? "Remove from selected list" : "Delete profile permanently"
                            }
                        }

                        MouseArea {
                            anchors.fill: parent
                            z: -1
                            onClicked: {
                                if (!modelData) return
                                MainController.loadProfile(modelData.name)
                            }
                        }

                        Accessible.role: Accessible.ListItem
                        Accessible.name: {
                            var source = profileDelegate.isBuiltIn ? "Decent" :
                                         profileDelegate.isDownloaded ? "Downloaded" : "Custom"
                            var fav = profileDelegate.isFavorite ? ", favorite" : ""
                            var current = profileDelegate.isCurrentProfile ? ", currently selected" : ""
                            return source + " profile: " + modelData.title + fav + current
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
                        text: "Favorites (" + Settings.favoriteProfiles.length + ")"
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
                    text: "No favorites yet.\nUse the \u22EE menu on a profile\nto add it to favorites."
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
                                    id: editFavoriteButton
                                    Layout.preferredWidth: Theme.scaled(36)
                                    Layout.preferredHeight: Theme.scaled(36)
                                    flat: true
                                    icon.source: "qrc:/icons/edit.svg"
                                    icon.width: Theme.scaled(18)
                                    icon.height: Theme.scaled(18)
                                    icon.color: index === Settings.selectedFavoriteProfile ?
                                               "white" : Theme.textColor

                                    function doEdit() {
                                        if (!modelData) return
                                        Settings.selectedFavoriteProfile = index
                                        MainController.loadProfile(modelData.filename)
                                        root.goToProfileEditor()
                                    }

                                    onClicked: doEdit()

                                    AccessibleMouseArea {
                                        anchors.fill: parent
                                        accessibleName: modelData ? ("Edit " + root.cleanForSpeech(modelData.name)) : ""
                                        accessibleItem: editFavoriteButton
                                        onAccessibleClicked: editFavoriteButton.doEdit()
                                    }
                                }

                                // Remove button
                                RoundButton {
                                    id: removeFavoriteButton
                                    Layout.preferredWidth: Theme.scaled(36)
                                    Layout.preferredHeight: Theme.scaled(36)
                                    flat: true
                                    text: "\u00D7"  // × multiplication sign
                                    font.pixelSize: Theme.scaled(20)

                                    function doRemove() {
                                        if (!modelData) return
                                        var name = root.cleanForSpeech(modelData.name)
                                        Settings.removeFavoriteProfile(index)
                                        if (typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled) {
                                            AccessibilityManager.announce(name + " removed from favorites")
                                        }
                                    }

                                    onClicked: doRemove()

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

                                    AccessibleMouseArea {
                                        anchors.fill: parent
                                        accessibleName: modelData ? ("Remove " + root.cleanForSpeech(modelData.name) + " from favorites") : ""
                                        accessibleItem: removeFavoriteButton
                                        onAccessibleClicked: removeFavoriteButton.doRemove()
                                    }
                                }
                            }

                            AccessibleMouseArea {
                                anchors.fill: parent
                                z: -1
                                accessibleName: modelData ? (root.cleanForSpeech(modelData.name) + (index === Settings.selectedFavoriteProfile ? ", selected favorite" : ", favorite")) : ""
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
                                            AccessibilityManager.announce(root.cleanForSpeech(modelData.name) + " selected")
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
        modal: true
        title: "Delete Profile"

        property string profileName: ""
        property string profileTitle: ""
        property bool isFavorite: false

        contentItem: ColumnLayout {
            spacing: Theme.scaled(15)

            Text {
                Layout.fillWidth: true
                text: deleteDialog.isFavorite ?
                      "\"" + deleteDialog.profileTitle + "\" is in your favorites.\n\nDeleting will also remove it from favorites.\n\nAre you sure you want to delete this profile?" :
                      "Are you sure you want to delete \"" + deleteDialog.profileTitle + "\"?\n\nThis cannot be undone."
                color: Theme.textColor
                font: Theme.bodyFont
                wrapMode: Text.WordWrap
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.scaled(10)

                Button {
                    Layout.fillWidth: true
                    text: "Cancel"
                    onClicked: deleteDialog.close()

                    background: Rectangle {
                        radius: Theme.scaled(4)
                        color: Theme.surfaceColor
                        border.color: Theme.borderColor
                    }
                    contentItem: Text {
                        text: parent.text
                        color: Theme.textColor
                        font: Theme.bodyFont
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }

                Button {
                    Layout.fillWidth: true
                    text: "Delete"
                    onClicked: {
                        MainController.deleteProfile(deleteDialog.profileName)
                        deleteDialog.close()
                    }

                    background: Rectangle {
                        radius: Theme.scaled(4)
                        color: Theme.errorColor
                    }
                    contentItem: Text {
                        text: parent.text
                        color: "white"
                        font: Theme.bodyFont
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
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

    // Bottom bar
    BottomBar {
        title: "Profiles"
        rightText: "Current: " + MainController.currentProfileName
        onBackClicked: root.goBack()
    }
}
