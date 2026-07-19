import QtQuick
import Decenza

// Surfaces TranslationManager's refusals to the user instead of only the log.
//
// This exists because of a live test that went exactly wrong enough to be useful. With a corrupt
// language file the app correctly refused to save an edit — protecting 4021 strings that the
// previous code would have destroyed — and then said nothing at all. The tester edited a string,
// watched it not save, and had to ask whether the feature was broken. A guard that is
// indistinguishable from a dead button is only half a fix.
//
// The messages already exist and are already translated: every refusal path sets m_lastError with
// a tr() string explaining what was not done and why. They were simply never read. `lastError` had
// no QML consumer anywhere in the app, so all of it was written and thrown away.
//
// Self-subscribing on purpose. Wiring each call site individually is how the gap appeared in the
// first place — a new refusal path would silently not surface. Mount this once per page that can
// trigger one and every present and future refusal is covered.
Rectangle {
    id: root

    // Auto-dismiss is one of the cases the project rules explicitly allow a timer for; this is a
    // transient notice, not a guard standing in for a condition.
    property int dismissAfterMs: 6000

    anchors.horizontalCenter: parent.horizontalCenter
    anchors.bottom: parent.bottom
    anchors.bottomMargin: Theme.spacingLarge
    width: Math.min(messageText.implicitWidth + Theme.scaled(28), parent.width - Theme.scaled(32))
    height: messageText.implicitHeight + Theme.scaled(20)
    color: Theme.surfaceColor
    radius: Theme.cardRadius
    border.width: 1
    border.color: Theme.errorColor
    opacity: 0
    visible: opacity > 0
    z: 1000

    Accessible.role: Accessible.AlertMessage
    Accessible.name: messageText.text

    Text {
        id: messageText
        anchors.centerIn: parent
        width: root.width - Theme.scaled(28)
        color: Theme.textColor
        font: Theme.bodyFont
        wrapMode: Text.WordWrap
        horizontalAlignment: Text.AlignHCenter
    }

    Connections {
        target: TranslationManager
        function onLastErrorChanged() {
            // An empty error is a clear, not a message.
            if (!TranslationManager.lastError || TranslationManager.lastError.length === 0)
                return
            messageText.text = TranslationManager.lastError
            AccessibilityManager.announce(TranslationManager.lastError, true)
            showAnimation.restart()
        }
    }

    SequentialAnimation {
        id: showAnimation
        NumberAnimation { target: root; property: "opacity"; to: 1; duration: 150 }
        PauseAnimation { duration: root.dismissAfterMs }
        NumberAnimation { target: root; property: "opacity"; to: 0; duration: 300 }
    }
}
