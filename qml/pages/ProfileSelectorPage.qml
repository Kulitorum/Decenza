import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects
import Decenza
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

                // ===== Auto-Load status strip =====
                // Visible only when an auto-load profile is configured AND it
                // resolves to a Selected-list profile. Stale entries are
                // cleared at trigger time by ProfileManager (or eagerly by
                // hide/de-select/delete), so the visible-condition below is
                // the steady-state truth.
                Rectangle {
                    id: autoLoadStrip
                    Layout.fillWidth: true
                    Layout.preferredHeight: autoLoadStripRow.implicitHeight + Theme.scaled(16)
                    color: Theme.surfaceColor
                    border.color: Theme.borderColor
                    border.width: 1
                    radius: Theme.cardRadius

                    visible: Settings.app.autoLoadProfileFilename !== ""
                             && ProfileManager.isProfileInSelectedList(Settings.app.autoLoadProfileFilename)

                    readonly property var autoLoadProfile: visible
                        ? ProfileManager.getProfileByFilename(Settings.app.autoLoadProfileFilename)
                        : ({})
                    readonly property string autoLoadTitle: autoLoadProfile && autoLoadProfile.title
                                                            ? autoLoadProfile.title
                                                            : Settings.app.autoLoadProfileFilename

                    Accessible.role: Accessible.StaticText
                    Accessible.name: TranslationManager.translate("profileselector.strip.auto_load_label", "Auto-load:") + " " + autoLoadStrip.autoLoadTitle

                    RowLayout {
                        id: autoLoadStripRow
                        anchors.fill: parent
                        anchors.leftMargin: Theme.scaled(12)
                        anchors.rightMargin: Theme.scaled(8)
                        spacing: Theme.scaled(8)

                        Image {
                            id: autoLoadStripPin
                            source: "qrc:/icons/pin.svg"
                            sourceSize.width: Theme.scaled(16)
                            sourceSize.height: Theme.scaled(16)
                            Layout.alignment: Qt.AlignVCenter
                            Accessible.ignored: true

                            layer.enabled: true
                            layer.smooth: true
                            layer.effect: MultiEffect {
                                colorization: 1.0
                                colorizationColor: Theme.primaryColor
                            }
                        }

                        Text {
                            text: TranslationManager.translate("profileselector.strip.auto_load_label", "Auto-load:")
                            color: Theme.textSecondaryColor
                            font: Theme.bodyFont
                            Layout.alignment: Qt.AlignVCenter
                            Accessible.ignored: true
                        }

                        Text {
                            text: autoLoadStrip.autoLoadTitle
                            color: Theme.textColor
                            font: Theme.bodyFont
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignVCenter
                            Accessible.ignored: true
                        }

                        Text {
                            text: TranslationManager.translate("profileselector.strip.revert_after", "revert after")
                            color: Theme.textSecondaryColor
                            font: Theme.bodyFont
                            Layout.alignment: Qt.AlignVCenter
                            Accessible.ignored: true
                        }

                        ValueInput {
                            id: autoLoadRevertInput
                            Layout.preferredWidth: Theme.scaled(120)
                            Layout.preferredHeight: Theme.scaled(40)
                            Layout.alignment: Qt.AlignVCenter
                            value: Settings.app.autoLoadRevertMinutes
                            from: 0
                            to: 60
                            stepSize: 1
                            suffix: TranslationManager.translate("profileselector.strip.minutes_short", "min")
                            displayText: value === 0
                                ? TranslationManager.translate("profileselector.strip.never", "Never")
                                : value + " " + TranslationManager.translate("profileselector.strip.minutes_short", "min")
                            accessibleName: TranslationManager.translate("profileselector.strip.revert_after", "revert after")

                            onValueCommitted: function(newValue) {
                                Settings.app.autoLoadRevertMinutes = newValue
                            }
                        }

                        AccessibleButton {
                            id: autoLoadClearButton
                            text: "×"
                            Layout.preferredWidth: Theme.scaled(36)
                            Layout.preferredHeight: Theme.scaled(36)
                            Layout.alignment: Qt.AlignVCenter
                            accessibleName: TranslationManager.translate("profileselector.strip.clear_aria", "Disable auto-load")
                            contentItem: Text {
                                text: "×"
                                color: Theme.textColor
                                font.pixelSize: Theme.scaled(20)
                                font.bold: true
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                                Accessible.ignored: true
                            }
                            onClicked: {
                                Settings.app.autoLoadProfileFilename = ""
                                profileSelectorPage.showToast(TranslationManager.translate("profileselector.toast.auto_load_disabled", "Auto-load disabled"))
                            }
                        }
                    }
                }

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
                        // Hint the Android IME away from autocorrect (reduces surprise corrections
                        // on profile names); some IMEs ignore this, so we still filter via displayText.
                        inputMethodHints: Qt.ImhNoPredictiveText
                        // Drive filter from displayText (includes IME preedit) so suggestions
                        // update on every keystroke on Android. See SuggestionField.qml.
                        onDisplayTextChanged: allProfilesList.searchFilter = displayText.toLowerCase()
                    }

                    Item { Layout.fillWidth: true; visible: viewFilter.currentIndex !== 5 }

                    AccessibleButton {
                        visible: viewFilter.currentIndex === 1  // Cleaning/Descale view
                        text: TranslationManager.translate("profileselector.button.descaling_wizard", "Descaling Wizard")
                        accessibleName: TranslationManager.translate("profileSelector.openDescalingWizard", "Open descaling wizard to clean your machine")
                        primary: true
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
                            color: Theme.primaryContrastColor
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
                            case 0: return ProfileManager.selectedProfiles      // "Selected"
                            case 1: return ProfileManager.cleaningProfiles      // "Cleaning/Descale"
                            case 2: return ProfileManager.allBuiltInProfiles    // "Decent Built-in"
                            case 3: return ProfileManager.downloadedProfiles    // "Downloaded"
                            case 4: return ProfileManager.userCreatedProfiles   // "User Created"
                            case 5: {
                                var all = ProfileManager.allProfilesList
                                if (filter === "") return all
                                var result = []
                                for (var i = 0; i < all.length; i++) {
                                    if (all[i].title.toLowerCase().indexOf(filter) >= 0) {
                                        result.push(all[i])
                                    }
                                }
                                return result
                            }
                            default: return ProfileManager.selectedProfiles
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
                                var list = Settings.app.selectedBuiltInProfiles  // Create dependency
                                return Settings.app.isSelectedBuiltInProfile(modelData.name)
                            } else {
                                var hidden = Settings.app.hiddenProfiles  // Create dependency
                                return !Settings.app.isHiddenProfile(modelData.name)
                            }
                        }
                        property bool isFavorite: {
                            var list = Settings.app.favoriteProfiles  // Create dependency
                            return Settings.app.isFavoriteProfile(modelData.name)
                        }
                        property bool isCurrentProfile: modelData.name === ProfileManager.currentProfileName
                        readonly property bool isAutoLoad: modelData && modelData.name === Settings.app.autoLoadProfileFilename && Settings.app.autoLoadProfileFilename !== ""

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

                                // Parent Rectangle already announces source in its Accessible.name
                                Accessible.ignored: true
                            }

                            // Profile name + AI knowledge indicator
                            RowLayout {
                                Layout.fillWidth: true
                                Layout.alignment: Qt.AlignVCenter
                                spacing: Theme.scaled(4)

                                Text {
                                    Layout.fillWidth: true
                                    Layout.alignment: Qt.AlignVCenter
                                    text: {
                                        var name = modelData.title
                                        if (isCurrentProfile && ProfileManager.profileModified) {
                                            return ProfileManager.isCurrentProfileReadOnly
                                                ? name + " " + TranslationManager.translate("profileselector.modified_suffix", "(modified)") : "*" + name
                                        }
                                        return name
                                    }
                                    color: Theme.textColor
                                    font: Theme.bodyFont
                                    elide: Text.ElideRight
                                    Accessible.ignored: true
                                }

                                Image {
                                    id: autoLoadPinIcon
                                    visible: profileDelegate.isAutoLoad
                                    source: "qrc:/icons/pin.svg"
                                    sourceSize.width: Theme.scaled(14)
                                    sourceSize.height: Theme.scaled(14)
                                    Layout.alignment: Qt.AlignVCenter
                                    Accessible.role: Accessible.Indicator
                                    Accessible.name: TranslationManager.translate("profileselector.accessible.auto_load_profile", "Auto-load profile")
                                    Accessible.ignored: !visible

                                    layer.enabled: true
                                    layer.smooth: true
                                    layer.effect: MultiEffect {
                                        colorization: 1.0
                                        colorizationColor: Theme.primaryColor
                                    }
                                }

                                Image {
                                    id: sparkleIcon
                                    visible: modelData.hasKnowledgeBase === true
                                    source: "qrc:/icons/sparkle.svg"
                                    sourceSize.width: Theme.scaled(14)
                                    sourceSize.height: Theme.scaled(14)
                                    Layout.alignment: Qt.AlignVCenter
                                    opacity: sparkleMouseArea.containsMouse ? 1.0 : 0.6
                                    Accessible.ignored: true

                                    layer.enabled: true
                                    layer.smooth: true
                                    layer.effect: MultiEffect {
                                        colorization: 1.0
                                        colorizationColor: Theme.textSecondaryColor
                                    }

                                    AccessibleMouseArea {
                                        id: sparkleMouseArea
                                        anchors.fill: parent
                                        anchors.margins: Theme.scaled(-4)
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        accessibleName: TranslationManager.translate("profileselector.accessible.view_knowledge", "View AI knowledge base")
                                        accessibleItem: sparkleIcon
                                        onAccessibleClicked: {
                                            knowledgeDialog.profileTitle = modelData.title
                                            knowledgeDialog.content = ProfileManager.profileKnowledgeContent(modelData.title)
                                            knowledgeDialog.open()
                                        }
                                    }
                                }
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
                                visible: viewFilter.currentIndex !== 0  // Hidden on "Selected" view (use overflow menu to remove)
                                Layout.preferredWidth: Theme.scaled(40)
                                Layout.preferredHeight: Theme.scaled(40)
                                Layout.alignment: Qt.AlignVCenter
                                icon.source: profileDelegate.isSelected ? "qrc:/icons/box-checked.svg" : "qrc:/icons/box.svg"
                                active: profileDelegate.isSelected
                                accessibleName: profileDelegate.isSelected ? TranslationManager.translate("profileselector.accessible.remove_from_selected", "Remove from selected") : TranslationManager.translate("profileselector.accessible.add_to_selected", "Add to selected")

                                onClicked: {
                                    if (profileDelegate.isBuiltIn) {
                                        if (profileDelegate.isSelected) {
                                            Settings.app.removeSelectedBuiltInProfile(modelData.name)
                                            AccessibilityManager.announce(TranslationManager.translate("profileselector.announce.removed_from_selected", "Removed from selected"))
                                            profileSelectorPage.showToast(TranslationManager.translate("profileselector.toast.removed_from_selected", "Removed from selected"))
                                        } else {
                                            Settings.app.addSelectedBuiltInProfile(modelData.name)
                                            AccessibilityManager.announce(TranslationManager.translate("profileselector.announce.added_to_selected", "Added to selected"))
                                            profileSelectorPage.showToast(TranslationManager.translate("profileselector.toast.added_to_selected", "Added to selected"))
                                        }
                                    } else {
                                        if (profileDelegate.isSelected) {
                                            Settings.app.addHiddenProfile(modelData.name)
                                            AccessibilityManager.announce(TranslationManager.translate("profileselector.announce.removed_from_selected", "Removed from selected"))
                                            profileSelectorPage.showToast(TranslationManager.translate("profileselector.toast.removed_from_selected", "Removed from selected"))
                                        } else {
                                            Settings.app.removeHiddenProfile(modelData.name)
                                            AccessibilityManager.announce(TranslationManager.translate("profileselector.announce.added_to_selected", "Added to selected"))
                                            profileSelectorPage.showToast(TranslationManager.translate("profileselector.toast.added_to_selected", "Added to selected"))
                                        }
                                    }
                                }
                            }

                            // === Favorite toggle button (hollow/filled star) ===
                            StyledIconButton {
                                id: favoriteToggleButton
                                Layout.preferredWidth: Theme.scaled(40)
                                Layout.preferredHeight: Theme.scaled(40)
                                Layout.alignment: Qt.AlignVCenter
                                enabled: profileDelegate.isFavorite || Settings.app.favoriteProfiles.length < 50
                                icon.source: profileDelegate.isFavorite ? "qrc:/icons/star.svg" : "qrc:/icons/star-outline.svg"
                                active: profileDelegate.isFavorite
                                accessibleName: profileDelegate.isFavorite ? TranslationManager.translate("profileselector.accessible.remove_from_favorites", "Remove from favorites") : TranslationManager.translate("profileselector.accessible.add_to_favorites", "Add to favorites")

                                onClicked: {
                                    if (profileDelegate.isFavorite) {
                                        // Find and remove from favorites
                                        var favs = Settings.app.favoriteProfiles
                                        for (var i = 0; i < favs.length; i++) {
                                            if (favs[i].filename === modelData.name) {
                                                Settings.app.removeFavoriteProfile(i)
                                                break
                                            }
                                        }
                                        profileSelectorPage.showToast(TranslationManager.translate("profileselector.toast.removed_from_favorites", "Removed from favorites"))
                                    } else {
                                        Settings.app.addFavoriteProfile(modelData.title, modelData.name)
                                        profileSelectorPage.showToast(TranslationManager.translate("profileselector.toast.added_to_favorites", "Added to favorites"))
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
                                icon.source: "qrc:/icons/more-vertical.svg"
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
                                onTriggered: {
                                    ProfileManager.loadProfile(modelData.name)
                                    root.goToProfileEditor()
                                }

                                contentItem: Row {
                                    spacing: Theme.scaled(8)
                                    leftPadding: Theme.scaled(8)
                                    Image {
                                        source: "qrc:/icons/edit.svg"
                                        sourceSize.width: Theme.scaled(16)
                                        sourceSize.height: Theme.scaled(16)
                                        anchors.verticalCenter: parent.verticalCenter

                                        layer.enabled: true
                                        layer.smooth: true
                                        layer.effect: MultiEffect {
                                            colorization: 1.0
                                            colorizationColor: Theme.textColor
                                        }
                                    }
                                    Text {
                                        text: TranslationManager.translate("profileselector.menu.edit", "Edit Profile")
                                        color: Theme.textColor
                                        font: Theme.bodyFont
                                        anchors.verticalCenter: parent.verticalCenter
                                        Accessible.ignored: true
                                    }
                                }
                                background: Rectangle {
                                    color: parent.highlighted ? Qt.rgba(Theme.primaryColor.r, Theme.primaryColor.g, Theme.primaryColor.b, 0.2) : "transparent"
                                }

                                Accessible.role: Accessible.MenuItem
                                Accessible.name: TranslationManager.translate("profileselector.accessible.edit_profile", "Edit profile")
                                Accessible.focusable: true
                                Accessible.onPressAction: { ProfileManager.loadProfile(modelData.name); root.goToProfileEditor() }
                            }

                            MenuItem {
                                onTriggered: {
                                    copyProfileDialog.sourceFilename = modelData.name
                                    copyProfileDialog.sourceTitle = modelData.title
                                    copyProfileDialog.open()
                                }

                                contentItem: Row {
                                    spacing: Theme.scaled(8)
                                    leftPadding: Theme.scaled(8)
                                    Image {
                                        source: "qrc:/icons/plus.svg"
                                        sourceSize.width: Theme.scaled(16)
                                        sourceSize.height: Theme.scaled(16)
                                        anchors.verticalCenter: parent.verticalCenter

                                        layer.enabled: true
                                        layer.smooth: true
                                        layer.effect: MultiEffect {
                                            colorization: 1.0
                                            colorizationColor: Theme.textColor
                                        }
                                    }
                                    Text {
                                        text: TranslationManager.translate("profileselector.menu.copy", "Copy Profile")
                                        color: Theme.textColor
                                        font: Theme.bodyFont
                                        anchors.verticalCenter: parent.verticalCenter
                                        Accessible.ignored: true
                                    }
                                }
                                background: Rectangle {
                                    color: parent.highlighted ? Qt.rgba(Theme.primaryColor.r, Theme.primaryColor.g, Theme.primaryColor.b, 0.2) : "transparent"
                                }

                                Accessible.role: Accessible.MenuItem
                                Accessible.name: TranslationManager.translate("profileselector.accessible.copy_profile", "Copy profile")
                                Accessible.focusable: true
                                Accessible.onPressAction: { copyProfileDialog.sourceFilename = modelData.name; copyProfileDialog.sourceTitle = modelData.title; copyProfileDialog.open() }
                            }

                            // === Set / Disable Auto-Load ===
                            // Visible only when the row's profile is in the Selected list
                            // (Selected view, or row.isSelected when browsing other views).
                            MenuItem {
                                id: autoLoadMenuItem
                                visible: viewFilter.currentIndex === 0 || profileDelegate.isSelected

                                onTriggered: {
                                    if (profileDelegate.isAutoLoad) {
                                        Settings.app.autoLoadProfileFilename = ""
                                        profileSelectorPage.showToast(TranslationManager.translate("profileselector.toast.auto_load_disabled", "Auto-load disabled"))
                                    } else {
                                        Settings.app.autoLoadProfileFilename = modelData.name
                                        profileSelectorPage.showToast(
                                            TranslationManager.translate("profileselector.toast.auto_load_set", "Auto-load set to %1").arg(modelData.title))
                                    }
                                }

                                contentItem: Row {
                                    spacing: Theme.scaled(8)
                                    leftPadding: Theme.scaled(8)
                                    Image {
                                        source: "qrc:/icons/pin.svg"
                                        sourceSize.width: Theme.scaled(16)
                                        sourceSize.height: Theme.scaled(16)
                                        anchors.verticalCenter: parent.verticalCenter

                                        layer.enabled: true
                                        layer.smooth: true
                                        layer.effect: MultiEffect {
                                            colorization: 1.0
                                            colorizationColor: Theme.textColor
                                        }
                                    }
                                    Text {
                                        text: profileDelegate.isAutoLoad
                                              ? TranslationManager.translate("profileselector.menu.disable_auto_load", "Disable Auto-Load")
                                              : TranslationManager.translate("profileselector.menu.set_auto_load", "Set Auto-Load")
                                        color: Theme.textColor
                                        font: Theme.bodyFont
                                        anchors.verticalCenter: parent.verticalCenter
                                        Accessible.ignored: true
                                    }
                                }
                                background: Rectangle {
                                    color: parent.highlighted ? Qt.rgba(Theme.primaryColor.r, Theme.primaryColor.g, Theme.primaryColor.b, 0.2) : "transparent"
                                }

                                Accessible.role: Accessible.MenuItem
                                Accessible.name: profileDelegate.isAutoLoad
                                                 ? TranslationManager.translate("profileselector.accessible.disable_auto_load", "Disable auto-load")
                                                 : TranslationManager.translate("profileselector.accessible.set_auto_load", "Set as auto-load profile")
                                Accessible.focusable: true
                                Accessible.onPressAction: autoLoadMenuItem.triggered()
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
                                onTriggered: {
                                    if (profileDelegate.isBuiltIn) {
                                        Settings.app.removeSelectedBuiltInProfile(modelData.name)
                                    } else {
                                        Settings.app.addHiddenProfile(modelData.name)
                                    }
                                }

                                contentItem: Row {
                                    spacing: Theme.scaled(8)
                                    leftPadding: Theme.scaled(8)
                                    Image {
                                        source: "qrc:/icons/minus.svg"
                                        sourceSize.width: Theme.scaled(16)
                                        sourceSize.height: Theme.scaled(16)
                                        anchors.verticalCenter: parent.verticalCenter

                                        layer.enabled: true
                                        layer.smooth: true
                                        layer.effect: MultiEffect {
                                            colorization: 1.0
                                            colorizationColor: Theme.errorColor
                                        }
                                    }
                                    Text {
                                        text: TranslationManager.translate("profileselector.menu.remove_from_selected", "Remove from Selected")
                                        color: Theme.errorColor
                                        font: Theme.bodyFont
                                        anchors.verticalCenter: parent.verticalCenter
                                        Accessible.ignored: true
                                    }
                                }
                                background: Rectangle {
                                    color: parent.highlighted ? Qt.rgba(Theme.errorColor.r, Theme.errorColor.g, Theme.errorColor.b, 0.2) : "transparent"
                                }

                                Accessible.role: Accessible.MenuItem
                                Accessible.name: TranslationManager.translate("profileselector.accessible.remove_from_list", "Remove from selected list")
                                Accessible.focusable: true
                                Accessible.onPressAction: { if (profileDelegate.isBuiltIn) { Settings.app.removeSelectedBuiltInProfile(modelData.name) } else { Settings.app.addHiddenProfile(modelData.name) } }
                            }

                            MenuItem {
                                visible: !profileDelegate.isBuiltIn
                                onTriggered: {
                                    deleteDialog.profileName = modelData.name
                                    deleteDialog.profileTitle = modelData.title
                                    deleteDialog.isFavorite = profileDelegate.isFavorite
                                    deleteDialog.open()
                                }

                                contentItem: Row {
                                    spacing: Theme.scaled(8)
                                    leftPadding: Theme.scaled(8)
                                    Image {
                                        source: "qrc:/icons/trash.svg"
                                        sourceSize.width: Theme.scaled(16)
                                        sourceSize.height: Theme.scaled(16)
                                        anchors.verticalCenter: parent.verticalCenter

                                        layer.enabled: true
                                        layer.smooth: true
                                        layer.effect: MultiEffect {
                                            colorization: 1.0
                                            colorizationColor: Theme.errorColor
                                        }
                                    }
                                    Text {
                                        text: TranslationManager.translate("profileselector.menu.delete", "Delete Profile")
                                        color: Theme.errorColor
                                        font: Theme.bodyFont
                                        anchors.verticalCenter: parent.verticalCenter
                                        Accessible.ignored: true
                                    }
                                }
                                background: Rectangle {
                                    color: parent.highlighted ? Qt.rgba(Theme.errorColor.r, Theme.errorColor.g, Theme.errorColor.b, 0.2) : "transparent"
                                }

                                Accessible.role: Accessible.MenuItem
                                Accessible.name: TranslationManager.translate("profileselector.accessible.delete_permanently", "Delete profile permanently")
                                Accessible.focusable: true
                                Accessible.onPressAction: { deleteDialog.profileName = modelData.name; deleteDialog.profileTitle = modelData.title; deleteDialog.isFavorite = profileDelegate.isFavorite; deleteDialog.open() }
                            }
                        }

                        MouseArea {
                            id: profileMouseArea
                            anchors.fill: parent
                            z: -1
                            onClicked: {
                                if (!modelData) return
                                // Check if this is the descale wizard (special profile)
                                if (modelData.name === "descale_wizard.json" || modelData.beverageType === "descale") {
                                    root.goToDescaling()
                                    return
                                }
                                ProfileManager.loadProfile(modelData.name)
                            }
                        }

                        Accessible.role: Accessible.ListItem
                        Accessible.name: {
                            var source = profileDelegate.isBuiltIn ? TranslationManager.translate("profileselector.accessible.source_decent", "Decent") :
                                         profileDelegate.isDownloaded ? TranslationManager.translate("profileselector.accessible.source_downloaded", "Downloaded") : TranslationManager.translate("profileselector.accessible.source_custom", "Custom")
                            var fav = profileDelegate.isFavorite ? ", " + TranslationManager.translate("profileselector.accessible.favorite", "favorite") : ""
                            var modified = (profileDelegate.isCurrentProfile && ProfileManager.profileModified) ? ", " + TranslationManager.translate("profileselector.accessible.unsaved_changes", "unsaved changes") : ""
                            var current = profileDelegate.isCurrentProfile ? ", " + TranslationManager.translate("profileselector.accessible.currently_selected", "currently selected") : ""
                            return source + " " + TranslationManager.translate("profileselector.accessible.profile_label", "profile:") + " " + modelData.title + fav + modified + current
                        }
                        Accessible.focusable: true
                        Accessible.onPressAction: profileMouseArea.clicked(null)
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
                        text: "(" + Settings.app.favoriteProfiles.length + ")"
                        font: Theme.subtitleFont
                        color: Theme.textColor
                    }

                    Item { Layout.fillWidth: true }

                    Tr {
                        visible: Settings.app.favoriteProfiles.length > 1
                        key: "profileselector.favorites.drag_hint"
                        fallback: "Drag to reorder"
                        font: Theme.captionFont
                        color: Theme.textSecondaryColor
                    }
                }

                // Empty state
                Tr {
                    visible: Settings.app.favoriteProfiles.length === 0
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    key: "profileselector.favorites.empty"
                    fallback: "No favorites yet.\nTap the star icon on any profile\nto add it to favorites."
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
                    visible: Settings.app.selectedFavoriteProfile === -1
                    radius: Theme.scaled(8)
                    color: Theme.successColor
                    border.color: Theme.successColor
                    border.width: 2

                    Accessible.role: Accessible.Button
                    Accessible.name: (ProfileManager.currentProfileName || "Loaded Profile") + ", " + TranslationManager.translate("profileselector.accessible.edit_profile", "Edit profile")
                    Accessible.focusable: true
                    Accessible.onPressAction: nonFavPillMouseArea.clicked(null)

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: Theme.scaled(10)
                        spacing: Theme.scaled(8)

                        // Profile name
                        Text {
                            Layout.fillWidth: true
                            text: ProfileManager.currentProfileName || "Loaded Profile"
                            color: Theme.primaryContrastColor
                            font.family: Theme.bodyFont.family
                            font.pixelSize: Theme.bodyFont.pixelSize
                            font.bold: true
                            elide: Text.ElideRight
                            Accessible.ignored: true
                        }

                        // Edit button - opens in profile editor
                        StyledIconButton {
                            Layout.preferredWidth: Theme.scaled(36)
                            Layout.preferredHeight: Theme.scaled(36)
                            icon.source: "qrc:/icons/edit.svg"
                            icon.width: Theme.scaled(18)
                            icon.height: Theme.scaled(18)
                            icon.color: Theme.primaryContrastColor
                            accessibleName: TranslationManager.translate("profileselector.accessible.edit_profile", "Edit profile")

                            onClicked: {
                                // Profile is already loaded, just open editor
                                root.goToProfileEditor()
                            }
                        }
                    }

                    MouseArea {
                        id: nonFavPillMouseArea
                        anchors.fill: parent
                        z: -1
                        onClicked: {
                            // Already selected, open editor
                            root.goToProfileEditor()
                        }
                    }
                }

                FavoritesListView {
                    id: favoritesList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: Settings.app.favoriteProfiles.length > 0
                    model: Settings.app.favoriteProfiles
                    selectedIndex: Settings.app.selectedFavoriteProfile
                    rowAccessibleDescription: TranslationManager.translate(
                        "profileselector.accessible.row_hint",
                        "Double-tap or long-press to open profile editor.")

                    displayTextFn: function(row, index) {
                        if (!row) return ""
                        var name = row.name
                        if (index === Settings.app.selectedFavoriteProfile && ProfileManager.profileModified) {
                            return ProfileManager.isCurrentProfileReadOnly
                                ? name + " " + TranslationManager.translate("profileselector.modified_suffix", "(modified)") : "*" + name
                        }
                        return name
                    }
                    accessibleNameFn: function(row, index) {
                        if (!row) return ""
                        var modified = (index === Settings.app.selectedFavoriteProfile && ProfileManager.profileModified)
                            ? ", " + TranslationManager.translate("presets.unsaved", "unsaved changes") : ""
                        var status = index === Settings.app.selectedFavoriteProfile
                            ? ", " + TranslationManager.translate("profileselector.accessible.selected_favorite", "selected favorite")
                            : ", " + TranslationManager.translate("profileselector.accessible.favorite", "favorite")
                        return root.cleanForSpeech(row.name) + modified + status
                    }
                    deleteAccessibleNameFn: function(row, index) {
                        if (!row) return ""
                        return TranslationManager.translate("profileselector.accessible.remove", "Remove") + " " +
                               root.cleanForSpeech(row.name) + " " +
                               TranslationManager.translate("profileselector.accessible.from_favorites", "from favorites")
                    }

                    trailingActionDelegate: Component {
                        StyledIconButton {
                            anchors.fill: parent
                            icon.source: "qrc:/icons/edit.svg"
                            icon.width: Theme.scaled(18)
                            icon.height: Theme.scaled(18)
                            icon.color: parent.selected ? Theme.primaryContrastColor : Theme.textColor
                            accessibleName: parent.row ? (TranslationManager.translate("profileselector.accessible.edit", "Edit") + " " + root.cleanForSpeech(parent.row.name)) : ""

                            onClicked: {
                                if (!parent.row) return
                                Settings.app.selectedFavoriteProfile = parent.rowIndex
                                ProfileManager.loadProfile(parent.row.filename)
                                root.goToProfileEditor()
                            }
                        }
                    }

                    onRowLongPressed: function(index) {
                        var fav = Settings.app.favoriteProfiles[index]
                        if (!fav) return
                        Settings.app.selectedFavoriteProfile = index
                        ProfileManager.loadProfile(fav.filename)
                        root.goToProfileEditor()
                    }
                    onRowSelected: function(index) {
                        var fav = Settings.app.favoriteProfiles[index]
                        if (!fav) return
                        if (index !== Settings.app.selectedFavoriteProfile) {
                            ProfileManager.loadProfile(fav.filename)
                            Settings.app.selectedFavoriteProfile = index
                            if (typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled) {
                                AccessibilityManager.announce(root.cleanForSpeech(fav.name) + " " + TranslationManager.translate("profileSelector.selected", "selected"))
                            }
                        }
                    }
                    onRowMoved: function(from, to) { Settings.app.moveFavoriteProfile(from, to) }
                    onRowDeleted: function(index) {
                        var fav = Settings.app.favoriteProfiles[index]
                        var name = fav ? root.cleanForSpeech(fav.name) : ""
                        Settings.app.removeFavoriteProfile(index)
                        if (typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled) {
                            AccessibilityManager.announce(name + " " + TranslationManager.translate("profileselector.accessible.removed_from_favorites", "removed from favorites"))
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
                        ProfileManager.deleteProfile(deleteDialog.profileName)
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

    // Profile AI knowledge base dialog
    Dialog {
        id: knowledgeDialog
        anchors.centerIn: parent
        width: Math.min(Theme.scaled(500), parent.width - Theme.scaled(40))
        height: Math.min(knowledgeContent.implicitHeight + Theme.scaled(120), parent.height - Theme.scaled(80))
        padding: 0
        modal: true

        property string profileTitle: ""
        property string content: ""

        // Format raw KB markdown into HTML for display:
        // - strips internal metadata lines (Also matches, AnalysisFlags)
        // - bolds field labels ("Category:", "How it works:", etc.)
        // - italicizes DO NOT lines
        function formatContent(raw) {
            var lines = raw.split('\n')
            var parts = []
            for (var i = 0; i < lines.length; i++) {
                var line = lines[i]
                if (!line.trim()) continue
                if (line.startsWith('Also matches:') || line.startsWith('AnalysisFlags:')) continue

                var colonIdx = line.indexOf(': ')
                if (colonIdx > 0 && colonIdx <= 35 && !line.startsWith('DO NOT') && !line.startsWith('-')) {
                    var label = line.substring(0, colonIdx).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;')
                    var value = line.substring(colonIdx + 2).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;')
                    parts.push('<b>' + label + ':</b> ' + value)
                } else if (line.startsWith('DO NOT')) {
                    parts.push('<i>' + line.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;') + '</i>')
                } else {
                    parts.push(line.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;'))
                }
            }
            return parts.join('<br>')
        }

        header: Item {
            implicitHeight: Theme.scaled(50)

            Row {
                anchors.left: parent.left
                anchors.leftMargin: Theme.scaled(20)
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.scaled(8)

                Image {
                    source: "qrc:/icons/sparkle.svg"
                    sourceSize.width: Theme.scaled(18)
                    sourceSize.height: Theme.scaled(18)
                    anchors.verticalCenter: parent.verticalCenter

                    layer.enabled: true
                    layer.smooth: true
                    layer.effect: MultiEffect {
                        colorization: 1.0
                        colorizationColor: Theme.primaryColor
                    }
                }

                Text {
                    text: knowledgeDialog.profileTitle
                    font: Theme.titleFont
                    color: Theme.textColor
                    anchors.verticalCenter: parent.verticalCenter
                }
            }

            Rectangle {
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                height: 1
                color: Theme.borderColor
            }
        }

        contentItem: Flickable {
            clip: true
            contentHeight: knowledgeContent.implicitHeight + Theme.scaled(30)
            flickableDirection: Flickable.VerticalFlick
            boundsBehavior: Flickable.StopAtBounds

            Text {
                id: knowledgeContent
                width: parent.width - Theme.scaled(40)
                x: Theme.scaled(20)
                y: Theme.scaled(15)
                text: knowledgeDialog.formatContent(knowledgeDialog.content)
                textFormat: Text.RichText
                color: Theme.textColor
                font: Theme.bodyFont
                wrapMode: Text.WordWrap
                lineHeight: 1.5

                Accessible.role: Accessible.StaticText
                Accessible.name: TranslationManager.translate("profileselector.accessible.knowledgeContent", "Profile knowledge base")
                Accessible.description: Theme.stripMarkdown(knowledgeDialog.content)
                Accessible.focusable: true
                activeFocusOnTab: true
            }
        }

        footer: Item {
            implicitHeight: Theme.scaled(55)

            Rectangle {
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                height: 1
                color: Theme.borderColor
            }

            AccessibleButton {
                anchors.centerIn: parent
                width: Theme.scaled(100)
                text: TranslationManager.translate("common.button.ok", "OK")
                accessibleName: TranslationManager.translate("common.accessibility.dismissDialog", "Dismiss dialog")
                onClicked: knowledgeDialog.close()
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
                                ProfileManager.createNewPressureProfile("New Pressure Profile")
                                root.goToProfileEditor()
                            } else if (profileType === "flow") {
                                ProfileManager.createNewFlowProfile("New Flow Profile")
                                root.goToProfileEditor()
                            } else if (profileType === "dflow") {
                                ProfileManager.createNewRecipe("D-Flow / New Recipe")
                                root.goToProfileEditor()
                            } else if (profileType === "aflow") {
                                ProfileManager.createNewAFlowRecipe("A-Flow / New Recipe")
                                root.goToProfileEditor()
                            } else {
                                ProfileManager.createNewProfile("New Profile")
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

    // Copy profile dialog
    Dialog {
        id: copyProfileDialog
        anchors.centerIn: parent
        width: Theme.scaled(400)
        padding: 0
        modal: true

        property string sourceFilename: ""
        property string sourceTitle: ""

        onAboutToShow: {
            copyProfileNameField.text = sourceTitle + " " + TranslationManager.translate("profileselector.copy.suffix", "Copy")
            copyProfileNameField.forceActiveFocus()
            copyProfileNameField.selectAll()
        }

        header: Item {
            implicitHeight: Theme.scaled(50)

            Text {
                anchors.left: parent.left
                anchors.leftMargin: Theme.scaled(20)
                anchors.verticalCenter: parent.verticalCenter
                text: TranslationManager.translate("profileselector.copyProfile.title", "Copy Profile")
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

        contentItem: KeyboardAwareContainer {
            inOverlay: true
            textFields: [copyProfileNameField]
            implicitHeight: copyProfileColumn.implicitHeight
            implicitWidth: copyProfileColumn.implicitWidth

            ColumnLayout {
                id: copyProfileColumn
                anchors.fill: parent
                spacing: Theme.scaled(12)

                Item { implicitHeight: Theme.scaled(8) }

                Text {
                    Layout.fillWidth: true
                    Layout.leftMargin: Theme.scaled(20)
                    Layout.rightMargin: Theme.scaled(20)
                    text: TranslationManager.translate("profileselector.copyProfile.label", "Enter a name for the copy:")
                    color: Theme.textColor
                    font: Theme.bodyFont
                    wrapMode: Text.Wrap
                }

                StyledTextField {
                    id: copyProfileNameField
                    Layout.fillWidth: true
                    Layout.leftMargin: Theme.scaled(20)
                    Layout.rightMargin: Theme.scaled(20)
                    Layout.preferredHeight: Theme.scaled(44)
                    placeholder: TranslationManager.translate("profileselector.copyProfile.placeholder", "Profile name")

                    Keys.onReturnPressed: {
                        Qt.inputMethod.commit()
                        if (copyProfileNameField.displayText.trim() !== "") {
                            copyProfileButton.clicked()
                        }
                    }
                }

                Item { implicitHeight: Theme.scaled(8) }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: Theme.scaled(20)
                    Layout.rightMargin: Theme.scaled(20)
                    spacing: Theme.scaled(12)

                    Item { Layout.fillWidth: true }

                    AccessibleButton {
                        text: TranslationManager.translate("common.button.cancel", "Cancel")
                        accessibleName: TranslationManager.translate("common.accessibility.cancel", "Cancel")
                        Layout.preferredHeight: Theme.scaled(40)
                        onClicked: copyProfileDialog.close()
                    }

                    AccessibleButton {
                        id: copyProfileButton
                        text: TranslationManager.translate("profileselector.copyProfile.button", "Copy")
                        accessibleName: TranslationManager.translate("profileselector.copyProfile.accessible", "Copy profile with new name")
                        primary: true
                        enabled: copyProfileNameField.displayText.trim() !== ""
                        Layout.preferredHeight: Theme.scaled(40)
                        onClicked: {
                            Qt.inputMethod.commit()
                            var newTitle = copyProfileNameField.text.trim()
                            if (newTitle !== "") {
                                if (ProfileManager.duplicateProfile(copyProfileDialog.sourceFilename, newTitle)) {
                                    profileSelectorPage.showToast(TranslationManager.translate("profileselector.toast.profile_copied", "Profile copied"))
                                    copyProfileDialog.close()
                                } else {
                                    profileSelectorPage.showToast(TranslationManager.translate("profileselector.toast.copy_failed", "Failed to copy profile"))
                                }
                            }
                        }
                    }
                }

                Item { implicitHeight: Theme.scaled(8) }
            }
        }

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.scaled(8)
            border.color: Theme.borderColor
        }

        Accessible.role: Accessible.Dialog
        Accessible.name: TranslationManager.translate("profileselector.copyProfile.title", "Copy Profile")
    }

    // Toast notification
    function showToast(message) {
        profileToastText.text = message
        profileToast.visible = true
        profileToastTimer.restart()
    }

    Rectangle {
        id: profileToast
        parent: Overlay.overlay
        visible: false
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.bottomBarHeight + Theme.scaled(12)
        anchors.horizontalCenter: parent.horizontalCenter
        width: profileToastText.implicitWidth + Theme.scaled(32)
        height: Theme.scaled(40)
        radius: Theme.scaled(20)
        color: Theme.surfaceColor
        border.color: Theme.borderColor
        border.width: 1
        z: 10

        Text {
            id: profileToastText
            anchors.centerIn: parent
            color: Theme.textColor
            font: Theme.bodyFont
        }
    }

    Timer {
        id: profileToastTimer
        interval: 3000
        onTriggered: profileToast.visible = false
    }

    // Bottom bar
    BottomBar {
        title: TranslationManager.translate("profileselector.title", "Profiles")
        rightText: TranslationManager.translate("profileselector.current_prefix", "Current:") + " " + ProfileManager.currentProfileName
        onBackClicked: root.goBack()
    }
}
