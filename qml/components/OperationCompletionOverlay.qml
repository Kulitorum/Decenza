import QtQuick
import Decenza
import "OperationNavigationPolicy.js" as OperationNavigationPolicy

// The single completion treatment and timeout used by both application shells.
// The shell owns what happens after finished(); this component only presents the
// result and holds it while a post-session dialog is open.
Rectangle {
    id: root

    property alias message: completionState.message
    property alias completionType: completionState.completionType
    readonly property bool pending: completionState.pending

    // Kept configurable for focused timer-policy tests; production shells use
    // the native 1.5 second completion interval.
    property int completionDuration: 1500
    // Steam's completion stays long enough to offer a purge (add-steam-purge).
    property int steamCompletionDuration: 10000

    // A purge started from the steam completion holds it until the machine has
    // been through SteamPurge and is idle again.
    property string _purgeRequestId: ""
    property bool _purging: false
    property bool _purgeSeen: false
    // Back's stop request; the window leaves once the host has accepted it.
    property string _backRequestId: ""
    // The host's reason when Purge or Back failed, shown under the buttons.
    property string _actionError: ""

    // A long black's hot water is only the first step: its completion waits until
    // the drink resolves, and is dropped if the espresso starts (its page replaces
    // this one). Shown only if the machine is still idle, i.e. the espresso was
    // refused or skipped.
    property bool _heldForDrink: false
    property string _heldText: ""
    property string _heldType: ""
    readonly property bool _drinkActive: MachineControlService.drinkSequenceActive
    // drinkSequenceActive reads false while the service is not ready, so a
    // reconnect must not release the hold: only a ready service ends it.
    readonly property bool _serviceReady: MachineControlService.ready

    signal finished()

    on_DrinkActiveChanged: _releaseIfDrinkOver()
    on_ServiceReadyChanged: _releaseIfDrinkOver()

    function _releaseIfDrinkOver() {
        if (!_heldForDrink || !_serviceReady || _drinkActive)
            return
        _heldForDrink = false
        if (_isIdlePhase(MachineControlService.phase))
            completionState.showCompletion(_heldText, _heldType)
    }

    function _isIdlePhase(phase) {
        return phase === MachineState.Phase.Idle || phase === MachineState.Phase.Ready
            || phase === MachineState.Phase.Heating
    }

    Connections {
        target: MachineControlService
        function onStateChanged() {
            const phase = MachineControlService.phase
            // The second step started: its page replaces this one, so this
            // completion belongs to nothing on screen any more. A transient
            // state between the steps (Refill, Busy) has no page and keeps it.
            if (root._heldForDrink && root._serviceReady
                    && OperationNavigationPolicy.machineOperationPage(
                           phase, MachineState.Phase, AppShell.drinkInCupView) !== "")
                root._heldForDrink = false
            if (root._purging) {
                if (phase === MachineState.Phase.SteamPurge)
                    root._purgeSeen = true
                else if (phase === MachineState.Phase.Disconnected || !root._serviceReady
                         || (root._purgeSeen && root._isIdlePhase(phase)))
                    root._endPurge()
            }
        }
        function onCompleted(requestId, operation, success, message, result) {
            if (requestId === root._purgeRequestId) {
                root._purgeRequestId = ""
                if (!success)
                    root._resumeAfterFailure(message)
            } else if (requestId === root._backRequestId) {
                root._backRequestId = ""
                if (success)
                    root._endPurge()
                else
                    root._resumeAfterFailure(message)
            }
        }
    }

    function _startPurge() {
        if (_purging || _backRequestId)
            return
        _actionError = ""
        _purgeRequestId = MachineControlService.startSteamPurge(MachineState.controlContextId)
        _purging = true
        _purgeSeen = false
        completionState.hold()
    }

    function _endPurge() {
        _purging = false
        _purgeSeen = false
        completionState.finish()
    }

    // The window stays up with its buttons and counts down again.
    function _resumeAfterFailure(message) {
        _purging = false
        _purgeSeen = false
        _actionError = message
        completionState.showCompletion(completionState.message, completionState.completionType)
    }

    // Back stops whatever is still running, then leaves like Done.
    function _back() {
        if (_backRequestId)
            return
        const phase = MachineControlService.phase
        if (!_purging && phase !== MachineState.Phase.SteamPurge
                && phase !== MachineState.Phase.Steaming) {
            _endPurge()
            return
        }
        _purgeRequestId = ""
        completionState.hold()
        _backRequestId = MachineControlService.requestIdle(MachineState.controlContextId)
    }

    OperationCompletionState {
        id: completionState
        duration: completionType === "steam" ? root.steamCompletionDuration : root.completionDuration
        onFinished: root.finished()
    }

    anchors.fill: parent
    color: Theme.backgroundColor
    opacity: completionState.visible ? 1 : 0
    visible: opacity > 0
    z: 500

    function showCompletion(text, type) {
        // The steam page re-announces its completion when a purge returns to Idle.
        if (_purging || _backRequestId)
            return
        _actionError = ""
        if (_drinkActive) {
            _heldForDrink = true
            _heldText = text
            _heldType = type
            return
        }
        completionState.showCompletion(text, type)
    }

    function suspend() { completionState.suspend() }

    function cancel() {
        _heldForDrink = false
        _purging = false
        _purgeSeen = false
        _purgeRequestId = ""
        _backRequestId = ""
        _actionError = ""
        completionState.cancel()
    }

    function finish() { completionState.finish() }

    Column {
        anchors.centerIn: parent
        spacing: 20

        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            width: 120
            height: 120
            radius: 60
            color: "transparent"
            border.color: Theme.primaryColor
            border.width: 4

            ColoredIcon {
                anchors.centerIn: parent
                source: "qrc:/icons/tick.svg"
                iconWidth: 50
                iconHeight: 50
                iconColor: Theme.primaryColor
            }
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: root.message
            color: Theme.textSecondaryColor
            font: Theme.bodyFont
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: root.completionType === "hotwater"
                  ? Math.max(0, MachineState.scaleWeight).toFixed(0) + "g"
                  : MachineState.shotTime.toFixed(1) + "s"
            color: Theme.textColor
            font: Theme.timerFont
        }

        // After steaming: purge the wand, or leave now (add-steam-purge).
        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: Theme.spacingMedium
            visible: root.completionType === "steam" && !root._purging

            AccessibleButton {
                primary: true
                text: TranslationManager.translate("steam.label.purge", "Purge")
                accessibleName: TranslationManager.translate("steam.accessible.purgeWand",
                                                             "Purge the steam wand")
                onClicked: root._startPurge()
            }
            AccessibleButton {
                text: TranslationManager.translate("common.button.done", "Done")
                accessibleName: TranslationManager.translate("steam.accessible.doneSteaming",
                                                             "Done, leave the steam page")
                onClicked: completionState.finish()
            }
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            visible: root._purging
            text: TranslationManager.translate("steam.label.purging", "Purging…")
            color: Theme.textSecondaryColor
            font: Theme.bodyFont
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            visible: root.completionType === "steam" && !root._purging && root._actionError !== ""
            text: root._actionError
            color: Theme.errorColor
            font: Theme.bodyFont
        }
    }

    BottomBar {
        visible: root.completionType === "steam"
        onBackClicked: root._back()
    }

    Behavior on opacity {
        // Match native behavior: appear instantly, fade only when dismissed.
        enabled: root.opacity > 0
        NumberAnimation { duration: 200 }
    }
}
