import QtQuick
import QtQuick.Layouts
import Decenza

// Live coaching banner — purely VISUAL. Binds to a live-coach service passed in
// via `coach` (LiveSteamCoach, mounted on the steam page) and shows one short
// calm cue at a time while an operation runs. Fades in when a cue is active,
// auto-dismisses after a few seconds (UI auto-dismiss timer is allowed), and
// tints by severity using Theme tokens. Voice is NOT this component's job: the
// coach service itself emits speakRequested (gated on its own audio setting and
// wired to AccessibilityManager::announceCoaching in main.cpp), so audio works
// with this banner disabled.
Item {
    id: banner

    // The live coach service to bind to (QML-marshalable Q_PROPERTYs: cueText /
    // cueSeverity / cueActive). Passed in by the mount so this banner doesn't
    // reference any page-specific service directly.
    property var coach: null

    // Feature gate, passed in by each mount (each page has its own on/off pref).
    // Named coachEnabled (not `enabled`) to avoid shadowing the built-in Item.enabled.
    property bool coachEnabled: true

    // Gate: feature pref + an actually-active cue + not yet auto-dismissed.
    readonly property bool shouldShow: coachEnabled
                                       && coach
                                       && coach.cueActive
                                       && !dismissed

    // Set true by the auto-dismiss timer; reset whenever a new cue arrives.
    property bool dismissed: false

    // Severity -> Theme color token (no hardcoded colors).
    function severityColor(severity) {
        if (severity === "positive") return Theme.successColor
        if (severity === "caution") return Theme.warningColor
        return Theme.textColor  // "info"
    }

    implicitHeight: pill.height
    height: shouldShow ? implicitHeight : 0
    visible: opacity > 0.01
    opacity: shouldShow ? 1.0 : 0.0
    Behavior on opacity { NumberAnimation { duration: 220 } }

    Accessible.role: Accessible.StaticText
    Accessible.name: coach ? coach.cueText : ""

    // React to cue changes: re-arm the auto-dismiss timer. (Speaking happens in
    // the coach service, not here.)
    Connections {
        target: banner.coach
        function onCueChanged() {
            if (!banner.coach.cueActive)
                return
            banner.dismissed = false
            if (banner.coachEnabled)
                dismissTimer.restart()
        }
    }

    // UI auto-dismiss (allowed by the no-timers-as-guards rule).
    Timer {
        id: dismissTimer
        interval: 5000
        repeat: false
        onTriggered: banner.dismissed = true
    }

    Rectangle {
        id: pill
        anchors.horizontalCenter: parent.horizontalCenter
        width: Math.min(parent.width, cueRow.implicitWidth + Theme.spacingMedium * 2)
        height: cueRow.implicitHeight + Theme.spacingSmall * 2
        radius: Theme.cardRadius
        color: Qt.darker(Theme.surfaceColor, 1.15)
        border.width: Theme.scaled(2)
        border.color: banner.severityColor(banner.coach ? banner.coach.cueSeverity : "info")

        RowLayout {
            id: cueRow
            anchors.centerIn: parent
            spacing: Theme.spacingSmall

            // Severity dot.
            Rectangle {
                Layout.alignment: Qt.AlignVCenter
                width: Theme.scaled(10)
                height: Theme.scaled(10)
                radius: width / 2
                color: banner.severityColor(banner.coach ? banner.coach.cueSeverity : "info")
                Accessible.ignored: true
            }

            Text {
                id: cueLabel
                Layout.alignment: Qt.AlignVCenter
                text: banner.coach ? banner.coach.cueText : ""
                color: Theme.textColor
                font.family: Theme.bodyFont.family
                font.pixelSize: Theme.bodyFont.pixelSize
                font.weight: Font.Medium
                elide: Text.ElideRight
                Accessible.ignored: true  // banner-level Accessible.name carries it
            }
        }
    }
}
