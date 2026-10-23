import QtQuick
import QtQuick.Controls
import QtQuick.Templates as T
import QtQuick.Layouts
import QtGraphs
import Decenza

T.Page {
    id: calibrationPage
    // Declarative so it re-evaluates on a language change. This used to be an
    // imperative assignment in onCompleted/onActivated, which ran once and left
    // page titles in the previous language until you navigated away and back.
    readonly property string pageTitle: TranslationManager.translate("flowCalibration.title", "Flow Calibration")

    objectName: "flowCalibrationPage"
    // suppressShotChart: this page draws its own graph, and the last-shot chart
    // background would put a second set of curves behind it.
    background: ThemedPageBackground { suppressShotChart: true }

    property var flowSnapshot: FlowCalibrationService.flowSnapshot
    property double multiplierDraft: 1.0
    property bool multiplierDraftInitialized: false
    // Save against the revision the displayed draft came from. Reading the live
    // revision at click time would let a second client update authorize this
    // page's now-stale draft to overwrite it.
    property double draftCalibrationRevision: -1
    property string loadRequestId: ""
    property string saveRequestId: ""
    property string operationError: ""

    Component.onCompleted: {
        loadRequestId = FlowCalibrationService.loadRecentShots()
    }

    ScrollView {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: bottomBar.top
        anchors.margins: Theme.scaled(12)
        anchors.topMargin: Theme.pageTopMargin
        contentWidth: availableWidth
        clip: true

        ColumnLayout {
            width: parent.width
            spacing: Theme.scaled(8)

            // Graph area. Keep it large enough to read, while allowing the
            // surrounding controls to scroll on short windows.
            GraphsView {
                id: chart
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.scaled(300)
                Layout.minimumHeight: Theme.scaled(180)

                theme: DecenzaGraphsTheme {}

                axisX: timeAxis
                axisY: valueAxis

                ValueAxis {
                    id: timeAxis
                    min: 0
                    // Round up to a whole second so the rightmost tick (placed by
                    // tickInterval: 1) coincides with the right edge of the plot
                    // — no dead space past the last tick.
                    max: Math.max(5, Math.ceil((calibrationPage.flowSnapshot.maxTime ?? 0) + 1))
                    tickInterval: 1
                    subTickCount: 0
                    labelFormat: "%.0f"
                    titleText: "s"
                }

                AutoRangingAxis {
                    id: valueAxis
                    series: [flowSeries, weightFlowSeries]
                    padding: 0.05
                    minFloor: 0
                    fallbackMax: 2.0
                    tickInterval: 2
                    subTickCount: 0
                    labelFormat: "%.1f"
                    titleText: "mL/s  ·  g/s"
                }

                LineSeries {
                    id: flowSeries
                    name: TranslationManager.translate("flowCalibration.flow", "Flow (calibrated)")
                    color: Theme.flowColor
                    width: Theme.graphLineWidth
                }

                LineSeries {
                    id: weightFlowSeries
                    name: TranslationManager.translate("flowCalibration.weightFlow", "Weight flow")
                    color: Theme.weightColor
                    width: Theme.graphLineWidth
                }
            }

            CustomLegend {
                Layout.fillWidth: true
                entries: [
                    { label: flowSeries.name,       color: Theme.flowColor,   active: flowSeries.visible },
                    { label: weightFlowSeries.name, color: Theme.weightColor, active: weightFlowSeries.visible }
                ]
                onEntryToggled: (index, nowActive) => {
                    if (index === 0) flowSeries.visible = nowActive
                    else             weightFlowSeries.visible = nowActive
                }
            }

            BusyIndicator {
                Layout.alignment: Qt.AlignHCenter
                running: FlowCalibrationService.loading
                visible: FlowCalibrationService.loading
                Accessible.ignored: true
            }

            // Error message (shown when no data)
            Text {
                Layout.fillWidth: true
                text: calibrationPage.operationError !== "" ? calibrationPage.operationError : (calibrationPage.flowSnapshot.errorMessage ?? "")
                color: Theme.textSecondaryColor
                font.pixelSize: Theme.scaled(14)
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                visible: calibrationPage.operationError !== "" || (!(calibrationPage.flowSnapshot.hasData ?? false)
                         && (calibrationPage.flowSnapshot.errorMessage?.length ?? 0) > 0
                         && !FlowCalibrationService.loading)
            }

            // Shot navigation row
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.scaled(10)

                AccessibleButton {
                    accessibleName: TranslationManager.translate("flowCalibration.previousShot", "Previous shot")
                    text: "◀"
                    enabled: (calibrationPage.flowSnapshot.hasPreviousShot ?? false) && !(FlowCalibrationService.loading)
                    onClicked: calibrationPage.loadShot((calibrationPage.flowSnapshot.currentShotIndex ?? 0) - 1)
                }

                Text {
                    Layout.fillWidth: true
                    text: (calibrationPage.flowSnapshot.hasData ?? false)
                          ? TranslationManager.translate("flowCalibration.shotCounter", "Shot") + " "
                            + ((calibrationPage.flowSnapshot.currentShotIndex ?? 0) + 1) + "/" + (calibrationPage.flowSnapshot.shotCount ?? 0)
                            + "    " + (calibrationPage.flowSnapshot.shotInfo ?? "")
                          : TranslationManager.translate("flowCalibration.noShots", "No shots available")
                    color: Theme.textColor
                    font.pixelSize: Theme.scaled(13)
                    horizontalAlignment: Text.AlignHCenter
                    elide: Text.ElideRight
                }

                AccessibleButton {
                    accessibleName: TranslationManager.translate("flowCalibration.nextShot", "Next shot")
                    text: "▶"
                    enabled: (calibrationPage.flowSnapshot.hasNextShot ?? false) && !(FlowCalibrationService.loading)
                    onClicked: calibrationPage.loadShot((calibrationPage.flowSnapshot.currentShotIndex ?? 0) + 1)
                }
            }

            // Multiplier controls
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: multiplierContent.implicitHeight + Theme.scaled(20)
                color: Theme.cardBackgroundColor
                radius: Theme.cardRadius

                ColumnLayout {
                    id: multiplierContent
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: Theme.scaled(10)
                    spacing: Theme.scaled(6)

                    RowLayout {
                        Layout.fillWidth: true

                        Text {
                            text: TranslationManager.translate("flowCalibration.multiplier", "Multiplier:")
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(14)
                        }

                        Text {
                            // Number() so the ?? union resolves to a double: qmllint models the
                            // operator's result as QJSPrimitiveValue, which has no toFixed.
                            text: Number(calibrationPage.multiplierDraft).toFixed(2)
                            color: Theme.primaryColor
                            font.pixelSize: Theme.scaled(18)
                            font.bold: true
                        }

                        Item { Layout.fillWidth: true }

                        AccessibleButton {
                            text: "-0.01"
                            accessibleName: TranslationManager.translate("flowCalibration.decrease", "Decrease multiplier")
                            enabled: (calibrationPage.flowSnapshot.hasData ?? false) && calibrationPage.multiplierDraft > 0.36
                            onClicked: calibrationPage.multiplierDraft = Math.max(0.35, calibrationPage.multiplierDraft - 0.01)
                        }

                        AccessibleButton {
                            text: "+0.01"
                            accessibleName: TranslationManager.translate("flowCalibration.increase", "Increase multiplier")
                            enabled: (calibrationPage.flowSnapshot.hasData ?? false) && calibrationPage.multiplierDraft < 2.99
                            onClicked: calibrationPage.multiplierDraft = Math.min(3.0, calibrationPage.multiplierDraft + 0.01)
                        }
                    }

                    // What the displayed shot actually poured under. The flow curve
                    // above is replayed from that value to the slider's, so when it
                    // is unknown the overlay is an assumption and says so.
                    //
                    // A sibling of the RowLayout above, not a child of it: inside the
                    // row it would have competed with the row's fillWidth spacer and
                    // wrapped this sentence into a narrow column beside the +/- buttons.
                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        font: Theme.captionFont
                        color: Theme.textSecondaryColor
                        visible: calibrationPage.flowSnapshot.hasData ?? false
                        text: (calibrationPage.flowSnapshot.shotMultiplierRecorded ?? false)
                            ? TranslationManager.translate("flowCalibration.recordedAt",
                                  "This shot was recorded at %1")
                                  .replace("%1", Number(calibrationPage.flowSnapshot.shotRecordedMultiplier).toFixed(3))
                            : TranslationManager.translate("flowCalibration.recordedAtUnknown",
                                  "Recorded before the multiplier was stored — the curve is shown as if it were 1.000")
                    }

                    Slider {
                        id: multiplierSlider
                        Layout.fillWidth: true
                        Layout.preferredHeight: Theme.scaled(40)
                        from: 0.35
                        to: 3.0
                        stepSize: 0.01
                        value: calibrationPage.multiplierDraft
                        enabled: calibrationPage.flowSnapshot.hasData ?? false
                        onMoved: calibrationPage.multiplierDraft = value

                        Accessible.role: Accessible.Slider
                        Accessible.name: TranslationManager.translate("flowCalibration.multiplierSlider", "Flow calibration multiplier")
                    }
                }
            }

            // Tip text
            Text {
                Layout.fillWidth: true
                text: TranslationManager.translate("flowCalibration.tip",
                      "Tip: Adjust until the flow curve matches the weight flow during the steady pour phase.")
                color: Theme.textSecondaryColor
                font.pixelSize: Theme.scaled(11)
                font.italic: true
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
            }

            // Action buttons
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.scaled(10)

                AccessibleButton {
                    text: TranslationManager.translate("flowCalibration.reset", "Reset to 1.0")
                    accessibleName: TranslationManager.translate("flowCalibration.resetAccessible", "Reset multiplier to factory default")
                    enabled: calibrationPage.flowSnapshot.hasData ?? false
                    onClicked: calibrationPage.multiplierDraft = 1.0
                }

                Item { Layout.fillWidth: true }

                AccessibleButton {
                    text: TranslationManager.translate("flowCalibration.save", "Save")
                    accessibleName: TranslationManager.translate("flowCalibration.saveAccessible", "Save flow calibration to machine")
                    primary: true
                    enabled: (calibrationPage.flowSnapshot.hasData ?? false)
                             && FlowCalibrationService.ready
                             && !FlowCalibrationService.busy
                    onClicked: {
                        calibrationPage.operationError = ""
                        calibrationPage.saveRequestId = FlowCalibrationService.save(
                            calibrationPage.multiplierDraft, calibrationPage.draftCalibrationRevision)
                    }
                }
            }
        }
    }

    BottomBar {
        id: bottomBar
        title: calibrationPage.pageTitle
        onBackClicked: AppShell.backRequested()
    }

    // Reload chart data when model data changes
    Connections {
        target: FlowCalibrationService
        function onFlowSnapshotChanged() {
            calibrationPage.flowSnapshot = FlowCalibrationService.flowSnapshot
            if (calibrationPage.flowSnapshot.hasData && !calibrationPage.multiplierDraftInitialized) {
                calibrationPage.multiplierDraft = FlowCalibrationService.savedMultiplier
                calibrationPage.draftCalibrationRevision = FlowCalibrationService.calibrationRevision
                calibrationPage.multiplierDraftInitialized = true
            }
            calibrationPage.loadData()
        }
        function onOperationCompleted(requestId, operation, success, result, reason) {
            if (requestId === calibrationPage.loadRequestId
                    && (operation === "loadRecentShots" || operation === "loadShot")) {
                calibrationPage.loadRequestId = ""
                if (success) {
                    calibrationPage.operationError = ""
                    calibrationPage.flowSnapshot = FlowCalibrationService.flowSnapshot
                    if (!calibrationPage.multiplierDraftInitialized) {
                        calibrationPage.multiplierDraft = FlowCalibrationService.savedMultiplier
                        calibrationPage.draftCalibrationRevision = FlowCalibrationService.calibrationRevision
                        calibrationPage.multiplierDraftInitialized = true
                    }
                    calibrationPage.loadData()
                } else {
                    calibrationPage.operationError = calibrationPage.displayReason(reason)
                }
                return
            }
            if (requestId !== calibrationPage.saveRequestId || operation !== "save")
                return
            calibrationPage.saveRequestId = ""
            if (success)
                AppShell.backRequested()
            else
                calibrationPage.operationError = calibrationPage.displayReason(reason)
        }
    }

    function displayReason(reason) {
        if (reason === "conflict")
            return TranslationManager.translate("flowCalibration.conflict", "Calibration changed elsewhere. Review the current value and try again.")
        if (reason === "busy" || reason === "tooManyRequests")
            return TranslationManager.translate("flowCalibration.busy", "Another calibration request is still running. Try again shortly.")
        if (reason === "noShots")
            return TranslationManager.translate("flowCalibration.noData", "No shots with scale data found. Run a shot with a Bluetooth scale connected.")
        if (reason === "disconnected" || reason === "unavailable")
            return TranslationManager.translate("flowCalibration.unavailable", "Flow calibration is unavailable. Reconnect and try again.")
        return reason
    }

    function loadShot(index) {
        if (index < 0 || index >= (flowSnapshot.shotCount ?? 0)) return
        operationError = ""
        loadRequestId = FlowCalibrationService.loadShot(index, flowSnapshot.shotListRevision)
    }

    function loadData() {
        flowSeries.clear()
        weightFlowSeries.clear()
        var fData = FlowCalibrationService.replayFlowData(
                    flowSnapshot.originalFlowData ?? [],
                    flowSnapshot.shotRecordedMultiplier ?? 0,
                    multiplierDraft)
        for (let i = 0; i < fData.length; i++)
            flowSeries.append(fData[i].x, fData[i].y)
        var wfData = flowSnapshot.weightFlowData ?? []
        for (let i = 0; i < wfData.length; i++)
            weightFlowSeries.append(wfData[i].x, wfData[i].y)
    }

    onMultiplierDraftChanged: loadData()
}
