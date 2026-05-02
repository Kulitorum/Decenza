import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import Decenza

// QuickRatingRow — issue #1055 Layer 2.
//
// Low-friction one-tap rating row shown above the metadata fold on the
// post-shot review page when the shot is unrated AND the user has not
// dismissed the prompt for that shot. Three icon buttons map to default
// scores 80 / 60 / 40 (good / okay / bad). Tapping persists the score
// immediately via the existing saveEditedShot path; tapping the small
// dismiss × hides the row for that shot via a per-shot QSettings key.
//
// Once the shot is rated (currentScore > 0) the row collapses into a
// compact "Rated N — tap to revise" pill that reopens the three-icon
// state on tap, so the row is also a fast revision surface.
//
// Translation: all visible text routes through TranslationManager so
// non-English locales work. Accessibility: each interactive element is
// an AccessibleButton (which enforces accessibleName) so TalkBack /
// VoiceOver users can rate the shot too.
RowLayout {
    id: root

    // Inputs ---------------------------------------------------------
    // The shot's current enjoyment score (0 = unrated → row visible).
    property int currentScore: 0
    // Whether the user has dismissed the prompt for this specific shot.
    // Caller owns persistence (per-shot QSettings key) and binds this
    // property; the row only emits the dismiss signal.
    property bool dismissed: false

    // Outputs --------------------------------------------------------
    // Emitted when the user taps a rating icon. Caller writes the score
    // through to the shot via saveEditedShot.
    signal rateClicked(int score)
    // Emitted when the user taps the collapsed "Rated N — tap to
    // revise" pill. Caller is expected to zero `currentScore` so the
    // three-icon picker re-appears for a new tap. The component
    // does NOT mutate `currentScore` itself — the caller owns the
    // persisted value.
    signal reviseClicked()
    // Emitted when the user taps the dismiss control. Caller persists
    // the per-shot dismissed flag.
    signal dismissedClicked()

    // Layout ---------------------------------------------------------
    // Hidden entirely when dismissed; otherwise either the three-icon
    // bar or the collapsed "Rated N" pill.
    visible: !root.dismissed
    spacing: Theme.spacingMedium

    // Three-icon state — visible when currentScore == 0.
    Item {
        id: tripleState
        Layout.fillWidth: true
        Layout.preferredHeight: Theme.scaled(56)
        visible: root.currentScore === 0

        RowLayout {
            anchors.fill: parent
            spacing: Theme.spacingMedium

            // Tr-as-hidden pattern from CLAUDE.md.
            Tr { id: trPrompt; key: "rating.quick.prompt"
                 fallback: "How was this shot?"; visible: false }

            Text {
                text: trPrompt.text
                color: Theme.textColor
                font.pixelSize: Theme.bodyFontSize
                Layout.fillWidth: false
                // The three AccessibleButton children below carry the
                // navigation focus; this prompt is decorative for
                // sighted users and would otherwise create an extra
                // swipe target on TalkBack/VoiceOver.
                Accessible.ignored: true
            }

            // High / 80 (smiling face).
            AccessibleButton {
                accessibleName: TranslationManager.translate(
                    "rating.quick.good.accessibility", "Rate this shot good")
                Layout.preferredWidth: Theme.scaled(48)
                Layout.preferredHeight: Theme.scaled(48)
                onClicked: root.rateClicked(80)
                contentItem: Image {
                    source: Theme.emojiToImage("😊")  // 😊
                    sourceSize.width: Theme.scaled(28)
                    sourceSize.height: Theme.scaled(28)
                    fillMode: Image.PreserveAspectFit
                }
            }
            // Medium / 60 (neutral face).
            AccessibleButton {
                accessibleName: TranslationManager.translate(
                    "rating.quick.ok.accessibility", "Rate this shot okay")
                Layout.preferredWidth: Theme.scaled(48)
                Layout.preferredHeight: Theme.scaled(48)
                onClicked: root.rateClicked(60)
                contentItem: Image {
                    source: Theme.emojiToImage("😐")  // 😐
                    sourceSize.width: Theme.scaled(28)
                    sourceSize.height: Theme.scaled(28)
                    fillMode: Image.PreserveAspectFit
                }
            }
            // Low / 40 (frowning face).
            AccessibleButton {
                accessibleName: TranslationManager.translate(
                    "rating.quick.bad.accessibility", "Rate this shot bad")
                Layout.preferredWidth: Theme.scaled(48)
                Layout.preferredHeight: Theme.scaled(48)
                onClicked: root.rateClicked(40)
                contentItem: Image {
                    source: Theme.emojiToImage("😞")  // 😞
                    sourceSize.width: Theme.scaled(28)
                    sourceSize.height: Theme.scaled(28)
                    fillMode: Image.PreserveAspectFit
                }
            }

            Item { Layout.fillWidth: true }  // spacer pushes dismiss to right edge

            AccessibleButton {
                accessibleName: TranslationManager.translate(
                    "rating.quick.dismiss.accessibility", "Dismiss rating prompt")
                Layout.preferredWidth: Theme.scaled(36)
                Layout.preferredHeight: Theme.scaled(36)
                onClicked: root.dismissedClicked()
                contentItem: Image {
                    source: "qrc:/icons/cross.svg"
                    sourceSize.width: Theme.scaled(16)
                    sourceSize.height: Theme.scaled(16)
                    fillMode: Image.PreserveAspectFit
                }
            }
        }
    }

    // Collapsed "Rated N — tap to revise" pill — visible after a tap
    // landed a non-zero score. Tapping emits reviseClicked so the
    // caller can zero its bound currentScore and re-show the three-icon
    // picker; the pill itself does not mutate currentScore.
    AccessibleButton {
        id: collapsedPill
        Layout.fillWidth: true
        Layout.preferredHeight: Theme.scaled(40)
        visible: root.currentScore > 0
        accessibleName: TranslationManager.translate(
            "rating.quick.revise.accessibility",
            "Rated, tap to revise") + " " + root.currentScore
        text: TranslationManager.translate(
            "rating.quick.revise.label", "Rated %1 — tap to revise")
            .replace("%1", root.currentScore)
        onClicked: root.reviseClicked()
    }
}
