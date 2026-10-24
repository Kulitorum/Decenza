pragma ComponentBehavior: Bound

import QtQuick
import Decenza
import "EspressoCompletionPolicy.js" as CompletionPolicy

// Shared presentation and navigation policy for an espresso operation that has
// ended. The shell supplies navigation callbacks so the same coordinator serves
// the native and WebAssembly application shells.
Item {
    id: root

    property bool isCalibrationShot: false
    property int postShotReviewTimeout: 31

    property bool stopOverlayVisible: false
    property bool wasEspressoOperation: false
    property int pendingShotId: -1
    property bool pendingMetadataNavigation: false
    // qint64 is bounded to JavaScript's exact-integer range by MainController.
    property double activeOperationId: 0

    signal leaveOperationPageRequested()
    signal reviewRequested(int shotId)
    signal shotStarted()

    anchors.fill: parent
    z: 500

    function getStopReasonText() {
        switch (AppShell.stopReason) {
        case "manual":
            return TranslationManager.translate("main.stopReason.manual", "Stopped manually")
        case "weight":
            return TranslationManager.translate("main.stopReason.weight", "Target weight reached")
        case "machine":
            return TranslationManager.translate("main.stopReason.machine", "Profile complete - DE1 stopped the shot")
        default:
            return TranslationManager.translate("main.stopReason.ended", "Shot ended")
        }
    }

    function isEspressoPhase(phase) {
        return CompletionPolicy.isOperationPhase(phase, MachineState.Phase)
    }

    function beginShot(phase, operationId) {
        if (!isEspressoPhase(phase) || operationId <= 0)
            return
        if (wasEspressoOperation && activeOperationId === operationId)
            return

        activeOperationId = operationId
        wasEspressoOperation = true
        AppShell.stopReason = ""
        stopOverlayVisible = false
        pendingMetadataNavigation = false
        pendingShotId = -1
        stopOverlayTimer.stop()
        shotStarted()
    }

    function finishShot(operationId, reason) {
        if (!wasEspressoOperation
                || !CompletionPolicy.isCurrentOperation(operationId, activeOperationId))
            return

        AppShell.stopReason = reason === "manual" || reason === "weight"
                ? reason : "machine"

        stopOverlayVisible = true
        popInAnim.start()
        stopOverlayTimer.restart()
        wasEspressoOperation = false
    }

    function reviewWasSaved(shotId, operationId) {
        // The async history save can finish after the next espresso cycle has
        // already started. Only the originating cycle may trigger navigation.
        if (!CompletionPolicy.isCurrentReviewResult(
                    shotId, operationId, MachineControlService.espressoOperationId)) {
            WebDebugLogger.debug("Shot", "main", "Ignoring post-shot result for an older espresso operation")
            return
        }

        // Zero is a real save-failure result. Never substitute a previous saved
        // shot ID, which could navigate to stale data.
        pendingShotId = shotId

        if (stopOverlayVisible) {
            pendingMetadataNavigation = true
            stopOverlayTimer.restart()
            return
        }

        if (isCalibrationShot || postShotReviewTimeout === 0)
            return

        if (pendingShotId > 0)
            reviewRequested(pendingShotId)
        else
            WebDebugLogger.warn("Shot", "main", "Post-shot navigation skipped: no valid saved shot ID")
    }

    Connections {
        target: MachineControlService

        function onStateChanged() {
            if (!MachineControlService.ready || (root.activeOperationId > 0
                    && root.activeOperationId !== MachineControlService.espressoOperationId)) {
                root.stopOverlayVisible = false
                root.pendingMetadataNavigation = false
                root.pendingShotId = -1
                root.wasEspressoOperation = false
                root.activeOperationId = 0
                stopOverlayTimer.stop()
            }
            // A shell may attach while a shot is already running, after the
            // start event. Hydrate only from an actual espresso phase; Ending
            // is shared by unrelated machine operations.
            if (CompletionPolicy.canHydrate(
                    MachineControlService.ready,
                    MachineControlService.phase,
                    MachineControlService.espressoOperationId,
                    MachineState.Phase)) {
                root.beginShot(MachineControlService.phase,
                               MachineControlService.espressoOperationId)
            }
        }

        function onPresentationEvent(sourceId, sequence, event, payload) {
            if (event === "shotStarted")
                root.beginShot(Number(payload.phase), Number(payload.operationId))
            else if (event === "shotEnded")
                root.finishShot(Number(payload.operationId), String(payload.reason || "machine"))
            else if (event === "targetWeightReached")
                AppShell.stopReason = "weight"
            else if (event === "shotReviewReady")
                root.reviewWasSaved(Number(payload.shotId), Number(payload.operationId))
        }
    }

    Rectangle {
        id: stopReasonOverlay

        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.scaled(120)
        anchors.horizontalCenter: parent.horizontalCenter
        width: stopReasonText.width + Theme.spacingLarge * 2
        height: Theme.scaled(44)
        radius: Theme.scaled(22)
        color: Theme.warningColor
        visible: root.stopOverlayVisible || fadeOutAnim.running
        opacity: root.stopOverlayVisible ? 1 : 0

        SequentialAnimation {
            id: popInAnim
            NumberAnimation {
                target: stopReasonOverlay
                property: "scale"
                from: 1.0
                to: 1.10
                duration: 150
                easing.type: Easing.OutBack
                easing.overshoot: 1.5
            }
            NumberAnimation {
                target: stopReasonOverlay
                property: "scale"
                from: 1.10
                to: 1.0
                duration: 350
                easing.type: Easing.OutBack
                easing.overshoot: 1.5
            }
        }

        Behavior on opacity {
            enabled: root.stopOverlayVisible
            NumberAnimation { id: fadeOutAnim; duration: 2000 }
        }

        Accessible.role: Accessible.AlertMessage
        Accessible.name: stopReasonText.text
        Accessible.focusable: true

        Text {
            id: stopReasonText
            anchors.centerIn: parent
            text: root.getStopReasonText()
            color: Theme.contrastColorFor(stopReasonOverlay.color)
            font: Theme.bodyFont
            Accessible.ignored: true
        }
    }

    Timer {
        id: stopOverlayTimer
        interval: 3000
        onTriggered: {
            root.stopOverlayVisible = false
            if (!MachineControlService.ready || !CompletionPolicy.isCurrentOperation(
                        root.activeOperationId, MachineControlService.espressoOperationId)) {
                root.pendingMetadataNavigation = false
                return
            }
            if (root.isCalibrationShot) {
                root.pendingMetadataNavigation = false
                root.leaveOperationPageRequested()
                return
            }

            if (root.pendingMetadataNavigation) {
                root.pendingMetadataNavigation = false
                if (root.postShotReviewTimeout === 0) {
                    WebDebugLogger.debug("Shot", "main", "Post-shot review timeout is Instant; skipping review page")
                    root.leaveOperationPageRequested()
                } else if (root.pendingShotId > 0) {
                    root.reviewRequested(root.pendingShotId)
                } else {
                    WebDebugLogger.warn("Shot", "main", "Post-shot navigation skipped: no valid saved shot ID")
                    root.leaveOperationPageRequested()
                }
            } else {
                root.leaveOperationPageRequested()
            }
        }
    }
}
