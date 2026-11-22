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
// it rests on, with Apply to load both and set the grind. The number comes from
// GrindSuggestionRequest, the same one the widget and the recipe wizard show.
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
    // An Apply in flight cannot be called back, so the popup stays until it ends. An
    // operation page still closes it (PopupCloser calls close(), popupcloser.cpp:26), so
    // nothing resets the chain on close: it finishes behind the page.
    closePolicy: root.applying ? Popup.NoAutoClose : (Popup.CloseOnEscape | Popup.CloseOnPressOutside)

    readonly property var bags: BagService.bags.filter(function(bag) { return String(bag.kind || "") !== "tea" })
    property var profileTitles: []
    property double bagId: -1
    property string profileTitle: ""
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
    // Shown in place of the source line: a toast would sit under this popup's overlay.
    property string applyError: ""

    readonly property var selectedBag: {
        for (let i = 0; i < root.bags.length; ++i) {
            if (Number(root.bags[i].id) === root.bagId) return root.bags[i]
        }
        return null
    }
    readonly property GrindSuggestionRequest _request: GrindSuggestionRequest {
        active: root.visible && root.selectedBag !== null
        packageId: EquipmentService.activePackageId
        roaster: String(root.selectedBag ? root.selectedBag.roasterName || "" : "")
        coffee: String(root.selectedBag ? root.selectedBag.coffeeName || "" : "")
        roastDate: String(root.selectedBag ? root.selectedBag.roastDate || "" : "")
        profileTitle: root.profileTitle
    }
    readonly property var suggestion: root._request.suggestion
    onBagIdChanged: root.applyError = ""
    onProfileTitleChanged: root.applyError = ""
    // No active bean, or it is gone: start from the most recently used one. Tested on
    // the new list itself: selectedBag may not have re-evaluated yet.
    onBagsChanged: if (root.bags.length > 0 && !root.bags.some(b => Number(b.id) === root.bagId))
                       root.bagId = Number(root.bags[0].id)

    // Empty while a refresh is out, so Apply cannot send the number it replaces.
    readonly property string suggestedSetting: root._request.pending ? ""
                                                                     : String(root.suggestion.setting || "")
    // While an Apply error is shown, the line is that error, not a link to the shot.
    readonly property double _startShotId: root.suggestedSetting.length > 0 && root.applyError.length === 0
        ? Number(root.suggestion.startShotId || 0) : 0

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
        const next = order[(at + step + order.length) % order.length]
        const target = next === choiceList && choiceList.currentItem ? choiceList.currentItem : next
        target.forceActiveFocus(Qt.TabFocusReason)
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
        root.applyError = ""
        root.expanded = ""
        if (!root.selectedBag && root.bags.length > 0) root.bagId = Number(root.bags[0].id)
        root.open()
    }

    // The line naming the shot the number rests on (spec: "name its starting shot").
    function sourceText(): string {
        const s = root.suggestion
        const when = DateUtils.shotTimeLabel(Number(s.startTimestamp || 0), Qt.locale(),
                                             Settings.app.use12HourTime)
        return GrindSuggestionText.sourceLine(s, root.profileTitle, when, TranslationManager.translate)
    }

    // The move in UGS, the way the UGS calculator shows it without a key.
    function ugsMoveText(): string {
        const s = root.suggestion
        if (s.ugsDelta === undefined || !(Number(s.startShotId || 0) > 0)) return ""
        const profile = String(s.startProfileName || "")
        const setting = String(s.startShotSetting || "")
        if (s.ugsDirection === "same")
            return TranslationManager.translate("geniusGrind.ugs.same", "%1 sits at the same UGS as your %2 shot at %3.")
                       .arg(root.profileTitle).arg(profile).arg(setting)
        const amount = Math.round(Number(s.ugsDelta) * 100) / 100
        return (s.ugsDirection === "coarser"
            ? TranslationManager.translate("geniusGrind.ugs.coarser", "%1 is %2 UGS coarser than your %3 shot at %4.")
            : TranslationManager.translate("geniusGrind.ugs.finer", "%1 is %2 UGS finer than your %3 shot at %4."))
            .arg(root.profileTitle).arg(amount).arg(profile).arg(setting)
    }

    function withUgsMove(reason: string): string {
        const move = root.ugsMoveText()
        return move.length > 0 ? reason + " " + move : reason
    }

    function statusText(): string {
        if (root.applyError.length > 0)
            return root.applyError
        if (root._request.pending)
            return TranslationManager.translate("geniusGrind.working", "Working it out…")
        if (EquipmentService.activePackageId <= 0)
            return TranslationManager.translate("geniusGrind.noEquipment", "Set up your grinder under Equipment first.")
        if (!root.selectedBag)
            return TranslationManager.translate("geniusGrind.noBean", "Choose a bean.")
        if (root.suggestedSetting.length > 0)
            return root.sourceText()
        if (root.suggestion.error !== undefined)
            return TranslationManager.translate("geniusGrind.reason.unreachable", "Could not read your shots. Try again in a moment.")
        switch (String(root.suggestion.reason || "")) {
        case "noBean":
            return TranslationManager.translate("geniusGrind.reason.noBean", "Name the current coffee first: its bag has no roaster or coffee name.")
        case "noShots":
            return TranslationManager.translate("geniusGrind.reason.noShots", "No shots of this coffee on this equipment yet.")
        case "noStart":
            return TranslationManager.translate("geniusGrind.reason.noStart", "No good shot of this coffee yet. Shots rated under 50 are not used.")
        case "profileNotOnChart":
            return TranslationManager.translate("geniusGrind.reason.profileNotOnChart", "This profile is not on the UGS chart.")
        case "notCalibrated":
            return root.withUgsMove(TranslationManager.translate("geniusGrind.reason.notCalibrated",
                           "Your grinder is not calibrated yet. Pull one coffee on two profiles, or mark a fine and a coarse anchor."))
        case "outsideRange":
            return root.withUgsMove(TranslationManager.translate("geniusGrind.reason.outsideRange",
                                                                 "This profile is outside the range your shots cover."))
        default:
            return TranslationManager.translate("geniusGrind.reason.unavailable", "No suggestion for this coffee and profile.")
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
        root.applyError = ""
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
        root.applyError = TranslationManager.translate("idle.operationFailed",
                                                       "The selection could not be changed. Please try again.")
        AccessibilityManager.announce(root.applyError, true)
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
            text: TranslationManager.translate("geniusGrind.title", "Genius Grind")
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
            Accessible.name: TranslationManager.translate("geniusGrind.suggestedGrind", "Suggested grind")
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
                text: TranslationManager.translate("geniusGrind.bean", "Bean")
                font: Theme.labelFont
                color: Theme.textSecondaryColor
            }
            AccessibleButton {
                id: beanButton
                Layout.fillWidth: true
                Keys.onTabPressed: root.moveFocus(beanButton, 1)
                Keys.onBacktabPressed: root.moveFocus(beanButton, -1)
                text: root.selectedBag ? BagName.fullName(root.selectedBag)
                                       : TranslationManager.translate("geniusGrind.chooseBean", "Choose a bean")
                labelElide: Text.ElideRight
                accessibleName: TranslationManager.translate("geniusGrind.changeBean", "Change bean") + ": " + text
                enabled: !root.applying
                onClicked: root.expanded = root.expanded === "bean" ? "" : "bean"
            }

            Text {
                text: TranslationManager.translate("geniusGrind.profile", "Profile")
                font: Theme.labelFont
                color: Theme.textSecondaryColor
            }
            AccessibleButton {
                id: profileButton
                Layout.fillWidth: true
                Keys.onTabPressed: root.moveFocus(profileButton, 1)
                Keys.onBacktabPressed: root.moveFocus(profileButton, -1)
                text: root.profileTitle.length > 0 ? root.profileTitle
                                                   : TranslationManager.translate("geniusGrind.chooseProfile", "Choose a profile")
                labelElide: Text.ElideRight
                accessibleName: TranslationManager.translate("geniusGrind.changeProfile", "Change profile") + ": " + text
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
            // Up/Down are the list's own key navigation. The view focuses the new row
            // with no key reason (qquickitemview.cpp:1841), and a control draws its ring
            // only for one (qquickcontrol.cpp:1432-1436); a second request on a focused
            // item is ignored (qquickitem.cpp:8173), so drop the focus and retake it.
            onCurrentItemChanged: {
                if (!activeFocus || !currentItem) return
                currentItem.focus = false
                currentItem.forceActiveFocus(Qt.TabFocusReason)
            }
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
                accessibleDescription: choice.isCurrent ? TranslationManager.translate("accessibility.selected", "selected") : ""
                primary: choice.isCurrent
                Keys.onTabPressed: root.moveFocus(choiceList, 1)
                Keys.onBacktabPressed: root.moveFocus(choiceList, -1)
                onClicked: {
                    // Collapsing the list destroys this row; a keyboard pick keeps focus
                    // on the button that opened it.
                    if (choiceList.activeFocus) {
                        const opener = choice.isBean ? beanButton : profileButton
                        opener.forceActiveFocus(Qt.TabFocusReason)
                    }
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
                text: TranslationManager.translate("common.button.cancel", "Cancel")
                accessibleName: TranslationManager.translate("geniusGrind.cancel", "Cancel without changes")
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
                text: TranslationManager.translate("geniusGrind.apply", "Apply")
                accessibleName: TranslationManager.translate("geniusGrind.applyDescription", "Apply this bean, profile and grind")
                onClicked: root.startApply()
            }
        }
    }
}
