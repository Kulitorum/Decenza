pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decenza
import "BagName.js" as BagName
import "DateUtils.js" as DateUtils
import "GrindSuggestionText.js" as GrindSuggestionText

// Genius Grind (add-genius-grind-widget; designed by Mark Palmos): the grind
// suggestion for a chosen bean and profile on the active equipment, naming the shot
// it rests on, with Apply to load both and set the grind. The number is
// EquipmentService.requestGrindSuggestion, the same one the recipe wizard shows.
DecenzaDialog {
    id: root

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(parent ? parent.width - Theme.scaled(40) : Theme.scaled(420), Theme.scaled(460))
    // In a short window the open choice list shrinks rather than pushing Apply off screen.
    height: Math.min(implicitHeight, parent ? parent.height - Theme.scaled(40) : implicitHeight)
    modal: true
    padding: 0
    header: null
    footer: null
    // An Apply in flight cannot be called back, so the popup stays until it ends.
    closePolicy: root.applying ? Popup.NoAutoClose : (Popup.CloseOnEscape | Popup.CloseOnPressOutside)

    readonly property var bags: BagService.bags.filter(function(bag) { return String(bag.kind || "") !== "tea" })
    property var profileTitles: []
    property double bagId: -1
    property string profileTitle: ""
    property var suggestion: ({})
    property bool suggestionPending: false
    property string _pendingSuggestionId: ""
    // Which choice list is open in place: "", "bean" or "profile".
    property string expanded: ""

    // Apply runs bag activation, then profile load, then the grind write.
    property bool applying: false
    property double _pendingBagId: -1
    property string _pendingProfileRequest: ""
    property string _pendingGrindRequest: ""
    property bool _grindRetried: false
    property string _applySetting: ""
    property int _applyRpm: 0

    readonly property var selectedBag: {
        for (let i = 0; i < root.bags.length; ++i) {
            if (Number(root.bags[i].id) === root.bagId) return root.bags[i]
        }
        return null
    }
    // Everything the suggestion depends on; a saved, rated or deleted shot moves
    // historyRevision. Compared by value, so the replica's other notifications pass.
    readonly property string requestKey: {
        const bag = root.selectedBag
        return root.visible + "|" + EquipmentService.ready + "|" + EquipmentService.activePackageId + "|"
            + (bag ? bag.id + "|" + bag.roasterName + "|" + bag.coffeeName + "|" + bag.roastDate : "")
            + "|" + root.profileTitle + "|" + HistoryService.historyRevision
    }
    onRequestKeyChanged: if (root.visible) root.requestSuggestion()
    // No active bean, or it is gone: start from the most recently used one. Tested on
    // the new list itself: selectedBag may not have re-evaluated yet.
    onBagsChanged: if (root.bags.length > 0 && !root.bags.some(b => Number(b.id) === root.bagId))
                       root.bagId = Number(root.bags[0].id)

    readonly property string suggestedSetting: String(root.suggestion.setting || "")
    readonly property double _startShotId: root.suggestedSetting.length > 0
        ? Number(root.suggestion.startShotId || 0) : 0

    function tr(key: string, fallback: string): string {
        return TranslationManager.translate(key, fallback)
    }

    // Tab order (ACCESSIBILITY.md Rule 3): a closed loop over whatever is shown. The
    // choice list is one stop; the arrow keys move within it.
    function focusOrder(): var {
        return [sourceArea, beanButton, profileButton, choiceList, cancelButton, applyButton]
            .filter(function(item) { return item.visible && item.enabled })
    }

    function moveFocus(from: Item, step: int): void {
        const order = root.focusOrder()
        if (order.length === 0) return
        const at = order.indexOf(from)
        order[(at + step + order.length) % order.length].forceActiveFocus(Qt.TabFocusReason)
    }

    onOpened: {
        const order = root.focusOrder()
        if (order.length > 0) order[0].forceActiveFocus()
    }

    function chartedProfileTitles(): var {
        const profiles = ProfileService.availableProfiles
        const out = []
        const seen = {}
        for (let i = 0; i < profiles.length; ++i) {
            const title = String(profiles[i].title || "")
            if (title.length === 0 || seen[title]) continue
            // A profile on the chart is the "same" as itself; a filter profile has a
            // direction to espresso profiles but no UGS, so it is not.
            if (ProfileService.grindDirectionBetween(title, title) !== "same" && title !== root.profileTitle)
                continue
            seen[title] = true
            out.push(title)
        }
        out.sort(function(a, b) { return a.localeCompare(b) })
        return out
    }

    function openFor(currentBagId: double, currentProfileTitle: string): void {
        root.bagId = currentBagId
        root.profileTitle = currentProfileTitle
        root.profileTitles = root.chartedProfileTitles()
        root.applying = false
        root.expanded = ""
        if (!root.selectedBag && root.bags.length > 0) root.bagId = Number(root.bags[0].id)
        root.open()
    }

    function requestSuggestion(): void {
        root.suggestion = ({})
        root._pendingSuggestionId = ""
        const bag = root.selectedBag
        const packageId = EquipmentService.activePackageId
        if (!bag || root.profileTitle.length === 0 || packageId <= 0 || !EquipmentService.ready) {
            root.suggestionPending = false
            return
        }
        root.suggestionPending = true
        root._pendingSuggestionId = EquipmentService.requestGrindSuggestion(
            packageId, String(bag.roasterName || ""), String(bag.coffeeName || ""),
            String(bag.roastDate || ""), root.profileTitle)
    }

    // The line naming the shot the number rests on (spec: "name its starting shot").
    function sourceText(): string {
        const s = root.suggestion
        const when = DateUtils.shotTimeLabel(Number(s.startTimestamp || 0), Qt.locale(),
                                             Settings.app.use12HourTime)
        return GrindSuggestionText.sourceLine(s, root.profileTitle, when, root.tr)
    }

    // The move in UGS, the way the UGS calculator shows it without a key.
    function ugsMoveText(): string {
        const s = root.suggestion
        if (s.ugsDelta === undefined || !(Number(s.startShotId || 0) > 0)) return ""
        const profile = String(s.startProfileName || "")
        const setting = String(s.startShotSetting || "")
        if (s.ugsDirection === "same")
            return root.tr("geniusGrind.ugs.same", "%1 sits at the same UGS as your %2 shot at %3.")
                       .arg(root.profileTitle).arg(profile).arg(setting)
        const amount = Math.round(Number(s.ugsDelta) * 100) / 100
        return (s.ugsDirection === "coarser"
            ? root.tr("geniusGrind.ugs.coarser", "%1 is %2 UGS coarser than your %3 shot at %4.")
            : root.tr("geniusGrind.ugs.finer", "%1 is %2 UGS finer than your %3 shot at %4."))
            .arg(root.profileTitle).arg(amount).arg(profile).arg(setting)
    }

    function statusText(): string {
        if (root.suggestionPending)
            return root.tr("geniusGrind.working", "Working it out…")
        if (EquipmentService.activePackageId <= 0)
            return root.tr("geniusGrind.noEquipment", "Set up your grinder under Equipment first.")
        if (!root.selectedBag)
            return root.tr("geniusGrind.noBean", "Choose a bean.")
        if (root.suggestedSetting.length > 0)
            return root.sourceText()
        if (root.suggestion.error !== undefined)
            return root.tr("geniusGrind.reason.unreachable", "Could not read your shots. Try again in a moment.")
        switch (String(root.suggestion.reason || "")) {
        case "noBean":
            return root.tr("geniusGrind.reason.noBean", "Name the current coffee first: its bag has no roaster or coffee name.")
        case "noShots":
            return root.tr("geniusGrind.reason.noShots", "No shots of this coffee on this equipment yet.")
        case "noStart":
            return root.tr("geniusGrind.reason.noStart", "No good shot of this coffee yet. Shots rated under 50 are not used.")
        case "profileNotOnChart":
            return root.tr("geniusGrind.reason.profileNotOnChart", "This profile is not on the UGS chart.")
        case "notCalibrated":
            return root.tr("geniusGrind.reason.notCalibrated",
                           "Your grinder is not calibrated yet. Pull one coffee on two profiles, or mark a fine and a coarse anchor.")
                   + " " + root.ugsMoveText()
        case "outsideRange":
            return root.tr("geniusGrind.reason.outsideRange", "This profile is outside the range your shots cover.")
                   + " " + root.ugsMoveText()
        default:
            return root.tr("geniusGrind.reason.unavailable", "No suggestion for this coffee and profile.")
        }
    }

    function openShot(shotId: double): void {
        if (shotId <= 0 || root.applying) return
        root.close()
        AppShell.shotDetailRequested(shotId, [shotId])
    }

    function startApply(): void {
        if (root.suggestedSetting.length === 0 || !root.selectedBag) return
        root.applying = true
        root.expanded = ""
        root._grindRetried = false
        root._applySetting = root.suggestedSetting
        root._applyRpm = Number(root.suggestion.startRpm) || 0
        if (root.bagId !== BagService.activeBagId) {
            root._pendingBagId = root.bagId
            BagService.requestActivateBag(root.bagId)
        } else {
            root.applyProfile()
        }
    }

    function applyProfile(): void {
        if (root.profileTitle !== String(ProfileService.currentProfileSummary.title || "")) {
            const filename = ProfileService.findProfileByTitle(root.profileTitle)
            if (filename.length === 0) {
                root.failApply("profile", "profileNotFound")
                return
            }
            root._pendingProfileRequest = ProfileService.loadProfile(filename)
            return
        }
        root.applyGrind()
    }

    function applyGrind(): void {
        if (!RecipeContext.ready) {
            root.failApply("grind", "unavailable")
            return
        }
        const values = { "dyeGrinderSetting": root._applySetting }
        // A setting at another RPM is a different grind: carry the shot's RPM with it.
        if (root._applyRpm > 0)
            values.dyeGrinderRpm = root._applyRpm
        root._pendingGrindRequest = RecipeContext.newRequestId()
        RecipeContext.setPreference(root._pendingGrindRequest, "grindDialIn", values, RecipeContext.revision)
    }

    function failApply(step: string, reason: string): void {
        WebDebugLogger.warn("Equipment", "GeniusGrindDialog",
                            "Apply failed at the " + step + " step: " + reason)
        root.applying = false
        root._pendingBagId = -1
        root._pendingProfileRequest = ""
        root._pendingGrindRequest = ""
        HomePresetActions.failed(root.tr("idle.operationFailed", "The selection could not be changed. Please try again."))
    }

    onClosed: {
        root.applying = false
        root._pendingBagId = -1
        root._pendingProfileRequest = ""
        root._pendingGrindRequest = ""
        root._pendingSuggestionId = ""
        root.suggestionPending = false
    }

    Connections {
        target: BagService
        function onBagActivated(bagId, success, bag, reason) {
            if (!root.applying || bagId !== root._pendingBagId) return
            root._pendingBagId = -1
            if (success) root.applyProfile()
            else root.failApply("bean", String(reason || "unknown"))
        }
    }

    Connections {
        target: ProfileService
        function onOperationCompleted(requestId, operation, filename, success, reason, result) {
            if (!root.applying || requestId !== root._pendingProfileRequest) return
            root._pendingProfileRequest = ""
            if (success) root.applyGrind()
            else root.failApply("profile", String(reason || "unknown"))
        }
    }

    Connections {
        target: RecipeContext
        function onPreferenceCompleted(requestId, name, success, preferences, reason) {
            if (!root.applying || requestId !== root._pendingGrindRequest) return
            root._pendingGrindRequest = ""
            // A bag switch reports done before the bag's row lands and moves the
            // preferences revision (SettingsDye::setActiveBagId → requestBag). The write
            // is absolute, and the source refreshes the revision before it refuses.
            if (!success && reason === "stalePreferences" && !root._grindRetried) {
                root._grindRetried = true
                root.applyGrind()
                return
            }
            root.applying = false
            if (success) root.close()
            else root.failApply("grind", String(reason || "unknown"))
        }
    }

    Connections {
        target: EquipmentService
        function onGrindSuggestionReady(suggestion) {
            if (suggestion.requestId !== root._pendingSuggestionId) return
            root._pendingSuggestionId = ""
            root.suggestionPending = false
            root.suggestion = suggestion
        }
        function onGrindCalibrationChanged(packageId) {
            if (root.visible && packageId === EquipmentService.activePackageId) root.requestSuggestion()
        }
    }

    background: Rectangle {
        color: Theme.surfaceColor
        radius: Theme.cardRadius
        border.width: 1
        border.color: Theme.primaryContrastColor
    }

    contentItem: ColumnLayout {
        spacing: Theme.spacingMedium

        Text {
            Layout.fillWidth: true
            Layout.topMargin: Theme.scaled(16)
            Layout.leftMargin: Theme.scaled(20)
            Layout.rightMargin: Theme.scaled(20)
            text: root.tr("geniusGrind.title", "Genius Grind")
            font: Theme.titleFont
            color: Theme.textColor
        }

        Text {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            text: root.suggestedSetting.length > 0 ? root.suggestedSetting : "—"
            font: Theme.valueFont
            color: root.suggestedSetting.length > 0 ? Theme.primaryColor : Theme.textSecondaryColor
            Accessible.role: Accessible.StaticText
            Accessible.name: root.tr("geniusGrind.suggestedGrind", "Suggested grind")
                             + ": " + (root.suggestedSetting.length > 0 ? root.suggestedSetting : root.statusText())
        }

        // The shot the number rests on; tapping it opens that shot.
        Text {
            id: sourceLine
            Layout.fillWidth: true
            Layout.leftMargin: Theme.scaled(20)
            Layout.rightMargin: Theme.scaled(20)
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            visible: text.length > 0
            text: root.statusText()
            font: Theme.bodyFont
            color: Theme.textSecondaryColor
            // Read once: by the tap area when there is a shot to open, by the number's
            // name when there is no number, else as text.
            Accessible.ignored: sourceArea.visible || root.suggestedSetting.length === 0

            AccessibleMouseArea {
                id: sourceArea
                anchors.fill: parent
                visible: root._startShotId > 0
                enabled: !root.applying
                accessibleName: sourceLine.text
                onAccessibleClicked: root.openShot(root._startShotId)
                Keys.onReturnPressed: root.openShot(root._startShotId)
                Keys.onSpacePressed: root.openShot(root._startShotId)
                Keys.onTabPressed: root.moveFocus(sourceArea, 1)
                Keys.onBacktabPressed: root.moveFocus(sourceArea, -1)
            }
        }

        GridLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.scaled(20)
            Layout.rightMargin: Theme.scaled(20)
            columns: 2
            columnSpacing: Theme.spacingMedium
            rowSpacing: Theme.scaled(8)

            Text {
                text: root.tr("geniusGrind.bean", "Bean")
                font: Theme.labelFont
                color: Theme.textSecondaryColor
            }
            AccessibleButton {
                id: beanButton
                Layout.fillWidth: true
                Keys.onTabPressed: root.moveFocus(beanButton, 1)
                Keys.onBacktabPressed: root.moveFocus(beanButton, -1)
                text: root.selectedBag ? BagName.fullName(root.selectedBag)
                                       : root.tr("geniusGrind.chooseBean", "Choose a bean")
                labelElide: Text.ElideRight
                accessibleName: root.tr("geniusGrind.changeBean", "Change bean") + ": " + text
                enabled: !root.applying
                onClicked: root.expanded = root.expanded === "bean" ? "" : "bean"
            }

            Text {
                text: root.tr("geniusGrind.profile", "Profile")
                font: Theme.labelFont
                color: Theme.textSecondaryColor
            }
            AccessibleButton {
                id: profileButton
                Layout.fillWidth: true
                Keys.onTabPressed: root.moveFocus(profileButton, 1)
                Keys.onBacktabPressed: root.moveFocus(profileButton, -1)
                text: root.profileTitle.length > 0 ? root.profileTitle
                                                   : root.tr("geniusGrind.chooseProfile", "Choose a profile")
                labelElide: Text.ElideRight
                accessibleName: root.tr("geniusGrind.changeProfile", "Change profile") + ": " + text
                enabled: !root.applying
                onClicked: root.expanded = root.expanded === "profile" ? "" : "profile"
            }
        }

        // The choices open in place: a picker popup over this popup did not reach
        // the screen on a Windows build.
        ListView {
            id: choiceList
            Layout.fillWidth: true
            Layout.leftMargin: Theme.scaled(20)
            Layout.rightMargin: Theme.scaled(20)
            Layout.preferredHeight: Math.min(contentHeight, Theme.scaled(260))
            Layout.fillHeight: true
            Layout.minimumHeight: Theme.scaled(88)
            visible: root.expanded !== ""
            clip: true
            spacing: Theme.scaled(4)
            Keys.onTabPressed: root.moveFocus(choiceList, 1)
            Keys.onBacktabPressed: root.moveFocus(choiceList, -1)
            model: root.expanded === "bean" ? root.bags
                 : root.expanded === "profile" ? root.profileTitles : []
            delegate: AccessibleButton {
                id: choice
                required property var modelData
                width: choiceList.width
                readonly property bool isBean: root.expanded === "bean"
                readonly property bool isCurrent: choice.isBean
                    ? Number(choice.modelData.id) === root.bagId
                    : String(choice.modelData) === root.profileTitle
                text: choice.isBean ? BagName.fullName(choice.modelData) : String(choice.modelData)
                labelElide: Text.ElideRight
                accessibleName: choice.text
                accessibleDescription: choice.isCurrent ? root.tr("accessibility.selected", "selected") : ""
                primary: choice.isCurrent
                // The list's current row holds its focus, so Up/Down (the list's own
                // key navigation) move the focus and scroll it into view.
                focus: choice.ListView.isCurrentItem
                Keys.onTabPressed: root.moveFocus(choiceList, 1)
                Keys.onBacktabPressed: root.moveFocus(choiceList, -1)
                onClicked: {
                    if (choice.isBean) root.bagId = Number(choice.modelData.id)
                    else root.profileTitle = String(choice.modelData)
                    root.expanded = ""
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.scaled(20)
            Layout.rightMargin: Theme.scaled(20)
            Layout.bottomMargin: Theme.scaled(20)
            spacing: Theme.spacingMedium

            AccessibleButton {
                id: cancelButton
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.scaled(50)
                Keys.onTabPressed: root.moveFocus(cancelButton, 1)
                Keys.onBacktabPressed: root.moveFocus(cancelButton, -1)
                text: root.tr("common.button.cancel", "Cancel")
                accessibleName: root.tr("geniusGrind.cancel", "Cancel without changes")
                enabled: !root.applying
                onClicked: root.close()
            }
            AccessibleButton {
                id: applyButton
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.scaled(50)
                Keys.onTabPressed: root.moveFocus(applyButton, 1)
                Keys.onBacktabPressed: root.moveFocus(applyButton, -1)
                primary: true
                enabled: root.suggestedSetting.length > 0 && !root.applying
                text: root.tr("geniusGrind.apply", "Apply")
                accessibleName: root.tr("geniusGrind.applyDescription", "Apply this bean, profile and grind")
                onClicked: root.startApply()
            }
        }
    }
}
