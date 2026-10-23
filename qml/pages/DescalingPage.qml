import QtQuick
import QtQuick.Controls
import QtQuick.Templates as T
import QtQuick.Layouts
import Decenza

T.Page {
    id: descalingPage
    // Declarative so it re-evaluates on a language change. This used to be an
    // imperative assignment in onCompleted/onActivated, which ran once and left
    // page titles in the previous language until you navigated away and back.
    readonly property string pageTitle: TranslationManager.translate("descaling.title", "Descaling")

    objectName: "descalingPage"
    background: ThemedPageBackground {}


    // Decent requires the steam heater OFF and the steam boiler below 60 °C before a
    // descale ("Disable the steam heater on the Steam page before using the descale
    // function. And wait till the steam temperature cools down to 60 °C or lower (It can
    // take an hour)" — Decent Espresso Machine Manual, 2.2 The DE1 Software, 5.
    // Maintenance & Cleaning). It is a precondition of every descale, not a preference,
    // so the page satisfies it on open rather than asking the user to.
    //
    // Opening the page is also the earliest moment the user has expressed intent, which
    // matters: the same manual notes that disabling right after waking the machine, before
    // preheating, is what keeps the wait short.
    readonly property real steamSafeTempC: 60
    readonly property real steamTempC: typeof MaintenanceService.snapshot.steamTemperature === 'number'
                                       ? MaintenanceService.snapshot.steamTemperature : 0
    readonly property bool steamTooHotToDescale: steamTempC > steamSafeTempC

    // Both start sites go through here so the hot-boiler check cannot be added to one and
    // forgotten on the other — the same trap the three maintenance states fell into in C++.
    // The view flags are cleared only on the path that actually starts: clearing them up
    // front lost the rinse instructions and the Done button when the confirmation was
    // declined, with no way back to them.
    function startDescaleChecked() {
        if (descalingPage.steamTooHotToDescale) {
            hotSteamConfirmDialog.open()
            return
        }
        descalingPage.beginDescaleRun(false)
    }

    function beginDescaleRun(allowHotSteam) {
        descalingPage.requestHostOperation("startDescale", {
            allowHotSteam: Boolean(allowHotSteam)
        })
    }

    // The host owns this cooling workflow, which can outlive the view while the
    // boiler cools or auto-sleep replaces the page.
    Component.onCompleted: {
        MaintenanceService.refresh()
        if (MaintenanceService.snapshot.descaleStopped === true)
            descalingPage.observedDescaleStopped = true
        descalingPage.ensureDescaleWorkflow()
        if (MaintenanceService.snapshot.descaleNeedsFinish)
            descalingPage.showRinseInstructions = true
        if (descalingPage.descaleStopped)
            descalingPage.showRinseInstructions = true
    }

    function ensureDescaleWorkflow() {
        if (MaintenanceService.snapshot.descaleWorkflowActive === true)
            descalingPage.observedHostWorkflowActive = true
        if (!MaintenanceService.ready || descalingPage.descaleWorkflowObserved) return
        descalingPage.descaleWorkflowObserved = true
        if (MaintenanceService.snapshot.descaleStopped === true) return
        if (!descalingPage.descaleWorkflowEnded && !MaintenanceService.snapshot.descaleWorkflowActive)
            descalingPage.requestHostOperation("beginDescaleHold")
    }

    function navigateBack() {
        AppShell.backRequested()
    }

    // focus: true alone is not enough — it marks the page as WANTING focus within its scope,
    // and without the grab the key never reaches this handler (nor the StackView's, which is
    // why Escape did nothing at all on macOS rather than popping the page). EspressoPage
    // pairs the two the same way.
    focus: true
    StackView.onActivated: descalingPage.forceActiveFocus()

    Keys.onReleased: function(event) {
        if (event.key === Qt.Key_Back || event.key === Qt.Key_Escape) {
            event.accepted = true
            descalingPage.navigateBack()
        }
    }

    Component.onDestruction: {
        // The host workflow, cooling hold and temporary profile survive view destruction.
    }

    property bool isDescaling: MaintenanceService.snapshot.phaseIsDescaling
    property bool wasDescaling: false
    property bool showRinseInstructions: false
    property int descaleCycleCount: 0
    property string pendingRequestId: ""
    property string pendingOperation: ""
    property string operationError: ""
    property bool descaleWorkflowEnded: false
    property bool descaleWorkflowObserved: false
    property bool observedHostWorkflowActive: false
    property bool observedDescaleStopped: false
    readonly property bool descaleStopped: MaintenanceService.snapshot.descaleStopped === true
    readonly property bool descaleHoldReady: MaintenanceService.snapshot.descaleWorkflowActive === true

    function requestHostOperation(operation, arguments) {
        if (operation === "finishDescale" || operation === "cancelDescale" || operation === "stopDescale")
            descaleWorkflowEnded = true
        var id = MaintenanceService.execute(operation, arguments || ({}))
        if (id !== "") {
            pendingRequestId = id
            pendingOperation = operation
            operationError = ""
        }
        return id
    }

    onIsDescalingChanged: {
        if (isDescaling) {
            wasDescaling = true
            descaleCycleCount++
        }
    }

    // Track when descaling completes to show rinse instructions
    Connections {
        target: MaintenanceService
        function onStateChanged() {
            descalingPage.ensureDescaleWorkflow()
            var state = MaintenanceService.snapshot
            if (state.descaleWorkflowActive === true) {
                descalingPage.observedHostWorkflowActive = true
            } else if (descalingPage.observedHostWorkflowActive && !descalingPage.descaleWorkflowEnded) {
                // Another client may stop the cycle while this page observes it.
                // Keep this page open for the host-published rinse-safety guidance;
                // other ended workflows no longer have actionable controls here.
                descalingPage.observedHostWorkflowActive = false
                if (state.descaleStopped !== true && descalingPage.visible)
                    AppShell.dismissRequested()
            }
            if (state.descaleInterrupted === true)
                descalingPage.wasDescaling = false
            if (state.descaleStopped === true) {
                descalingPage.observedDescaleStopped = true
                descalingPage.showRinseInstructions = true
                descalingPage.wasDescaling = false
            } else if (descalingPage.observedDescaleStopped) {
                descalingPage.observedDescaleStopped = false
                // Another client acknowledged Done. Close this observer page so
                // a cleared stop outcome cannot be mislabeled as natural completion.
                if (descalingPage.pendingOperation !== "finishDescale" && descalingPage.visible)
                    AppShell.dismissRequested()
            } else if (state.descaleNeedsFinish) {
                descalingPage.showRinseInstructions = true
                descalingPage.wasDescaling = false
            }
            if (descalingPage.wasDescaling && !descalingPage.isDescaling && descalingPage.visible) {
                if (descalingPage.pendingOperation === "stopDescale") {
                    // stopOperation() can publish a settled phase before its
                    // correlated completion; wait for the canonical stop result.
                    descalingPage.wasDescaling = false
                } else if (!state.descaleStopped && !state.descaleInterrupted
                        && (state.phaseIsIdle || state.phaseIsReady)) {
                    descalingPage.showRinseInstructions = true
                    descalingPage.wasDescaling = false
                }
            }
        }
        function onOperationCompleted(requestId, operation, success, result, reason, revision) {
            if (descalingPage.pendingRequestId === "" || requestId !== descalingPage.pendingRequestId
                    || operation !== descalingPage.pendingOperation) return
            descalingPage.pendingRequestId = ""
            descalingPage.pendingOperation = ""
            if (!success) {
                if (operation === "finishDescale" || operation === "cancelDescale" || operation === "stopDescale")
                    descalingPage.descaleWorkflowEnded = false
                descalingPage.operationError = reason || TranslationManager.translate("common.error", "The operation could not be started")
                if (operation === "stopDescale" && MaintenanceService.snapshot.descaleNeedsFinish) {
                    descalingPage.showRinseInstructions = true
                    descalingPage.wasDescaling = false
                }
                return
            }
            descalingPage.operationError = ""
            if (operation === "startDescale") {
                descalingPage.showRinseInstructions = false
                descalingPage.wasDescaling = false
            }
            if (operation === "stopDescale" && result.stopped === true) {
                descalingPage.showRinseInstructions = true
                descalingPage.wasDescaling = false
            }
            if (operation === "finishDescale") AppShell.dismissRequested()
            if (operation === "cancelDescale") AppShell.backRequested()
        }
    }

    // Get user-friendly substate description
    function getDescaleStepDescription(subState) {
        switch (subState) {
            case 8:  return TranslationManager.translate("descaling.step.init", "Initializing descale cycle...")
            case 9:  return TranslationManager.translate("descaling.step.fillGroup", "Filling group head with solution...")
            case 10: return TranslationManager.translate("descaling.step.return", "Circulating through boiler...")
            case 11: return TranslationManager.translate("descaling.step.group", "Flushing group head...")
            case 12: return TranslationManager.translate("descaling.step.steam", "Descaling steam system...")
            default: return TranslationManager.translate("descaling.step.running", "Descaling in progress...")
        }
    }

    // Progress comes from DE1Device, which derives it from the firmware's fixed step
    // schedule and resyncs at every substate boundary. It used to be computed here as
    // the substate's RANK — (subState - 8 + 1) / 5 — which read 100% from the moment
    // the final step began and stayed there for its whole 420 s: seven minutes of a
    // twelve-minute descale spent claiming the machine was finished.

    function formatRemaining(seconds) {
        if (seconds <= 0) return ""
        var mins = Math.floor(seconds / 60)
        var secs = seconds % 60
        return mins > 0
            ? TranslationManager.translate("descaling.remaining.minutes", "%1 min %2 s left")
                  .arg(mins).arg(secs)
            : TranslationManager.translate("descaling.remaining.seconds", "%1 s left").arg(secs)
    }

    ScrollView {
        anchors.fill: parent
        anchors.margins: Theme.standardMargin
        anchors.topMargin: Theme.pageTopMargin
        anchors.bottomMargin: Theme.scaled(80)
        contentWidth: availableWidth
        clip: true

        ColumnLayout {
            width: parent.width
            spacing: Theme.scaled(16)

            Text {
                Layout.fillWidth: true
                visible: descalingPage.operationError !== ""
                text: TranslationManager.translate("maintenance.operationFailed", "The machine could not start this maintenance action.")
                      + " (" + descalingPage.operationError + ")"
                color: Theme.errorColor
                font: Theme.captionFont
                wrapMode: Text.WordWrap
            }

            // === DESCALING IN PROGRESS VIEW ===
            Item {
                visible: descalingPage.isDescaling
                Layout.fillWidth: true
                Layout.fillHeight: true

                ColumnLayout {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: Math.min(Theme.scaled(500), parent.width - Theme.scaled(40))
                    spacing: Theme.scaled(20)

                    Item { Layout.preferredHeight: Theme.scaled(40) }

                    // Progress indicator
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: Theme.scaled(200)
                        color: Theme.cardBackgroundColor
                        radius: Theme.cardRadius

                        ColumnLayout {
                            anchors.centerIn: parent
                            spacing: Theme.scaled(16)

                            Tr {
                                Layout.alignment: Qt.AlignHCenter
                                key: "descaling.inprogress.title"
                                fallback: "Descaling in Progress"
                                font: Theme.titleFont
                                color: Theme.textColor
                            }

                            Text {
                                Layout.alignment: Qt.AlignHCenter
                                text: descalingPage.getDescaleStepDescription(MaintenanceService.snapshot.subState)
                                font: Theme.bodyFont
                                color: Theme.textSecondaryColor
                            }

                            // Progress bar (decorative — percentage text below provides the info)
                            Rectangle {
                                Layout.alignment: Qt.AlignHCenter
                                Layout.preferredWidth: Theme.scaled(300)
                                Layout.preferredHeight: Theme.scaled(12)
                                radius: Theme.scaled(6)
                                color: Theme.backgroundColor
                                Accessible.ignored: true

                                Rectangle {
                                    width: parent.width * MaintenanceService.snapshot.descaleProgress
                                    height: parent.height
                                    radius: Theme.scaled(6)
                                    color: Theme.primaryColor

                                    Behavior on width { NumberAnimation { duration: 300 } }
                                }
                            }

                            Text {
                                Layout.alignment: Qt.AlignHCenter
                                text: {
                                    var pct = Math.floor(MaintenanceService.snapshot.descaleProgress * 100) + "%"
                                    if (MaintenanceService.snapshot.descaleStepIndex <= 0) return pct
                                    var step = TranslationManager.translate("descaling.stepOf", "Step %1 of %2")
                                                   .arg(MaintenanceService.snapshot.descaleStepIndex)
                                                   .arg(MaintenanceService.snapshot.descaleStepCount)
                                    var remaining = descalingPage.formatRemaining(MaintenanceService.snapshot.descaleSecondsRemaining)
                                    return remaining !== "" ? pct + " · " + step + " · " + remaining
                                                            : pct + " · " + step
                                }
                                font: Theme.captionFont
                                color: Theme.textSecondaryColor
                            }
                        }
                    }

                    // Timer display
                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        text: MaintenanceService.snapshot.shotTime.toFixed(0) + "s"
                        font: Theme.timerFont
                        color: Theme.textColor
                    }

                    Tr {
                        Layout.alignment: Qt.AlignHCenter
                        key: "descaling.inprogress.dontstop"
                        fallback: "Do not stop the machine during descaling"
                        font: Theme.captionFont
                        color: Theme.warningColor
                    }

                    Item { Layout.fillHeight: true }

                    // Stop button (emergency only, for headless machines)
                    Rectangle {
                        id: descaleStopButton
                        Layout.alignment: Qt.AlignHCenter
                        Layout.preferredWidth: Theme.scaled(200)
                        Layout.preferredHeight: Theme.scaled(50)
                        visible: MaintenanceService.snapshot.isHeadless
                        radius: Theme.cardRadius
                        color: stopTapHandler.isPressed ? Qt.darker(Theme.errorColor, 1.2) : Theme.errorColor
                        border.color: Theme.primaryContrastColor
                        border.width: Theme.scaled(2)

                        Text {
                            anchors.centerIn: parent
                            text: TranslationManager.translate("descaling.button.stop", "STOP")
                            color: Theme.primaryContrastColor
                            font.pixelSize: Theme.scaled(18)
                            font.weight: Font.Bold
                            Accessible.ignored: true
                        }

                        AccessibleTapHandler {
                            id: stopTapHandler
                            anchors.fill: parent
                            accessibleName: TranslationManager.translate("descaling.accessible.emergencyStop", "Emergency stop descaling")
                            accessibleItem: descaleStopButton
                            onAccessibleClicked: descalingPage.requestHostOperation("stopDescale")
                        }
                    }

                    Item { Layout.preferredHeight: Theme.scaled(20) }
                }
            }

            // === RINSE INSTRUCTIONS VIEW ===
            ColumnLayout {
                visible: descalingPage.showRinseInstructions && !descalingPage.isDescaling
                Layout.fillWidth: true
                spacing: Theme.scaled(12)

                Rectangle {
                    id: rinseCard
                    Layout.fillWidth: true
                    implicitHeight: rinseContent.implicitHeight + Theme.scaled(32)
                    color: Theme.cardBackgroundColor
                    radius: Theme.cardRadius

                    ColumnLayout {
                        id: rinseContent
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: Theme.scaled(16)
                        spacing: Theme.scaled(12)

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.scaled(8)

                            Image {
                                source: descalingPage.descaleStopped
                                        ? "qrc:/icons/warning.svg" : "qrc:/emoji/2705.svg"
                                sourceSize.width: Theme.scaled(24)
                                sourceSize.height: Theme.scaled(24)
                                Accessible.ignored: true
                            }

                            Tr {
                                key: descalingPage.descaleStopped
                                     ? "descaling.rinse.stoppedTitle" : "descaling.rinse.title"
                                fallback: descalingPage.descaleStopped
                                          ? "Descale Stopped - Rinse Before Continuing"
                                          : "Descale Complete - Now Rinse!"
                                font: Theme.titleFont
                                color: descalingPage.descaleStopped ? Theme.warningColor : Theme.primaryColor
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: Theme.scaled(1)
                            color: Theme.textSecondaryColor
                            opacity: 0.3
                        }

                        Tr {
                            Layout.fillWidth: true
                            key: "descaling.rinse.important"
                            fallback: "IMPORTANT: Rinsing is critical! Citric acid remains in the water lines and must be flushed out completely."
                            font.pixelSize: Theme.bodyFont.pixelSize
                            font.bold: true
                            color: Theme.warningColor
                            wrapMode: Text.WordWrap
                            horizontalAlignment: Text.AlignHCenter
                        }

                        Tr {
                            Layout.fillWidth: true
                            key: "descaling.rinse.step1.title"
                            fallback: "1. Drain remaining acid solution"
                            font: Theme.subtitleFont
                            color: Theme.textColor
                            wrapMode: Text.WordWrap
                            horizontalAlignment: Text.AlignHCenter
                        }

                        Tr {
                            Layout.fillWidth: true
                            key: "descaling.rinse.step1.desc"
                            fallback: "Empty the water tank of any remaining acid solution. Rinse the tank thoroughly, then fill with fresh filtered water."
                            font: Theme.bodyFont
                            color: Theme.textSecondaryColor
                            wrapMode: Text.WordWrap
                            horizontalAlignment: Text.AlignHCenter
                        }

                        Tr {
                            Layout.fillWidth: true
                            key: "descaling.rinse.step2.title"
                            fallback: "2. Rinse the group head"
                            font: Theme.subtitleFont
                            color: Theme.textColor
                            wrapMode: Text.WordWrap
                            horizontalAlignment: Text.AlignHCenter
                        }

                        Tr {
                            Layout.fillWidth: true
                            key: "descaling.rinse.step2.desc"
                            fallback: "Run flush cycles until the app shows 'refill'. Refill and repeat. Taste the water - if it tastes acidic or smells like vitamin C, flush more. Citric acid smells like vitamin C capsules, so if you smell it but it's not sour, flush more anyway. Expect to use 4+ liters."
                            font: Theme.bodyFont
                            color: Theme.textSecondaryColor
                            wrapMode: Text.WordWrap
                            horizontalAlignment: Text.AlignHCenter
                        }

                        Tr {
                            Layout.fillWidth: true
                            key: "descaling.rinse.step3.title"
                            fallback: "3. Rinse the steam line"
                            font: Theme.subtitleFont
                            color: Theme.textColor
                            wrapMode: Text.WordWrap
                            horizontalAlignment: Text.AlignHCenter
                        }

                        Tr {
                            Layout.fillWidth: true
                            key: "descaling.rinse.step3.desc"
                            fallback: "Keep the steam tip removed. Run steam for 100 seconds, repeat 5 times. Taste the water after - if acidic, run more steam cycles."
                            font: Theme.bodyFont
                            color: Theme.textSecondaryColor
                            wrapMode: Text.WordWrap
                            horizontalAlignment: Text.AlignHCenter
                        }

                        Tr {
                            Layout.fillWidth: true
                            key: "descaling.rinse.total"
                            fallback: "Total water needed: approximately 7-8 liters for thorough rinsing."
                            font.pixelSize: Theme.captionFont.pixelSize
                            font.italic: true
                            color: Theme.textSecondaryColor
                            wrapMode: Text.WordWrap
                            horizontalAlignment: Text.AlignHCenter
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: Theme.scaled(1)
                            color: Theme.textSecondaryColor
                            opacity: 0.3
                        }

                        Tr {
                            Layout.fillWidth: true
                            key: "descaling.rinse.residual.title"
                            fallback: "Next-day follow-up"
                            font: Theme.subtitleFont
                            color: Theme.warningColor
                            wrapMode: Text.WordWrap
                            horizontalAlignment: Text.AlignHCenter
                        }

                        Tr {
                            Layout.fillWidth: true
                            key: "descaling.rinse.residual.desc"
                            fallback: "Even after thorough rinsing, residual acid may leach out overnight. Run a few flush cycles the next morning and taste the water. If it tastes acidic or appears yellow, flush more. This can continue for 2-3 days."
                            font: Theme.bodyFont
                            color: Theme.textSecondaryColor
                            wrapMode: Text.WordWrap
                            horizontalAlignment: Text.AlignHCenter
                        }
                    }
                }

                // Cycle counter. This counts how many times the PAGE saw the machine enter
                // Descale, i.e. how many times "Run Again" was used — not the firmware's
                // own repeats within one run, which are exposed in the maintenance snapshot. Two
                // different numbers, both real; this line means the first.
                Text {
                    Layout.alignment: Qt.AlignHCenter
                    text: descalingPage.descaleStopped
                          ? TranslationManager.translate("descaling.cycleCount.stopped", "Cycle %1 stopped").arg(descalingPage.descaleCycleCount)
                          : TranslationManager.translate("descaling.cycleCount", "Cycle %1 complete").arg(descalingPage.descaleCycleCount)
                    font: Theme.captionFont
                    color: Theme.textSecondaryColor
                }

                // Action buttons
                RowLayout {
                    Layout.alignment: Qt.AlignHCenter
                    spacing: Theme.scaled(12)

                    AccessibleButton {
                        Layout.preferredWidth: Theme.scaled(200)
                        Layout.preferredHeight: Theme.scaled(50)
                        text: TranslationManager.translate("descaling.button.runAgain", "Run Again")
                        accessibleName: TranslationManager.translate("descaling.button.runAgain.accessible", "Run descale cycle again")
                        _customFontSize: Theme.scaled(18)
                        _customFontWeight: Font.Bold
                        enabled: descalingPage.descaleHoldReady && descalingPage.pendingOperation !== "startDescale"
                        onClicked: descalingPage.startDescaleChecked()
                    }

                    AccessibleButton {
                        Layout.preferredWidth: Theme.scaled(200)
                        Layout.preferredHeight: Theme.scaled(50)
                        primary: true
                        text: TranslationManager.translate("descaling.button.done", "Done")
                        accessibleName: TranslationManager.translate("descaling.button.done", "Done")
                        _customFontSize: Theme.scaled(18)
                        _customFontWeight: Font.Bold
                        onClicked: {
                            descalingPage.requestHostOperation("finishDescale")
                        }
                    }
                }
            }

            // === PREPARATION VIEW ===
            ColumnLayout {
                visible: !descalingPage.isDescaling && !descalingPage.showRinseInstructions
                Layout.fillWidth: true
                spacing: Theme.scaled(12)

                // Warning banner
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: warningContent.implicitHeight + Theme.scaled(24)
                    color: Qt.rgba(Theme.warningColor.r, Theme.warningColor.g, Theme.warningColor.b, 0.15)
                    radius: Theme.cardRadius
                    border.color: Theme.warningColor
                    border.width: 1

                    ColumnLayout {
                        id: warningContent
                        anchors.fill: parent
                        anchors.margins: Theme.scaled(12)
                        spacing: Theme.scaled(8)

                        Tr {
                            Layout.fillWidth: true
                            key: "descaling.warning.title"
                            fallback: "Important Warnings"
                            font.pixelSize: Theme.subtitleFont.pixelSize
                            font.bold: true
                            color: Theme.warningColor
                        }

                        Tr {
                            Layout.fillWidth: true
                            key: "descaling.warning.citriconly"
                            fallback: "\u2022 Use CITRIC ACID ONLY - other descaling products can damage seals and void warranty"
                            font: Theme.bodyFont
                            color: Theme.textColor
                            wrapMode: Text.WordWrap
                        }

                        Tr {
                            Layout.fillWidth: true
                            key: "descaling.warning.steam"
                            fallback: "\u2022 The steam heater stays off until you explicitly cancel or finish descaling. Steam temp must be below 60\u00B0C (can take 1 hour from hot) - opening this page right after waking the machine saves the wait"
                            font: Theme.bodyFont
                            color: Theme.textColor
                            wrapMode: Text.WordWrap
                        }

                        Tr {
                            Layout.fillWidth: true
                            key: "descaling.warning.softwater"
                            fallback: "\u2022 Use soft or distilled water (TDS < 120ppm) for the solution - hard water buffers the acid"
                            font: Theme.bodyFont
                            color: Theme.textColor
                            wrapMode: Text.WordWrap
                        }

                        Tr {
                            Layout.fillWidth: true
                            key: "descaling.warning.notneeded"
                            fallback: "\u2022 If your water TDS is below 120ppm, you likely don't need to descale at all. Measure your water TDS to be sure"
                            font: Theme.bodyFont
                            color: Theme.textColor
                            wrapMode: Text.WordWrap
                        }

                        Tr {
                            Layout.fillWidth: true
                            key: "descaling.warning.noportafilter"
                            fallback: "\u2022 No portafilter needed - descaling cleans the internal water path, not the group head externals"
                            font: Theme.bodyFont
                            color: Theme.textColor
                            wrapMode: Text.WordWrap
                        }
                    }
                }

                // Solution preparation + Steam heater (two columns)
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(12)

                    // Left column: solution mix
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: solutionContent.implicitHeight + Theme.scaled(24)
                        color: Theme.cardBackgroundColor
                        radius: Theme.cardRadius

                        ColumnLayout {
                            id: solutionContent
                            anchors.fill: parent
                            anchors.margins: Theme.scaled(12)
                            spacing: Theme.scaled(8)

                            Tr {
                                key: "descaling.solution.title"
                                fallback: "Prepare 5% Citric Acid Solution"
                                font: Theme.subtitleFont
                                color: Theme.textColor
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: Theme.scaled(60)
                                color: Theme.backgroundColor
                                radius: Theme.scaled(8)

                                RowLayout {
                                    anchors.centerIn: parent
                                    spacing: Theme.scaled(30)

                                    Column {
                                        spacing: Theme.scaled(4)
                                        Tr {
                                            anchors.horizontalCenter: parent.horizontalCenter
                                            key: "descaling.solution.waterAmount"
                                            fallback: "1540 ml"
                                            font: Theme.titleFont
                                            color: Theme.flowColor
                                        }
                                        Tr {
                                            anchors.horizontalCenter: parent.horizontalCenter
                                            key: "descaling.solution.water"
                                            fallback: "water (room temp)"
                                            font: Theme.captionFont
                                            color: Theme.textSecondaryColor
                                        }
                                    }

                                    Tr {
                                        key: "descaling.solution.plus"
                                        fallback: "+"
                                        font: Theme.titleFont
                                        color: Theme.textSecondaryColor
                                    }

                                    Column {
                                        spacing: Theme.scaled(4)
                                        Tr {
                                            anchors.horizontalCenter: parent.horizontalCenter
                                            key: "descaling.solution.citricAmount"
                                            fallback: "80 g"
                                            font: Theme.titleFont
                                            color: Theme.pressureColor
                                        }
                                        Tr {
                                            anchors.horizontalCenter: parent.horizontalCenter
                                            key: "descaling.solution.citric"
                                            fallback: "citric acid"
                                            font: Theme.captionFont
                                            color: Theme.textSecondaryColor
                                        }
                                    }
                                }
                            }

                            Tr {
                                Layout.fillWidth: true
                                key: "descaling.solution.note"
                                fallback: "Mix until fully dissolved. Do NOT use hot water (80-100°C) - room temperature to warm (20-40°C) is best."
                                font: Theme.captionFont
                                color: Theme.textSecondaryColor
                                wrapMode: Text.WordWrap
                            }

                            Tr {
                                Layout.fillWidth: true
                                key: "descaling.solution.oldmachine"
                                fallback: "For v1.0/v1.1 machines: Never exceed 5% concentration (can damage old pressure sensor). If you've replaced the pressure sensor or manifold assembly, higher concentrations are safe."
                                font.pixelSize: Theme.captionFont.pixelSize
                                font.italic: true
                                color: Theme.warningColor
                                wrapMode: Text.WordWrap
                            }
                        }
                    }

                    // Right column: Steam heater control
                    Rectangle {
                        Layout.preferredWidth: Theme.scaled(160)
                        Layout.fillHeight: true
                        color: Theme.cardBackgroundColor
                        radius: Theme.cardRadius

                        ColumnLayout {
                            id: steamContent
                            anchors.fill: parent
                            anchors.margins: Theme.scaled(12)
                            spacing: Theme.scaled(8)

                            Tr {
                                Layout.alignment: Qt.AlignHCenter
                                key: "descaling.steam.title"
                                fallback: "Steam Heater"
                                font: Theme.subtitleFont
                                color: Theme.textColor
                            }

                            Item { Layout.fillHeight: true }

                            // Temperature readout. Everything that ACTS on the threshold —
                            // this colour, the label below, the Start gate and the
                            // confirmation dialog — reads steamSafeTempC, so the number
                            // cannot drift between them. The instructional prose still
                            // spells "60°C" out, as it does every other figure on this page
                            // (80-100°C, TDS < 120ppm, 1540 ml).
                            Text {
                                Layout.alignment: Qt.AlignHCenter
                                text: Theme.formatTemperature(descalingPage.steamTempC, 0)
                                font.pixelSize: Theme.scaled(36)
                                font.weight: Font.Bold
                                color: descalingPage.steamTooHotToDescale ? Theme.errorColor : Theme.primaryColor
                            }

                            // A bare number does not say whether it is good enough. This does.
                            Text {
                                Layout.alignment: Qt.AlignHCenter
                                Layout.fillWidth: true
                                horizontalAlignment: Text.AlignHCenter
                                wrapMode: Text.WordWrap
                                text: descalingPage.steamTooHotToDescale
                                    ? TranslationManager.translate("descaling.steam.cooling", "Cooling — wait for %1")
                                          .arg(Theme.formatTemperature(descalingPage.steamSafeTempC, 0))
                                    : TranslationManager.translate("descaling.steam.ready", "Cool enough to descale")
                                font: Theme.captionFont
                                color: descalingPage.steamTooHotToDescale ? Theme.warningColor : Theme.textSecondaryColor
                            }

                            Item { Layout.fillHeight: true }

                            // No enable/disable control here on purpose. The page turns the
                            // heater off on open and restores it on exit, so the only thing a
                            // button could offer is switching it back ON — which is precisely
                            // what must not happen before a descale. It read "Enable" against a
                            // readout saying "Cool enough to descale".
                        }
                    }
                }

                // Setup steps
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: stepsContent.implicitHeight + Theme.scaled(24)
                    color: Theme.cardBackgroundColor
                    radius: Theme.cardRadius

                    ColumnLayout {
                        id: stepsContent
                        anchors.fill: parent
                        anchors.margins: Theme.scaled(12)
                        spacing: Theme.scaled(8)

                        Tr {
                            key: "descaling.steps.title"
                            fallback: "Setup Steps"
                            font: Theme.subtitleFont
                            color: Theme.textColor
                        }

                        Tr {
                            Layout.fillWidth: true
                            key: "descaling.steps.1"
                            fallback: "1. The steam heater has been turned off for you. Wait for the steam temp (shown right) to drop below 60\u00B0C - it can take an hour from hot"
                            font: Theme.bodyFont
                            color: Theme.textColor
                            wrapMode: Text.WordWrap
                        }

                        Tr {
                            Layout.fillWidth: true
                            key: "descaling.steps.2"
                            fallback: "2. Pour the citric acid solution into the water tank"
                            font: Theme.bodyFont
                            color: Theme.textColor
                            wrapMode: Text.WordWrap
                        }

                        Tr {
                            Layout.fillWidth: true
                            key: "descaling.steps.3"
                            fallback: "3. Remove the steam tip from the wand"
                            font: Theme.bodyFont
                            color: Theme.textColor
                            wrapMode: Text.WordWrap
                        }

                        Tr {
                            Layout.fillWidth: true
                            key: "descaling.steps.4"
                            fallback: "4. Remove the drip tray cover (keep tray in place to catch water)"
                            font: Theme.bodyFont
                            color: Theme.textColor
                            wrapMode: Text.WordWrap
                        }

                        Tr {
                            Layout.fillWidth: true
                            key: "descaling.steps.5"
                            fallback: "5. Optional: Remove portafilter screen and brass diffusers for separate cleaning"
                            font: Theme.bodyFont
                            color: Theme.textColor
                            wrapMode: Text.WordWrap
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: Theme.scaled(1)
                            color: Theme.textSecondaryColor
                            opacity: 0.3
                        }

                        Tr {
                            Layout.fillWidth: true
                            key: "descaling.steps.duration"
                            // 12 minutes is measured, not quoted: three instrumented runs on
                            // firmware 1358 took 720.1, 720.2 and 720.2 s ([DE1][Descale] log
                            // lines). The page previously said 6 minutes, which came from
                            // Decent's 2018 descaling write-up.
                            fallback: "The descale cycle takes about 12 minutes. You can repeat up to 3 times until the solution is used up (empty drip tray between cycles)."
                            font: Theme.captionFont
                            color: Theme.textSecondaryColor
                            wrapMode: Text.WordWrap
                        }
                    }
                }

                // Start button
                AccessibleButton {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.topMargin: Theme.scaled(8)
                    Layout.preferredWidth: Theme.scaled(250)
                    Layout.preferredHeight: Theme.scaled(56)
                    primary: true
                    text: TranslationManager.translate("descaling.button.start", "Start Descaling")
                    accessibleName: TranslationManager.translate("descaling.button.start", "Start Descaling")
                    _customFontSize: Theme.scaled(20)
                    _customFontWeight: Font.Bold
                    enabled: descalingPage.descaleHoldReady && descalingPage.pendingOperation !== "startDescale"
                    onClicked: descalingPage.startDescaleChecked()
                }

                AccessibleButton {
                    Layout.alignment: Qt.AlignHCenter
                    visible: descalingPage.descaleHoldReady && !descalingPage.isDescaling
                    text: TranslationManager.translate("descaling.button.cancel", "Cancel Descaling")
                    accessibleName: TranslationManager.translate("descaling.button.cancel", "Cancel Descaling")
                    enabled: descalingPage.pendingOperation === ""
                    onClicked: {
                        descalingPage.requestHostOperation("cancelDescale")
                    }
                }

                Item { Layout.preferredHeight: Theme.scaled(20) }
            }
        }
    }

    // The 60 °C limit is Decent's, and descaling a hot steam boiler is what it exists to
    // prevent — so this warns rather than blocks. A user who has just replaced the boiler
    // water, or who knows the reading is stale, keeps the ability to proceed.
    DecenzaDialog {
        id: hotSteamConfirmDialog
        anchors.centerIn: parent
        width: Math.min(Theme.scaled(460), descalingPage.width - Theme.scaled(40))
        padding: Theme.scaled(20)
        modal: true
        closePolicy: T.Popup.CloseOnEscape | T.Popup.CloseOnPressOutside

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            border.color: Theme.warningColor
            border.width: 1
        }

        contentItem: ColumnLayout {
            spacing: Theme.scaled(16)

            Tr {
                Layout.fillWidth: true
                key: "descaling.hotsteam.title"
                fallback: "Steam boiler is still hot"
                font: Theme.subtitleFont
                color: Theme.warningColor
                wrapMode: Text.WordWrap
            }

            Text {
                Layout.fillWidth: true
                text: TranslationManager.translate(
                          "descaling.hotsteam.body",
                          "The steam boiler is at %1. Decent recommends descaling only below %2. Descaling now can damage the machine.")
                      .arg(Theme.formatTemperature(descalingPage.steamTempC, 0))
                      .arg(Theme.formatTemperature(descalingPage.steamSafeTempC, 0))
                font: Theme.bodyFont
                color: Theme.textColor
                wrapMode: Text.WordWrap
            }

            Text {
                Layout.fillWidth: true
                text: TranslationManager.translate(
                          "descaling.hotsteam.hint",
                          "The heater is already off and keeps cooling if you leave this page. Use Cancel Descaling to restore it.")
                font: Theme.captionFont
                color: Theme.textSecondaryColor
                wrapMode: Text.WordWrap
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.scaled(12)

                AccessibleButton {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Theme.scaled(48)
                    primary: true
                    text: TranslationManager.translate("descaling.hotsteam.wait", "Wait")
                    accessibleName: TranslationManager.translate("descaling.hotsteam.wait", "Wait")
                    onClicked: hotSteamConfirmDialog.close()
                }

                AccessibleButton {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Theme.scaled(48)
                    destructive: true
                    text: TranslationManager.translate("descaling.hotsteam.startAnyway", "Descale anyway")
                    accessibleName: TranslationManager.translate("descaling.hotsteam.startAnyway", "Descale anyway")
                    onClicked: {
                        hotSteamConfirmDialog.close()
                        descalingPage.beginDescaleRun(true)
                    }
                }
            }
        }
    }

    // Bottom bar
    BottomBar {
        title: TranslationManager.translate("descaling.title", "Descaling")
        onBackClicked: descalingPage.navigateBack()
    }
}
