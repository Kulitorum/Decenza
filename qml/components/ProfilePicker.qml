// GridView/Repeater/ListView delegates below each declare every injected role
// (`modelData`) as a `required property` in the same edit — Bound stops plain
// context-property role injection, and a missed one renders a blank card at
// RUNTIME with nothing caught at build time. The `FilterChip` inline component
// and the dialogs' own ids are also only statically resolvable under Bound.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects
import Decenza

// Shared profile picker (profile-picker spec): search, filter chips, sort,
// bean-ranked tiers and a virtualised card grid, hosted by ProfileSelectorPage
// and the recipe wizard's profile step. The host supplies bean identity, the
// initial chip state and a beverage constraint, and gets `profileChosen`
// (filename) — it decides whether that loads the profile or merely selects it
// for a recipe (design: "host decides").
Item {
    id: picker

    // === Host contract (task 2.1) ===========================================
    property string beanBrand: ""
    property string beanType: ""
    property string roastLevel: ""
    property string teaType: ""
    // {favorites: bool} — read once at construction; chip state does not
    // persist across opens otherwise (profile-picker spec).
    property var initialChips: ({})
    // Host beverage constraint (recipe-wizard's drink type). Empty = no
    // constraint (the selector). Hides the Beverage chip group (design D6).
    property var allowedBeverageTypes: []
    // Selector-only auto-load status strip, rendered ABOVE the search/chip
    // rows when true (profile-auto-load spec).
    property bool showAutoLoadStrip: false
    // Selector-only "+" (Visualizer / Tablet / New) at the end of the search
    // row; the host owns the menu it opens.
    property bool showAddButton: false
    // For a host's KeyboardAwareContainer.textFields.
    property alias searchInput: searchField

    signal profileChosen(string filename)
    signal addRequested()

    Component.onCompleted: {
        picker.chipFavorites = picker.initialChips.favorites === true
        picker.requestBeanRanking()
    }

    // === Chip / search / sort state (never persisted) ======================
    property bool chipFavorites: false
    property var chipSources: []
    property var chipBeverages: []
    property string searchText: ""
    // Grid sort, this visit only. The favorites ORDER (idle pills) is a
    // setting edited in ProfileFavoritesOrderDialog, never by this control.
    property string sortMode: "usage"

    function buildChips() {
        return {
            favorites: picker.chipFavorites,
            sources: picker.chipSources,
            beverages: picker.chipBeverages
        }
    }

    function clearFilters() {
        picker.chipFavorites = false
        picker.chipSources = []
        picker.chipBeverages = []
        picker.searchText = ""
        searchField.text = ""
    }

    function toggleSourceChip(id) {
        var arr = picker.chipSources.slice()
        var idx = arr.indexOf(id)
        if (idx >= 0) arr.splice(idx, 1); else arr.push(id)
        picker.chipSources = arr
    }
    function toggleBeverageChip(id) {
        var arr = picker.chipBeverages.slice()
        var idx = arr.indexOf(id)
        if (idx >= 0) arr.splice(idx, 1); else arr.push(id)
        picker.chipBeverages = arr
    }

    // === Filtered catalogue + facet counts ==================================
    // Q_INVOKABLE calls establish no dependency by themselves, so each binding
    // reads a NOTIFYing property first purely to create one (the "Create
    // dependency" idiom already used throughout this codebase).
    readonly property var filteredAll: {
        var _dep1 = ProfileManager.allProfilesList
        var _dep2 = Settings.app.favoriteProfiles
        return ProfileManager.filterProfiles(picker.buildChips(), picker.searchText, picker.allowedBeverageTypes)
    }
    readonly property var facets: {
        var _dep1 = ProfileManager.allProfilesList
        var _dep2 = Settings.app.favoriteProfiles
        return ProfileManager.facetCounts(picker.buildChips(), picker.searchText, picker.allowedBeverageTypes)
    }

    // === Bean-ranked tiers ===================================================
    property var _ranked: ({})

    function requestBeanRanking() {
        picker._ranked = ({})
        if (picker.beanBrand !== "" || picker.beanType !== "")
            MainController.shotHistory.requestRankedProfilesForBean(
                picker.beanBrand, picker.beanType, picker.roastLevel, picker.teaType)
    }
    onBeanBrandChanged: picker.requestBeanRanking()
    onBeanTypeChanged: picker.requestBeanRanking()
    onRoastLevelChanged: picker.requestBeanRanking()
    onTeaTypeChanged: picker.requestBeanRanking()

    Connections {
        target: MainController.shotHistory
        function onRankedProfilesForBeanReady(result) {
            // Stale-reply guard: ignore a ranking that answers a bean the
            // host has since switched away from.
            if (String(result.queryBrand || "") !== picker.beanBrand
                || String(result.queryType || "") !== picker.beanType)
                return
            picker._ranked = result
        }
    }

    // Tier ①: exact profiles used with this bean, most recent first, filtered
    // like the grid. No reason chip — the tier header already says why.
    readonly property var tier1List: {
        var byTitle = {}
        for (var i = 0; i < picker.filteredAll.length; ++i) byTitle[picker.filteredAll[i].title] = picker.filteredAll[i]
        var withBean = picker._ranked.withBean || []
        var out = []
        var seen = {}
        for (i = 0; i < withBean.length; ++i) {
            var e = byTitle[withBean[i].profileName]
            if (e && !seen[e.title]) { out.push(e); seen[e.title] = true }
        }
        return out
    }

    // Tier ②: knowledge-driven recommendations first (KB roastAffinity for
    // coffee, tea-type match for tea — reused, not reimplemented: the same
    // ProfileManager invokables the wizard always called), then similar-bean
    // history, capped to a handful. Reused across BOTH hosts (design D3's
    // centralization) rather than kept wizard-only, per CLAUDE.md's rule
    // against a second copy of the same ranking.
    readonly property var tier2List: {
        var byTitle = {}
        for (var i = 0; i < picker.filteredAll.length; ++i) byTitle[picker.filteredAll[i].title] = picker.filteredAll[i]
        var used = {}
        for (i = 0; i < picker.tier1List.length; ++i) used[picker.tier1List[i].title] = true

        var out = []
        function pushWithReason(entry, reason) {
            var copy = Object.assign({}, entry)
            copy.reason = reason
            out.push(copy)
            used[entry.title] = true
        }

        if (picker.teaType !== "") {
            for (var t in byTitle) {
                if (used[t]) continue
                if (ProfileManager.teaProfileMatchesType(t, picker.teaType))
                    pushWithReason(byTitle[t], TranslationManager.translate(
                        "recipes.wizard.profiles.matchesType", "matches %1").arg(picker.teaType))
            }
        } else if (picker.roastLevel !== "") {
            for (t in byTitle) {
                if (used[t]) continue
                if (ProfileManager.kbProfileSuitsRoast(t, picker.roastLevel))
                    pushWithReason(byTitle[t], TranslationManager.translate(
                        "recipes.wizard.profiles.suitsRoast", "suits %1 roasts").arg(picker.roastLevel.toLowerCase()))
            }
        }
        var similar = picker._ranked.similar || []
        for (i = 0; i < similar.length; ++i) {
            var e = byTitle[similar[i].profileName]
            if (e && !used[e.title])
                pushWithReason(e, TranslationManager.translate(
                    "recipes.wizard.profiles.similarBeans", "used with similar beans"))
        }
        // A handful of the best, not the whole matching set (recipe-wizard spec).
        return out.slice(0, 5)
    }

    // === Sort: the "All" section ============================================
    readonly property var sortedAllList: {
        var list = picker.filteredAll.slice()

        if (picker.sortMode === "alpha") {
            list.sort(function(a, b) { return a.title.localeCompare(b.title) })
            return list
        }
        // "usage": current profile first, then most recent shot descending,
        // never-used last in alpha order.
        var usage = ProfileManager.profileUsage
        var currentTitle = ProfileManager.currentProfileTitle
        list.sort(function(a, b) {
            if (a.title === currentTitle && b.title !== currentTitle) return -1
            if (b.title === currentTitle && a.title !== currentTitle) return 1
            var ua = usage[a.title], ub = usage[b.title]
            var ta = ua ? ua.lastTimestamp : 0
            var tb = ub ? ub.lastTimestamp : 0
            if (!!ta !== !!tb) return ta ? -1 : 1
            if (ta !== tb) return tb - ta
            return a.title.localeCompare(b.title)
        })
        return list
    }

    // One row: exact-bean matches first (reason "used with <bean>"), then the
    // knowledge / similar-bean recommendations.
    readonly property string beanLabel: picker.beanType !== "" ? picker.beanType : picker.beanBrand
    readonly property var recommendedList: {
        var out = []
        var usedWith = TranslationManager.translate("profilepicker.reason.usedWith", "used with %1").arg(picker.beanLabel)
        for (var i = 0; i < picker.tier1List.length; ++i) {
            var copy = Object.assign({}, picker.tier1List[i])
            copy.reason = usedWith
            out.push(copy)
        }
        return out.concat(picker.tier2List)
    }

    readonly property bool isEmpty: picker.sortedAllList.length === 0
                                     && picker.recommendedList.length === 0

    function cardIsCurrent(entry) { return entry.name === ProfileManager.baseProfileName }

    // === Card action routing (shared across tiers + grid) ===================
    function openPreview(filename, title) {
        previewPopup.profileFilename = filename
        previewPopup.profileName = title
        previewPopup.open()
    }
    function openKnowledge(title) {
        knowledgeDialog.openFor(title)
    }
    function openActions(entry) {
        actionsDialog.profileFilename = entry.name
        actionsDialog.profileTitle = entry.title
        actionsDialog.profileIsBuiltIn = entry.source === 0
        actionsDialog.profileIsFavorite = Settings.app.isFavoriteProfile(entry.name)
        actionsDialog.profileIsAutoLoad = entry.name !== "" && entry.name === Settings.app.autoLoadProfileFilename
        actionsDialog.open()
    }

    function showToast(message) {
        toastText.text = message
        toastRect.visible = true
        toastTimer.restart()
    }

    // A toggleable filter pill with a faceted count (profile-picker "Faceted
    // chip counts"). Inline component: it must sit at this file's top level
    // (a sibling of the root's other children), not nested inside the Flow
    // that uses it — Qt's inline-component syntax does not nest.
    component FilterChip: Rectangle {
        id: chip
        property string label: ""
        property int count: 0
        property bool active: false
        signal toggled()

        implicitHeight: Theme.scaled(30)
        implicitWidth: chipRow.implicitWidth + Theme.scaled(18)
        radius: height / 2
        color: chip.active ? Theme.primaryColor : Theme.insetBackgroundColor
        border.color: chip.active ? Theme.primaryColor : Theme.borderColor
        border.width: 1

        RowLayout {
            id: chipRow
            anchors.centerIn: parent
            spacing: Theme.scaled(4)
            Text {
                text: chip.label
                color: chip.active ? Theme.primaryContrastColor : Theme.textColor
                font: Theme.captionFont
            }
            Text {
                text: "(" + chip.count + ")"
                color: chip.active ? Theme.primaryContrastColor : Theme.textSecondaryColor
                font: Theme.captionFont
            }
        }

        AccessibleMouseArea {
            anchors.fill: parent
            accessibleRole: Accessible.CheckBox
            accessibleChecked: chip.active
            accessibleName: chip.label + ", " + chip.count
            onAccessibleClicked: chip.toggled()
        }
    }

    // === Layout ==============================================================
    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.scaled(10)

        // ---- Auto-load strip (selector-only; profile-auto-load spec) ----
        Rectangle {
            id: autoLoadStrip
            Layout.fillWidth: true
            Layout.preferredHeight: Math.round(Theme.captionFont.pixelSize * 2.4)
            color: Theme.cardBackgroundColor
            border.color: Theme.borderColor
            border.width: 1
            radius: Theme.cardRadius

            visible: picker.showAutoLoadStrip
                     && Settings.app.autoLoadProfileFilename !== ""
                     && Settings.app.isFavoriteProfile(Settings.app.autoLoadProfileFilename)

            readonly property var autoLoadProfile: visible
                ? ProfileManager.getProfileByFilename(Settings.app.autoLoadProfileFilename)
                : ({})
            readonly property string autoLoadTitle: autoLoadStrip.autoLoadProfile && autoLoadStrip.autoLoadProfile.title
                                                    ? autoLoadStrip.autoLoadProfile.title
                                                    : Settings.app.autoLoadProfileFilename

            Accessible.role: Accessible.StaticText
            Accessible.name: TranslationManager.translate("profileselector.strip.auto_load_label", "Auto-load:") + " " + autoLoadStrip.autoLoadTitle
            Accessible.ignored: !autoLoadStrip.visible

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.scaled(8)
                anchors.rightMargin: Theme.scaled(2)
                spacing: Theme.scaled(4)

                Image {
                    source: "qrc:/icons/pin.svg"
                    sourceSize.width: Theme.scaled(11)
                    sourceSize.height: Theme.scaled(11)
                    Layout.alignment: Qt.AlignVCenter
                    Accessible.ignored: true
                    layer.enabled: true
                    layer.smooth: true
                    layer.effect: MultiEffect { colorization: 1.0; colorizationColor: Theme.primaryColor }
                }
                Text {
                    text: TranslationManager.translate("profileselector.strip.auto_load_label", "Auto-load:")
                    color: Theme.textSecondaryColor
                    font: Theme.captionFont
                    Layout.alignment: Qt.AlignVCenter
                    Accessible.ignored: true
                }
                Text {
                    text: autoLoadStrip.autoLoadTitle
                    color: Theme.textColor
                    font: Theme.captionFont
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignVCenter
                    Accessible.ignored: true
                }
                Text {
                    text: TranslationManager.translate("profileselector.strip.revert_after", "revert after")
                    color: Theme.textSecondaryColor
                    font: Theme.captionFont
                    Layout.alignment: Qt.AlignVCenter
                    Accessible.ignored: true
                }
                ValueInput {
                    Layout.preferredWidth: implicitWidth
                    Layout.preferredHeight: Math.round(Theme.captionFont.pixelSize * 1.9)
                    Layout.alignment: Qt.AlignVCenter
                    valueFontPixelSize: Theme.captionFont.pixelSize
                    value: Settings.app.autoLoadRevertMinutes
                    from: 0
                    to: 60
                    stepSize: 1
                    suffix: TranslationManager.translate("profileselector.strip.minutes_short", "min")
                    displayText: value === 0
                        ? TranslationManager.translate("profileselector.strip.off", "off")
                        : value + " " + TranslationManager.translate("profileselector.strip.minutes_short", "min")
                    accessibleName: TranslationManager.translate("profileselector.strip.revert_after", "revert after")
                    onValueModified: function(newValue) { Settings.app.autoLoadRevertMinutes = newValue }
                }
                AccessibleButton {
                    text: "×"
                    Layout.preferredWidth: Math.round(Theme.captionFont.pixelSize * 1.9)
                    Layout.preferredHeight: Math.round(Theme.captionFont.pixelSize * 1.9)
                    Layout.alignment: Qt.AlignVCenter
                    accessibleName: TranslationManager.translate("profileselector.strip.clear_aria", "Disable auto-load")
                    contentItem: Text {
                        text: "×"
                        color: Theme.textColor
                        font.pixelSize: Math.round(Theme.captionFont.pixelSize * 1.3)
                        font.bold: true
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        Accessible.ignored: true
                    }
                    onClicked: {
                        Settings.app.autoLoadProfileFilename = ""
                        picker.showToast(TranslationManager.translate("profileselector.toast.auto_load_disabled", "Auto-load disabled"))
                    }
                }
            }
        }

        // ---- Search + sort ----
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.scaled(10)

            StyledTextField {
                id: searchField
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.scaled(44)
                placeholder: TranslationManager.translate("profilepicker.search.placeholder", "Search profiles…")
                inputMethodHints: Qt.ImhNoPredictiveText
                onDisplayTextChanged: picker.searchText = displayText.toLowerCase()
            }

            StyledComboBox {
                id: sortCombo
                Layout.preferredWidth: Theme.scaled(190)
                Layout.preferredHeight: Theme.scaled(44)
                model: [TranslationManager.translate("profilepicker.sort.alpha", "A–Z"),
                        TranslationManager.translate("profilepicker.sort.recent", "Recently used")]
                currentIndex: picker.sortMode === "alpha" ? 0 : 1
                onActivated: function(index) { picker.sortMode = index === 0 ? "alpha" : "usage" }
                background: Rectangle {
                    radius: Theme.scaled(6)
                    color: Theme.surfaceColor
                    border.color: Theme.borderColor
                    border.width: 1
                }
                contentItem: Text {
                    text: sortCombo.displayText
                    color: Theme.textColor
                    font: Theme.bodyFont
                    verticalAlignment: Text.AlignVCenter
                    leftPadding: Theme.scaled(12)
                    rightPadding: Theme.scaled(24)
                    elide: Text.ElideRight
                    Accessible.ignored: true
                }
                indicator: Text {
                    x: sortCombo.width - width - Theme.scaled(10)
                    y: (sortCombo.height - height) / 2
                    text: "▼"
                    color: Theme.textSecondaryColor
                    font.pixelSize: Theme.scaled(10)
                    Accessible.ignored: true
                }
                accessibleLabel: TranslationManager.translate("profilepicker.sort.label", "Sort")
            }

            // Favorites ORDER is a setting (idle pill order), not a view sort,
            // so it has its own door rather than hiding behind the Favorites chip.
            AccessibleButton {
                text: TranslationManager.translate("profilepicker.favoritesOrder.button", "Favorites…")
                accessibleName: TranslationManager.translate("profilepicker.favoritesOrder.accessible", "Favorites order")
                Layout.preferredHeight: Theme.scaled(44)
                onClicked: favoritesOrderDialog.open()
            }

            AccessibleButton {
                visible: picker.showAddButton
                text: "+"
                accessibleName: TranslationManager.translate("profilepicker.addMenu.accessible", "Add a profile")
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
                onClicked: picker.addRequested()
            }
        }

        // ---- Filter chips ----
        Flow {
            Layout.fillWidth: true
            spacing: Theme.scaled(8)

            FilterChip {
                label: TranslationManager.translate("profilepicker.chip.favorites", "Favorites")
                count: picker.facets.favorites || 0
                active: picker.chipFavorites
                onToggled: picker.chipFavorites = !picker.chipFavorites
            }
            Repeater {
                model: [
                    { id: "builtin", label: TranslationManager.translate("profileselector.filter.builtin", "Decent Built-in") },
                    { id: "downloaded", label: TranslationManager.translate("profileselector.filter.downloaded", "Downloaded") },
                    { id: "mine", label: TranslationManager.translate("profileselector.filter.user", "User Created") }
                ]
                delegate: FilterChip {
                    id: sourceChipDelegate
                    required property var modelData
                    label: sourceChipDelegate.modelData.label
                    count: picker.facets[sourceChipDelegate.modelData.id] || 0
                    active: picker.chipSources.indexOf(sourceChipDelegate.modelData.id) >= 0
                    onToggled: picker.toggleSourceChip(sourceChipDelegate.modelData.id)
                }
            }
            Repeater {
                model: picker.allowedBeverageTypes.length === 0 ? [
                    { id: "espresso", label: TranslationManager.translate("profilepicker.beverage.espresso", "Espresso") },
                    { id: "filter", label: TranslationManager.translate("profilepicker.beverage.filter", "Filter") },
                    { id: "tea", label: TranslationManager.translate("profilepicker.beverage.tea", "Tea") },
                    { id: "cleaning", label: TranslationManager.translate("profilepicker.beverage.cleaning", "Cleaning") }
                ] : []
                delegate: FilterChip {
                    id: beverageChipDelegate
                    required property var modelData
                    label: beverageChipDelegate.modelData.label
                    count: picker.facets[beverageChipDelegate.modelData.id] || 0
                    active: picker.chipBeverages.indexOf(beverageChipDelegate.modelData.id) >= 0
                    onToggled: picker.toggleBeverageChip(beverageChipDelegate.modelData.id)
                }
            }
        }

        // ---- The virtualised "All" grid ----
        GridView {
            id: allGrid
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: !picker.isEmpty
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            readonly property int columns: Math.max(1, Math.floor(width / Theme.scaled(300)))
            cellWidth: width / Math.max(1, columns)
            cellHeight: Theme.scaled(100)

            model: picker.sortedAllList
            // Tiers and the "All" heading scroll WITH the grid as its header, so
            // the page is one Flickable and the grid keeps its virtualisation.
            // Pinned above the grid they ate the grid's height on a tablet and
            // left it nothing to scroll (design D4).
            header: Component {
                Column {
                    width: allGrid.width
                    spacing: Theme.scaled(10)
                    // ---- Recommended for this bean ----
                    Column {
                        width: parent ? parent.width : 0
                        visible: picker.recommendedList.length > 0
                        spacing: Theme.scaled(4)
                        Text {
                            text: picker.beanLabel !== ""
                                ? TranslationManager.translate("profilepicker.tier.recommendedFor", "Recommended for %1").arg(picker.beanLabel)
                                : TranslationManager.translate("recipes.wizard.profiles.recommended", "Recommended")
                            font: Theme.captionFont
                            color: Theme.textSecondaryColor
                            Accessible.role: Accessible.Heading
                            Accessible.name: text
                        }
                        // Wraps like the grid below, same columns, so nothing
                        // hides off the right edge. Bounded: exact-bean matches
                        // plus a capped handful of recommendations.
                        Grid {
                            width: parent ? parent.width : 0
                            columns: allGrid.columns
                            columnSpacing: 0
                            rowSpacing: Theme.scaled(8)
                            Repeater {
                                model: picker.recommendedList
                                delegate: ProfileCard {
                                    id: tier2Card
                                    required property var modelData
                                    width: allGrid.cellWidth - Theme.scaled(8)
                                    height: Theme.scaled(92)
                                    entry: tier2Card.modelData
                                    current: picker.cardIsCurrent(tier2Card.modelData)
                                    reason: tier2Card.modelData.reason || ""
                                    onChosen: picker.profileChosen(tier2Card.modelData.name)
                                    onLongPressed: picker.openPreview(tier2Card.modelData.name, tier2Card.modelData.title)
                                    onSparkleRequested: picker.openKnowledge(tier2Card.modelData.title)
                                    onInfoRequested: AppShell.profileInfoRequested(tier2Card.modelData.name, tier2Card.modelData.title)
                                    onOverflowRequested: picker.openActions(tier2Card.modelData)
                                }
                            }
                        }
                    }

                    // ---- "All" section header, only shown alongside tiers ----
                    Text {
                        visible: picker.sortedAllList.length > 0 && picker.recommendedList.length > 0
                        text: TranslationManager.translate("recipes.wizard.profiles.all", "All profiles")
                        font: Theme.captionFont
                        color: Theme.textSecondaryColor
                        Accessible.role: Accessible.Heading
                        Accessible.name: text
                    }

                    // Breathing room between the header and the first grid row.
                    Item { width: 1; height: Theme.scaled(4) }
                }
            }
            delegate: ProfileCard {
                id: gridCard
                required property var modelData
                width: allGrid.cellWidth - Theme.scaled(8)
                height: allGrid.cellHeight - Theme.scaled(8)
                entry: gridCard.modelData
                current: picker.cardIsCurrent(gridCard.modelData)
                onChosen: picker.profileChosen(gridCard.modelData.name)
                onLongPressed: picker.openPreview(gridCard.modelData.name, gridCard.modelData.title)
                onSparkleRequested: picker.openKnowledge(gridCard.modelData.title)
                onInfoRequested: AppShell.profileInfoRequested(gridCard.modelData.name, gridCard.modelData.title)
                onOverflowRequested: picker.openActions(gridCard.modelData)
            }
        }

        // ---- Empty state ----
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: picker.isEmpty
            spacing: Theme.scaled(12)

            Item { Layout.fillHeight: true }
            Text {
                Layout.alignment: Qt.AlignHCenter
                text: TranslationManager.translate("profilepicker.empty.message", "No profiles match.")
                color: Theme.textSecondaryColor
                font: Theme.bodyFont
            }
            AccessibleButton {
                Layout.alignment: Qt.AlignHCenter
                text: TranslationManager.translate("profilepicker.empty.clear_filters", "Clear filters")
                accessibleName: TranslationManager.translate("profilepicker.empty.clear_filters", "Clear filters")
                onClicked: picker.clearFilters()
            }
            Item { Layout.fillHeight: true }
        }
    }

    // === Shared dialogs (one instance per picker, D8/D3 centralization) =====

    DecenzaDialog {
        id: actionsDialog
        x: (parent.width - width) / 2
        y: (parent.height - height) / 2
        padding: 20
        modal: true
        focus: true
        closePolicy: Dialog.CloseOnEscape | Dialog.CloseOnPressOutside

        property string profileFilename: ""
        property string profileTitle: ""
        property bool profileIsBuiltIn: false
        property bool profileIsFavorite: false
        property bool profileIsAutoLoad: false

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            border.color: Theme.textSecondaryColor
            border.width: 1
        }

        contentItem: ColumnLayout {
            spacing: Theme.scaled(12)

            Accessible.role: Accessible.Dialog
            Accessible.name: TranslationManager.translate("profileselector.dialog.profile_actions_title", "Profile actions")
                + (actionsDialog.profileTitle ? ": " + actionsDialog.profileTitle : "")

            Text {
                Layout.preferredWidth: Theme.scaled(280)
                text: actionsDialog.profileTitle
                color: Theme.textColor
                font: Theme.subtitleFont
                elide: Text.ElideRight
                Accessible.role: Accessible.StaticText
                Accessible.name: TranslationManager.translate("profileselector.dialog.actions_for", "Actions for") + " " + actionsDialog.profileTitle
            }

            AccessibleButton {
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.scaled(40)
                text: TranslationManager.translate("profileselector.menu.edit", "Edit Profile")
                accessibleName: TranslationManager.translate("profileselector.accessible.edit_profile", "Edit profile")
                onClicked: {
                    actionsDialog.close()
                    ProfileManager.loadProfile(actionsDialog.profileFilename)
                    AppShell.profileEditorRequested()
                }
            }

            AccessibleButton {
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.scaled(40)
                text: TranslationManager.translate("profileselector.menu.copy", "Copy Profile")
                accessibleName: TranslationManager.translate("profileselector.accessible.copy_profile", "Copy profile")
                onClicked: {
                    actionsDialog.close()
                    copyDialog.sourceFilename = actionsDialog.profileFilename
                    copyDialog.sourceTitle = actionsDialog.profileTitle
                    copyDialog.open()
                }
            }

            AccessibleButton {
                visible: !actionsDialog.profileIsBuiltIn
                Layout.fillWidth: true
                Layout.preferredHeight: visible ? Theme.scaled(40) : 0
                text: TranslationManager.translate("profileselector.menu.rename", "Rename Profile")
                accessibleName: TranslationManager.translate("profileselector.accessible.rename_profile", "Rename profile")
                onClicked: {
                    actionsDialog.close()
                    renameDialog.profileFilename = actionsDialog.profileFilename
                    renameDialog.currentTitle = actionsDialog.profileTitle
                    renameDialog.open()
                }
            }

            AccessibleButton {
                visible: actionsDialog.profileIsFavorite
                Layout.fillWidth: true
                Layout.preferredHeight: visible ? Theme.scaled(40) : 0
                text: actionsDialog.profileIsAutoLoad
                      ? TranslationManager.translate("profileselector.menu.disable_auto_load", "Disable Auto-Load")
                      : TranslationManager.translate("profileselector.menu.set_auto_load", "Set Auto-Load")
                accessibleName: actionsDialog.profileIsAutoLoad
                      ? TranslationManager.translate("profileselector.accessible.disable_auto_load", "Disable auto-load")
                      : TranslationManager.translate("profileselector.accessible.set_auto_load", "Set as auto-load profile")
                onClicked: {
                    if (actionsDialog.profileIsAutoLoad) {
                        Settings.app.autoLoadProfileFilename = ""
                        picker.showToast(TranslationManager.translate("profileselector.toast.auto_load_disabled", "Auto-load disabled"))
                    } else {
                        Settings.app.autoLoadProfileFilename = actionsDialog.profileFilename
                        picker.showToast(TranslationManager.translate("profileselector.toast.auto_load_set", "Auto-load set to %1").arg(actionsDialog.profileTitle))
                    }
                    actionsDialog.close()
                }
            }

            AccessibleButton {
                visible: !actionsDialog.profileIsBuiltIn
                Layout.fillWidth: true
                Layout.preferredHeight: visible ? Theme.scaled(40) : 0
                destructive: true
                text: TranslationManager.translate("profileselector.menu.delete", "Delete Profile")
                accessibleName: TranslationManager.translate("profileselector.accessible.delete_permanently", "Delete profile permanently")
                onClicked: {
                    actionsDialog.close()
                    deleteDialog.profileName = actionsDialog.profileFilename
                    deleteDialog.profileTitle = actionsDialog.profileTitle
                    deleteDialog.isFavorite = actionsDialog.profileIsFavorite
                    deleteDialog.open()
                }
            }
        }
    }

    DecenzaDialog {
        id: deleteDialog
        anchors.centerIn: parent
        width: Theme.scaled(350)
        padding: 0
        modal: true

        property string profileName: ""
        property string profileTitle: ""
        property bool isFavorite: false
        property int recipesUsingProfile: 0
        onAboutToShow: recipesUsingProfile = MainController.recipeStorage.countRecipesUsingProfile(profileTitle)

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
            Rectangle { anchors.bottom: parent.bottom; anchors.left: parent.left; anchors.right: parent.right; height: 1; color: Theme.borderColor }
        }

        contentItem: ColumnLayout {
            spacing: Theme.scaled(15)

            Text {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.scaled(20)
                Layout.rightMargin: Theme.scaled(20)
                Layout.topMargin: Theme.scaled(15)
                text: deleteDialog.isFavorite
                      ? "\"" + deleteDialog.profileTitle + "\" " + TranslationManager.translate("profileselector.dialog.delete_favorite_msg", "is in your favorites.\n\nDeleting will also remove it from favorites.\n\nAre you sure you want to delete this profile?")
                      : TranslationManager.translate("profileselector.dialog.delete_confirm_prefix", "Are you sure you want to delete") + " \"" + deleteDialog.profileTitle + "\"?\n\n" + TranslationManager.translate("profileselector.dialog.delete_confirm_suffix", "This cannot be undone.")
                color: Theme.textColor
                font: Theme.bodyFont
                wrapMode: Text.WordWrap
            }

            Text {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.scaled(20)
                Layout.rightMargin: Theme.scaled(20)
                visible: deleteDialog.recipesUsingProfile !== 0
                text: (deleteDialog.recipesUsingProfile < 0
                       ? TranslationManager.translate("profileselector.dialog.delete_recipe_count_failed", "Couldn't check whether any recipes use this profile. Any that do will show as missing their profile until you pick another in the recipe editor.")
                       : deleteDialog.recipesUsingProfile === 1
                       ? TranslationManager.translate("profileselector.dialog.delete_one_recipe_uses", "1 recipe uses this profile. It will show as missing its profile until you pick another in the recipe editor.")
                       : TranslationManager.translate("profileselector.dialog.delete_recipes_use", "%1 recipes use this profile. They will show as missing their profile until you pick another in the recipe editor.").arg(deleteDialog.recipesUsingProfile))
                color: Theme.warningColor
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

        background: Rectangle { color: Theme.surfaceColor; radius: Theme.scaled(8); border.color: Theme.borderColor }
    }

    DecenzaDialog {
        id: copyDialog
        anchors.centerIn: parent
        width: Theme.scaled(400)
        padding: 0
        modal: true

        property string sourceFilename: ""
        property string sourceTitle: ""

        onAboutToShow: {
            copyNameField.text = sourceTitle + " " + TranslationManager.translate("profileselector.copy.suffix", "Copy")
            copyNameField.forceActiveFocus()
            copyNameField.selectAll()
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
            Rectangle { anchors.bottom: parent.bottom; anchors.left: parent.left; anchors.right: parent.right; height: 1; color: Theme.borderColor }
        }

        contentItem: KeyboardAwareContainer {
            inOverlay: true
            textFields: [copyNameField]
            implicitHeight: copyColumn.implicitHeight
            implicitWidth: copyColumn.implicitWidth

            Accessible.role: Accessible.Dialog
            Accessible.name: TranslationManager.translate("profileselector.copyProfile.title", "Copy Profile")

            ColumnLayout {
                id: copyColumn
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
                    id: copyNameField
                    Layout.fillWidth: true
                    Layout.leftMargin: Theme.scaled(20)
                    Layout.rightMargin: Theme.scaled(20)
                    Layout.preferredHeight: Theme.scaled(44)
                    placeholder: TranslationManager.translate("profileselector.copyProfile.placeholder", "Profile name")
                    Keys.onReturnPressed: {
                        Keyboard.commit()
                        if (copyNameField.displayText.trim() !== "") copyButton.clicked()
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
                        onClicked: copyDialog.close()
                    }
                    AccessibleButton {
                        id: copyButton
                        text: TranslationManager.translate("profileselector.copyProfile.button", "Copy")
                        accessibleName: TranslationManager.translate("profileselector.copyProfile.accessible", "Copy profile with new name")
                        primary: true
                        enabled: copyNameField.displayText.trim() !== ""
                        Layout.preferredHeight: Theme.scaled(40)
                        onClicked: {
                            Keyboard.commit()
                            var newTitle = copyNameField.text.trim()
                            if (newTitle !== "") {
                                if (ProfileManager.duplicateProfile(copyDialog.sourceFilename, newTitle)) {
                                    picker.showToast(TranslationManager.translate("profileselector.toast.profile_copied", "Profile copied"))
                                    copyDialog.close()
                                } else {
                                    picker.showToast(TranslationManager.translate("profileselector.toast.copy_failed", "Failed to copy profile"))
                                }
                            }
                        }
                    }
                }
                Item { implicitHeight: Theme.scaled(8) }
            }
        }

        background: Rectangle { color: Theme.surfaceColor; radius: Theme.scaled(8); border.color: Theme.borderColor }
    }

    DecenzaDialog {
        id: renameDialog
        anchors.centerIn: parent
        width: Theme.scaled(400)
        padding: 0
        modal: true

        property string profileFilename: ""
        property string currentTitle: ""

        onAboutToShow: {
            renameNameField.text = currentTitle
            renameNameField.forceActiveFocus()
            renameNameField.selectAll()
        }

        header: Item {
            implicitHeight: Theme.scaled(50)
            Text {
                anchors.left: parent.left
                anchors.leftMargin: Theme.scaled(20)
                anchors.verticalCenter: parent.verticalCenter
                text: TranslationManager.translate("profileselector.renameProfile.title", "Rename Profile")
                font: Theme.titleFont
                color: Theme.textColor
            }
            Rectangle { anchors.bottom: parent.bottom; anchors.left: parent.left; anchors.right: parent.right; height: 1; color: Theme.borderColor }
        }

        contentItem: KeyboardAwareContainer {
            inOverlay: true
            textFields: [renameNameField]
            implicitHeight: renameColumn.implicitHeight
            implicitWidth: renameColumn.implicitWidth

            Accessible.role: Accessible.Dialog
            Accessible.name: TranslationManager.translate("profileselector.renameProfile.title", "Rename Profile")

            ColumnLayout {
                id: renameColumn
                anchors.fill: parent
                spacing: Theme.scaled(12)

                Item { implicitHeight: Theme.scaled(8) }
                Text {
                    Layout.fillWidth: true
                    Layout.leftMargin: Theme.scaled(20)
                    Layout.rightMargin: Theme.scaled(20)
                    text: TranslationManager.translate("profileselector.renameProfile.label", "Enter a new name:")
                    color: Theme.textColor
                    font: Theme.bodyFont
                    wrapMode: Text.Wrap
                }
                StyledTextField {
                    id: renameNameField
                    Layout.fillWidth: true
                    Layout.leftMargin: Theme.scaled(20)
                    Layout.rightMargin: Theme.scaled(20)
                    Layout.preferredHeight: Theme.scaled(44)
                    placeholder: TranslationManager.translate("profileselector.renameProfile.placeholder", "Profile name")
                    Keys.onReturnPressed: {
                        Keyboard.commit()
                        if (renameButton.enabled) renameButton.clicked()
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
                        onClicked: renameDialog.close()
                    }
                    AccessibleButton {
                        id: renameButton
                        text: TranslationManager.translate("profileselector.renameProfile.button", "Rename")
                        accessibleName: TranslationManager.translate("profileselector.renameProfile.accessible", "Rename profile")
                        primary: true
                        enabled: renameNameField.displayText.trim() !== "" && renameNameField.displayText.trim() !== renameDialog.currentTitle
                        Layout.preferredHeight: Theme.scaled(40)
                        onClicked: {
                            Keyboard.commit()
                            var newTitle = renameNameField.text.trim()
                            if (newTitle !== "" && newTitle !== renameDialog.currentTitle) {
                                if (ProfileManager.renameProfile(renameDialog.profileFilename, newTitle)) {
                                    picker.showToast(TranslationManager.translate("profileselector.toast.profile_renamed", "Profile renamed"))
                                    renameDialog.close()
                                } else {
                                    picker.showToast(TranslationManager.translate("profileselector.toast.rename_failed", "Failed to rename profile"))
                                }
                            }
                        }
                    }
                }
                Item { implicitHeight: Theme.scaled(8) }
            }
        }

        background: Rectangle { color: Theme.surfaceColor; radius: Theme.scaled(8); border.color: Theme.borderColor }
    }

    ProfileKnowledgeDialog { id: knowledgeDialog }

    ProfilePreviewPopup { id: previewPopup }

    ProfileFavoritesOrderDialog { id: favoritesOrderDialog }

    // ---- Toast ----
    Rectangle {
        id: toastRect
        parent: Overlay.overlay
        visible: false
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.bottomBarHeight + Theme.scaled(12)
        anchors.horizontalCenter: parent.horizontalCenter
        width: toastText.implicitWidth + Theme.scaled(32)
        height: Theme.scaled(40)
        radius: Theme.scaled(20)
        color: Theme.surfaceColor
        border.color: Theme.borderColor
        border.width: 1
        z: 10

        Text {
            id: toastText
            anchors.centerIn: parent
            color: Theme.textColor
            font: Theme.bodyFont
        }
    }
    Timer {
        id: toastTimer
        interval: 3000
        onTriggered: toastRect.visible = false
    }
}
