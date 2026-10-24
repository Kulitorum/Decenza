import QtQuick
import Decenza

// Read-only view of the host-owned preparation capture. The controller owns the
// scale detector, session milk, timeout update, and cancel/restore semantics.
Item {
    id: root

    readonly property WeightCaptureState capture: PreparationService.state.milk
    readonly property bool active: capture.active
    readonly property bool seeded: capture.seeded
    readonly property bool loadPresent: capture.loadPresent
    readonly property bool isCaptured: capture.isCaptured
    readonly property bool lighterThanPitcher: capture.lighterThanPitcher
    readonly property bool belowMinimum: capture.belowMinimum
    readonly property real rawWeight: capture.rawWeight
    readonly property real cupWeight: capture.cupWeight
    readonly property real minNet: capture.minNet
    readonly property real maxNet: capture.maxNet
    readonly property real virtualZero: capture.virtualZero
    readonly property real netWeight: capture.netWeight

    signal milkCaptured(real milk, int seconds)

    function startAttempt() {
        return PreparationService.state.startMilkAttempt()
    }

    function cancelAttempt() {
        return PreparationService.state.cancelMilkAttempt()
    }

    Connections {
        target: PreparationService.state
        function onMilkCaptured(milk, seconds) {
            root.milkCaptured(milk, seconds)
        }
    }
}
