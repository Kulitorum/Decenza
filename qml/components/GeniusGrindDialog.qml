pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decenza
import "DateUtils.js" as DateUtils

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
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    property var bags: []
    property var profileTitles: []
    property double bagId: -1
    property string profileTitle: ""
    property var suggestion: ({})
    property bool suggestionPending: false
    // Which choice list is open in place: "", "bean" or "profile".
    property string expanded: ""
    // The user picked the other reading (the latest shot) over the suggestion.
    property bool useAlternative: false

    // Apply runs bag activation, then profile load, then the grind write.
    property bool applying: false
    property double _pendingBagId: -1
    property string _pendingProfileRequest: ""
    property string _pendingGrindRequest: ""
    property string _applySetting: ""
    property int _applyRpm: 0

    readonly property var selectedBag: {
        for (let i = 0; i < root.bags.length; ++i) {
            if (Number(root.bags[i].id) === root.bagId) return root.bags[i]
        }
        return null
    }
    readonly property string suggestedSetting: String(root.suggestion.setting || "")
    readonly property string alternativeSetting: String(root.suggestion.altSetting || "")
    // The other reading earns a place only when it is at least one grind step away.
    readonly property bool showAlternative: {
        if (root.suggestedSetting.length === 0 || root.alternativeSetting.length === 0) return false
        const a = parseFloat(root.suggestedSetting), b = parseFloat(root.alternativeSetting)
        if (isNaN(a) || isNaN(b)) return root.suggestedSetting !== root.alternativeSetting
        return Math.abs(a - b) >= Number(root.suggestion.grindStep || 1) - 1e-9
    }
    readonly property bool _alternativeChosen: root.showAlternative && root.useAlternative
    readonly property string chosenSetting: root._alternativeChosen ? root.alternativeSetting : root.suggestedSetting
    readonly property int chosenRpm: Number(root._alternativeChosen ? root.suggestion.altRpm : root.suggestion.startRpm) || 0
    // The source line describes the chosen reading; the note under the choice, the other.
    readonly property string _chosenPrefix: root._alternativeChosen ? "alt" : "start"
    readonly property string _otherPrefix: root._alternativeChosen ? "start" : "alt"

    function bagLabel(bag: var): string {
        if (!bag) return ""
        const coffee = String(bag.coffeeName || "")
        const roaster = String(bag.roasterName || "")
        if (coffee.length > 0 && roaster.length > 0) return roaster + " · " + coffee
        return coffee.length > 0 ? coffee : roaster
    }

    function chartedProfileTitles(): var {
        const directions = ProfileService.grindDirections
        const profiles = ProfileService.availableProfiles
        const out = []
        const seen = {}
        for (let i = 0; i < profiles.length; ++i) {
            const title = String(profiles[i].title || "")
            if (title.length === 0 || seen[title]) continue
            // A profile on the chart is the "same" as itself; a filter profile has a
            // direction to espresso profiles but no UGS, so it is not.
            const charted = directions[title] !== undefined && directions[title][title] === "same"
            if (!charted && title !== root.profileTitle) continue
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
        root.suggestion = ({})
        root.applying = false
        root.expanded = ""
        BagService.requestInventory()
        root.open()
        root.requestSuggestion()
    }

    function requestSuggestion(): void {
        root.suggestion = ({})
        root.useAlternative = false
        const bag = root.selectedBag
        const packageId = EquipmentService.activePackageId
        if (!bag || root.profileTitle.length === 0 || packageId <= 0 || !EquipmentService.ready) {
            root.suggestionPending = false
            return
        }
        root.suggestionPending = true
        EquipmentService.requestGrindSuggestion(packageId, String(bag.roasterName || ""),
                                                String(bag.coffeeName || ""),
                                                String(bag.roastDate || ""), root.profileTitle)
    }

    function shotWhen(prefix: string): string {
        return DateUtils.shotTimeLabel(Number(root.suggestion[prefix + "Timestamp"] || 0), Qt.locale(),
                                       Settings.app.use12HourTime)
    }

    // The line naming the shot the number rests on (spec: "name its starting shot").
    function sourceText(): string {
        const s = root.suggestion
        const p = root._chosenPrefix
        // The latest-shot reading is the bag's own history, or derived from it.
        const source = root._alternativeChosen
            ? (String(s.source || "") === "derived" ? "derived" : "history")
            : String(s.source || "")
        const profile = String(s[p + "ProfileName"] || "")
        const setting = String(root._alternativeChosen ? s.altShotSetting || "" : s.startSetting || "")
        const when = root.shotWhen(p)
        const rating = Number(s[p + "Rating"] || 0)
        const earlier = s[p + "EarlierBag"] === true
            ? " " + TranslationManager.translate("geniusGrind.source.earlierBag", "(from an earlier bag of this coffee)")
            : ""
        switch (source) {
        case "rated":
            return TranslationManager.translate("geniusGrind.source.rated",
                       "Your %1 shot of %2, rated %3, at %4.").arg(profile).arg(when).arg(rating).arg(setting) + earlier
        case "history":
            return TranslationManager.translate("geniusGrind.source.history",
                       "Your latest %1 shot of this coffee, %2, at %3.").arg(profile).arg(when).arg(setting) + earlier
        case "sameUgs":
            return TranslationManager.translate("geniusGrind.source.sameUgs",
                       "Your %1 shot of %2, rated %3, at %4. It sits at the same UGS as %5.")
                       .arg(profile).arg(when).arg(rating).arg(setting).arg(root.profileTitle) + earlier
        default:
            return (rating > 0
                ? TranslationManager.translate("geniusGrind.source.derivedRated",
                      "Estimated from your %1 shot of %2, rated %3, at %4, using your grinder calibration.")
                      .arg(profile).arg(when).arg(rating).arg(setting)
                : TranslationManager.translate("geniusGrind.source.derived",
                      "Estimated from your %1 shot of %2, at %3, using your grinder calibration.")
                      .arg(profile).arg(when).arg(setting)) + earlier
        }
    }

    // The note under the other reading: the facts behind it, no verdict.
    function alternativeText(): string {
        const s = root.suggestion
        if (root._alternativeChosen)
            return TranslationManager.translate("geniusGrind.alternative.ratedNote",
                       "Or %1, from your %2 shot of %3, rated %4, at %5. It was %6 days before your latest shot.")
                       .arg(root.suggestedSetting).arg(String(s.startProfileName || "")).arg(root.shotWhen("start"))
                       .arg(Number(s.startRating || 0)).arg(String(s.startSetting || "")).arg(Number(s.daysBetween || 0))
        return TranslationManager.translate("geniusGrind.alternative.note",
                   "Or %1, from your latest %2 shot, %3, at %4. Your rated shot was %5 days before it.")
                   .arg(root.alternativeSetting).arg(String(s.altProfileName || "")).arg(root.shotWhen("alt"))
                   .arg(String(s.altShotSetting || "")).arg(Number(s.daysBetween || 0))
    }

    // The move in UGS, the way the UGS calculator shows it without a key.
    function ugsMoveText(): string {
        const s = root.suggestion
        if (s.ugsDelta === undefined || !(Number(s.startShotId || 0) > 0)) return ""
        const profile = String(s.startProfileName || "")
        const setting = String(s.startSetting || "")
        if (s.ugsDirection === "same")
            return TranslationManager.translate("geniusGrind.ugs.same",
                       "%1 sits at the same UGS as your %2 shot at %3.").arg(root.profileTitle).arg(profile).arg(setting)
        const amount = Math.round(Number(s.ugsDelta) * 100) / 100
        return (s.ugsDirection === "coarser"
            ? TranslationManager.translate("geniusGrind.ugs.coarser",
                  "%1 is %2 UGS coarser than your %3 shot at %4.")
            : TranslationManager.translate("geniusGrind.ugs.finer",
                  "%1 is %2 UGS finer than your %3 shot at %4."))
            .arg(root.profileTitle).arg(amount).arg(profile).arg(setting)
    }

    function statusText(): string {
        if (root.suggestionPending)
            return TranslationManager.translate("geniusGrind.working", "Working it out…")
        if (EquipmentService.activePackageId <= 0)
            return TranslationManager.translate("geniusGrind.noEquipment", "Set up your grinder under Equipment first.")
        if (!root.selectedBag)
            return TranslationManager.translate("geniusGrind.noBean", "Choose a bean.")
        if (root.suggestedSetting.length > 0)
            return root.sourceText()
        switch (String(root.suggestion.reason || "")) {
        case "noBean":
            return TranslationManager.translate("geniusGrind.reason.noBean",
                       "Name the current coffee first: its bag has no roaster or coffee name.")
        case "noShots":
            return TranslationManager.translate("geniusGrind.reason.noShots",
                       "No shots of this coffee on this equipment yet.")
        case "noStart":
            return TranslationManager.translate("geniusGrind.reason.noStart",
                       "No good shot of this coffee yet. Shots rated under 50 are not used.")
        case "profileNotOnChart":
            return TranslationManager.translate("geniusGrind.reason.profileNotOnChart",
                       "This profile is not on the UGS chart.")
        case "notCalibrated":
            return TranslationManager.translate("geniusGrind.reason.notCalibrated",
                       "Your grinder is not calibrated yet. Pull one coffee on two profiles, or mark a fine and a coarse anchor.")
                   + " " + root.ugsMoveText()
        case "outsideRange":
            return TranslationManager.translate("geniusGrind.reason.outsideRange",
                       "This profile is outside the range your shots cover.") + " " + root.ugsMoveText()
        default:
            return TranslationManager.translate("geniusGrind.reason.unavailable",
                       "No suggestion for this coffee and profile.")
        }
    }

    function openShot(shotId: int): void {
        if (shotId <= 0) return
        root.close()
        AppShell.shotDetailRequested(shotId, [shotId])
    }

    function startApply(): void {
        if (root.chosenSetting.length === 0 || !root.selectedBag) return
        root.applying = true
        root._applySetting = root.chosenSetting
        root._applyRpm = root.chosenRpm
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
            if (root._pendingProfileRequest.length === 0)
                root.failApply("profile", "notSent")
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
        HomePresetActions.failed(TranslationManager.translate(
            "idle.operationFailed", "The selection could not be changed. Please try again."))
    }

    onClosed: {
        root.applying = false
        root._pendingBagId = -1
        root._pendingProfileRequest = ""
        root._pendingGrindRequest = ""
    }

    Connections {
        target: BagService
        function onInventoryReady(bags) {
            root.bags = bags.filter(function(bag) { return String(bag.kind || "") !== "tea" })
            // No active bean, or it is gone: start from the most recently used one.
            if (!root.selectedBag && root.bags.length > 0) root.bagId = Number(root.bags[0].id)
            if (root.visible && !root.suggestionPending && root.suggestedSetting.length === 0)
                root.requestSuggestion()
        }
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
            root.applying = false
            if (success) root.close()
            else root.failApply("grind", String(reason || "unknown"))
        }
    }

    Connections {
        target: EquipmentService
        function onGrindSuggestionReady(suggestion) {
            const bag = root.selectedBag
            if (!bag || suggestion.packageId !== EquipmentService.activePackageId
                || suggestion.beanBrand !== String(bag.roasterName || "")
                || suggestion.beanType !== String(bag.coffeeName || "")
                || suggestion.roastDate !== String(bag.roastDate || "")
                || suggestion.profileTitle !== root.profileTitle)
                return
            root.suggestionPending = false
            root.suggestion = suggestion
        }
        function onGrindCalibrationChanged(packageId) {
            if (root.visible) root.requestSuggestion()
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
            text: root.chosenSetting.length > 0 ? root.chosenSetting : "—"
            font.family: Theme.bodyFont.family
            font.pixelSize: Theme.scaled(56)
            font.bold: true
            color: root.chosenSetting.length > 0 ? Theme.primaryColor : Theme.textSecondaryColor
            Accessible.role: Accessible.StaticText
            Accessible.name: TranslationManager.translate("geniusGrind.suggestedGrind", "Suggested grind")
                             + ": " + (root.chosenSetting.length > 0 ? root.chosenSetting : root.statusText())
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

            AccessibleMouseArea {
                anchors.fill: parent
                enabled: root.suggestedSetting.length > 0 && Number(root.suggestion[root._chosenPrefix + "ShotId"] || 0) > 0
                accessibleName: sourceLine.text
                onAccessibleClicked: root.openShot(Number(root.suggestion[root._chosenPrefix + "ShotId"]))
            }
        }

        // The other reading, when the rated shot and the latest shot disagree.
        ColumnLayout {
            visible: root.showAlternative
            Layout.fillWidth: true
            Layout.leftMargin: Theme.scaled(20)
            Layout.rightMargin: Theme.scaled(20)
            spacing: Theme.scaled(6)

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingMedium
                AccessibleButton {
                    Layout.fillWidth: true
                    text: root.suggestedSetting
                    primary: !root.useAlternative
                    accessibleName: TranslationManager.translate("geniusGrind.alternative.useRated",
                                        "Use %1, from your rated shot").arg(root.suggestedSetting)
                    enabled: !root.applying
                    onClicked: root.useAlternative = false
                }
                AccessibleButton {
                    Layout.fillWidth: true
                    text: root.alternativeSetting
                    primary: root.useAlternative
                    accessibleName: TranslationManager.translate("geniusGrind.alternative.useLatest",
                                        "Use %1, from your latest shot").arg(root.alternativeSetting)
                    enabled: !root.applying
                    onClicked: root.useAlternative = true
                }
            }
            Text {
                id: alternativeLine
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                text: root.showAlternative ? root.alternativeText() : ""
                font: Theme.captionFont
                color: Theme.textSecondaryColor

                AccessibleMouseArea {
                    anchors.fill: parent
                    enabled: Number(root.suggestion[root._otherPrefix + "ShotId"] || 0) > 0
                    accessibleName: alternativeLine.text
                    onAccessibleClicked: root.openShot(Number(root.suggestion[root._otherPrefix + "ShotId"]))
                }
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
                Layout.fillWidth: true
                text: root.selectedBag ? root.bagLabel(root.selectedBag)
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
                Layout.fillWidth: true
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
                text: choice.isBean ? root.bagLabel(choice.modelData) : String(choice.modelData)
                labelElide: Text.ElideRight
                accessibleName: choice.text
                primary: choice.isCurrent
                onClicked: {
                    if (choice.isBean) root.bagId = Number(choice.modelData.id)
                    else root.profileTitle = String(choice.modelData)
                    root.expanded = ""
                    root.requestSuggestion()
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
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.scaled(50)
                text: TranslationManager.translate("common.button.cancel", "Cancel")
                accessibleName: TranslationManager.translate("geniusGrind.cancel", "Cancel without changes")
                onClicked: root.close()
            }
            AccessibleButton {
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.scaled(50)
                primary: true
                enabled: root.chosenSetting.length > 0 && !root.applying
                text: TranslationManager.translate("geniusGrind.apply", "Apply")
                accessibleName: TranslationManager.translate("geniusGrind.applyDescription",
                    "Apply this bean, profile and grind")
                onClicked: root.startApply()
            }
        }
    }
}
