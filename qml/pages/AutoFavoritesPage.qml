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
            if (Settings.autoFavoritesHideUnrated && favorites[i].avgEnjoyment <= 0)
                continue
            favoritesModel.append(favorites[i])
        }
    }

    // Build accessible text based on current groupBy setting
    function buildGroupByText(beanBrand, beanType, profileName, grinderModel, grinderSetting, doseWeight, finalWeight, shotCount, avgEnjoyment) {
        var groupBy = Settings.autoFavoritesGroupBy
        var parts = []

        // Add fields based on groupBy
        var includeBean = (groupBy === "bean" || groupBy === "bean_profile" || groupBy === "bean_profile_grinder")
        var includeProfile = (groupBy === "profile" || groupBy === "bean_profile" || groupBy === "bean_profile_grinder")
        var includeGrinder = (groupBy === "bean_profile_grinder")

        if (includeBean) {
            var bean = (beanBrand || "") + (beanType ? " - " + beanType : "")
            if (bean) parts.push(bean)
        }
        if (includeProfile && profileName)
            parts.push(profileName)
        if (includeGrinder) {
            var grinder = (grinderModel || "") + (grinderSetting ? " @ " + grinderSetting : "")
            if (grinder) parts.push(grinder)
        }

        // Always include recipe summary
        parts.push((doseWeight || 0).toFixed(1) + "g to " + (finalWeight || 0).toFixed(1) + "g")
        parts.push(shotCount + " " + TranslationManager.translate("autofavorites.shots", "shots"))
        if (avgEnjoyment > 0)
            parts.push(avgEnjoyment + "% enjoyment")

        return parts.join(". ")
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
                id: settingsButton
                width: Theme.scaled(36)
                height: Theme.scaled(36)
                radius: Theme.scaled(18)
                color: Theme.surfaceColor
                Accessible.ignored: true

                Image {
                    anchors.centerIn: parent
                    source: "qrc:/icons/settings.svg"
                    sourceSize.width: Theme.scaled(20)
                    sourceSize.height: Theme.scaled(20)
                    Accessible.ignored: true
                }

                AccessibleMouseArea {
                    anchors.fill: parent
                    accessibleName: TranslationManager.translate("autofavorites.settings", "Auto-Favorites Settings")
                    accessibleItem: settingsButton
                    onAccessibleClicked: settingsPopup.open()
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
                height: contentColumn.implicitHeight + Theme.spacingMedium * 2
                radius: Theme.cardRadius
                color: Theme.surfaceColor
                Accessible.ignored: true

                property string _beanText: {
                    var parts = []
                    if (model.beanBrand) parts.push(model.beanBrand)
                    if (model.beanType) parts.push(model.beanType)
                    return parts.join(" - ")
                }
                property bool _hasBean: !!(model.beanBrand || model.beanType)
                property bool _hasProfile: !!(model.profileName && model.profileName.length > 0)
                property bool _hasGrinder: Settings.autoFavoritesGroupBy.indexOf("grinder") >= 0 &&
                    !!(model.grinderModel || model.grinderSetting)
                property string _grinderText: (model.grinderModel || "") +
                    (model.grinderSetting ? " @ " + model.grinderSetting : "")
                property string _groupByText: autoFavoritesPage.buildGroupByText(
                    model.beanBrand, model.beanType, model.profileName,
                    model.grinderModel, model.grinderSetting,
                    model.doseWeight, model.finalWeight, model.shotCount, model.avgEnjoyment)

                // Whole card announces full details based on groupBy setting
                AccessibleMouseArea {
                    anchors.fill: parent
                    accessibleName: favoriteDelegate._groupByText
                    accessibleItem: favoriteDelegate
                    onAccessibleClicked: {} // Informational only
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.spacingMedium
                    spacing: Theme.spacingMedium

                    // Main info
                    ColumnLayout {
                        id: contentColumn
                        Layout.fillWidth: true
                        spacing: Theme.scaled(4)

                        // Bean · Profile · Grinder — wraps to 2 rows on small screens
                        Flow {
                            Layout.fillWidth: true
                            spacing: 0

                            Text {
                                text: favoriteDelegate._beanText
                                font.family: Theme.subtitleFont.family
                                font.pixelSize: Theme.subtitleFont.pixelSize
                                color: Theme.textColor
                                visible: favoriteDelegate._hasBean
                                Accessible.ignored: true
                            }

                            Text {
                                text: "  \u00b7  "
                                font.family: Theme.subtitleFont.family
                                font.pixelSize: Theme.subtitleFont.pixelSize
                                color: Theme.textSecondaryColor
                                visible: favoriteDelegate._hasBean && favoriteDelegate._hasProfile
                                Accessible.ignored: true
                            }

                            Text {
                                text: model.profileName || ""
                                font.family: Theme.subtitleFont.family
                                font.pixelSize: Theme.subtitleFont.pixelSize
                                color: Theme.primaryColor
                                visible: favoriteDelegate._hasProfile
                                Accessible.ignored: true
                            }

                            Text {
                                text: "  \u00b7  "
                                font.family: Theme.subtitleFont.family
                                font.pixelSize: Theme.subtitleFont.pixelSize
                                color: Theme.textSecondaryColor
                                visible: favoriteDelegate._hasGrinder && (favoriteDelegate._hasBean || favoriteDelegate._hasProfile)
                                Accessible.ignored: true
                            }

                            Text {
                                text: favoriteDelegate._grinderText
                                font.family: Theme.subtitleFont.family
                                font.pixelSize: Theme.subtitleFont.pixelSize
                                color: Theme.textSecondaryColor
                                visible: favoriteDelegate._hasGrinder
                                Accessible.ignored: true
                            }
                        }

                        // Recipe summary
                        RowLayout {
                            spacing: Theme.spacingLarge

                            Text {
                                text: (model.doseWeight || 0).toFixed(1) + "g \u2192 " +
                                      (model.finalWeight || 0).toFixed(1) + "g"
                                font.family: Theme.labelFont.family
                                font.pixelSize: Theme.labelFont.pixelSize
                                color: Theme.textSecondaryColor
                                Accessible.ignored: true
                            }

                            Text {
                                text: model.shotCount + " " +
                                      TranslationManager.translate("autofavorites.shots", "shots")
                                font.family: Theme.labelFont.family
                                font.pixelSize: Theme.labelFont.pixelSize
                                color: Theme.textSecondaryColor
                                Accessible.ignored: true
                            }

                            Text {
                                text: model.avgEnjoyment > 0 ? model.avgEnjoyment + "%" : ""
                                font.family: Theme.labelFont.family
                                font.pixelSize: Theme.labelFont.pixelSize
                                color: Theme.warningColor
                                visible: model.avgEnjoyment > 0
                                Accessible.ignored: true
                            }
                        }
                    }

                    // Info button
                    Rectangle {
                        id: infoButton
                        width: Theme.scaled(70)
                        height: Theme.scaled(40)
                        radius: Theme.scaled(20)
                        color: Theme.primaryColor
                        Accessible.ignored: true

                        Text {
                            anchors.centerIn: parent
                            text: TranslationManager.translate("autofavorites.info", "Info")
                            font.pixelSize: Theme.scaled(14)
                            font.bold: true
                            color: "white"
                            Accessible.ignored: true
                        }

                        AccessibleMouseArea {
                            anchors.fill: parent
                            accessibleName: TranslationManager.translate("autofavorites.favoriteInfo", "Favorite info") +
                                ". " + favoriteDelegate._groupByText
                            accessibleItem: infoButton
                            onAccessibleClicked: {
                                pageStack.push(Qt.resolvedUrl("AutoFavoriteInfoPage.qml"), {
                                    shotId: model.shotId,
                                    groupBy: Settings.autoFavoritesGroupBy,
                                    beanBrand: model.beanBrand || "",
                                    beanType: model.beanType || "",
                                    profileName: model.profileName || "",
                                    grinderModel: model.grinderModel || "",
                                    grinderSetting: model.grinderSetting || "",
                                    avgEnjoyment: model.avgEnjoyment || 0,
                                    shotCount: model.shotCount || 0
                                })
                            }
                        }
                    }

                    // Show button — opens Shot History filtered to this group
                    Rectangle {
                        id: showButton
                        width: Theme.scaled(70)
                        height: Theme.scaled(40)
                        radius: Theme.scaled(20)
                        color: Theme.primaryColor
                        Accessible.ignored: true

                        Text {
                            anchors.centerIn: parent
                            text: TranslationManager.translate("autofavorites.show", "Show")
                            font.pixelSize: Theme.scaled(14)
                            font.bold: true
                            color: "white"
                            Accessible.ignored: true
                        }

                        AccessibleMouseArea {
                            anchors.fill: parent
                            accessibleName: TranslationManager.translate("autofavorites.showShots", "Show shots") +
                                ". " + favoriteDelegate._groupByText
                            accessibleItem: showButton
                            onAccessibleClicked: {
                                var filter = {}
                                var groupBy = Settings.autoFavoritesGroupBy
                                var includeBean = (groupBy === "bean" || groupBy === "bean_profile" || groupBy === "bean_profile_grinder")
                                var includeProfile = (groupBy === "profile" || groupBy === "bean_profile" || groupBy === "bean_profile_grinder")
                                var includeGrinder = (groupBy === "bean_profile_grinder")

                                if (includeBean) {
                                    if (model.beanBrand) filter.beanBrand = model.beanBrand
                                    if (model.beanType) filter.beanType = model.beanType
                                }
                                if (includeProfile && model.profileName)
                                    filter.profileName = model.profileName
                                if (includeGrinder) {
                                    if (model.grinderModel) filter.grinderModel = model.grinderModel
                                    if (model.grinderSetting) filter.grinderSetting = model.grinderSetting
                                }

                                pageStack.push(Qt.resolvedUrl("ShotHistoryPage.qml"), {
                                    initialFilter: filter
                                })
                            }
                        }
                    }

                    // Load button
                    Rectangle {
                        id: loadButton
                        width: Theme.scaled(70)
                        height: Theme.scaled(40)
                        radius: Theme.scaled(20)
                        color: Theme.primaryColor
                        Accessible.ignored: true

                        Text {
                            anchors.centerIn: parent
                            text: TranslationManager.translate("autofavorites.load", "Load")
                            font.pixelSize: Theme.scaled(14)
                            font.bold: true
                            color: "white"
                            Accessible.ignored: true
                        }

                        AccessibleMouseArea {
                            anchors.fill: parent
                            accessibleName: TranslationManager.translate("autofavorites.load", "Load") +
                                ". " + favoriteDelegate._groupByText
                            accessibleItem: loadButton
                            onAccessibleClicked: {
                                MainController.loadShotWithMetadata(model.shotId)
                                if (Settings.autoFavoritesOpenBrewSettings)
                                    root.pendingBrewDialog = true
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

    // Settings dialog
    Dialog {
        id: settingsPopup
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(parent.width * 0.85, Theme.scaled(320))
        modal: true
        padding: Theme.scaled(20)
        title: TranslationManager.translate("autofavorites.settings", "Auto-Favorites Settings")
        header: Item {} // Hide default Dialog header, we use our own

        onOpened: {
            if (typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled) {
                AccessibilityManager.announce(TranslationManager.translate("autofavorites.settings", "Auto-Favorites Settings"))
            }
        }

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
                Accessible.ignored: true
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
                    Accessible.ignored: true
                }

                StyledComboBox {
                    id: groupByCombo
                    Layout.fillWidth: true
                    accessibleLabel: TranslationManager.translate("autofavorites.groupby", "Group by")
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
                        if (typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled) {
                            AccessibilityManager.announce(
                                TranslationManager.translate("autofavorites.groupby", "Group by") +
                                " " + displayText)
                        }
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
                    Accessible.ignored: true
                }

                ValueInput {
                    value: Settings.autoFavoritesMaxItems
                    from: 5
                    to: 50
                    stepSize: 5
                    accessibleName: TranslationManager.translate("autofavorites.maxitems", "Max items") +
                        ", " + value
                    onValueModified: function(newValue) {
                        Settings.autoFavoritesMaxItems = newValue
                        loadFavorites()
                    }
                }
            }

            // Hide unrated favorites
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingMedium

                Text {
                    text: TranslationManager.translate("autofavorites.hideUnrated", "Hide unrated favorites")
                    font.family: Theme.labelFont.family
                    font.pixelSize: Theme.labelFont.pixelSize
                    color: Theme.textSecondaryColor
                    Layout.fillWidth: true
                    Accessible.ignored: true
                }

                StyledSwitch {
                    checked: Settings.autoFavoritesHideUnrated
                    accessibleName: TranslationManager.translate("autofavorites.hideUnrated", "Hide unrated favorites")
                    onToggled: {
                        Settings.autoFavoritesHideUnrated = checked
                        loadFavorites()
                    }
                }
            }

            // Open brew settings after load
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingMedium

                Text {
                    text: TranslationManager.translate("autofavorites.openBrewSettings", "Open brew settings after load")
                    font.family: Theme.labelFont.family
                    font.pixelSize: Theme.labelFont.pixelSize
                    color: Theme.textSecondaryColor
                    Layout.fillWidth: true
                    Accessible.ignored: true
                }

                StyledSwitch {
                    checked: Settings.autoFavoritesOpenBrewSettings
                    accessibleName: TranslationManager.translate("autofavorites.openBrewSettings", "Open brew settings after load")
                    onToggled: Settings.autoFavoritesOpenBrewSettings = checked
                }
            }

            // Close button
            AccessibleButton {
                Layout.fillWidth: true
                text: TranslationManager.translate("common.close", "Close")
                accessibleName: TranslationManager.translate("common.close", "Close") + " " +
                    TranslationManager.translate("autofavorites.settings", "Auto-Favorites Settings")
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
