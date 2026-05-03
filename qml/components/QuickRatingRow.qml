import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import Decenza

// QuickRatingRow — issue #1055 Layer 2.
//
// Low-friction one-tap rating row shown above the metadata fold on the
// post-shot review page for unrated shots (enjoymentSource != "user").
// Three icon buttons map to scores 80 / 60 / 40 (good / okay / bad).
// Tapping persists the score immediately via saveEditedShot.
//
// Translation: all visible text routes through TranslationManager so
// non-English locales work. Accessibility: each interactive element is
// an AccessibleButton (which enforces accessibleName) so TalkBack /
// VoiceOver users can rate the shot too.
RowLayout {
    id: root

    // Emitted when the user taps a rating icon. Caller writes the score
    // through to the shot via saveEditedShot.
    signal rateClicked(int score)

    spacing: Theme.spacingMedium

    Item {
        id: tripleState
        Layout.fillWidth: true
        Layout.preferredHeight: Theme.scaled(80)

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
                Layout.preferredWidth: Theme.scaled(72)
                Layout.preferredHeight: Theme.scaled(72)
                onClicked: root.rateClicked(80)
                contentItem: Image {
                    source: Theme.emojiToImage("😊")  // 😊
                    sourceSize.width: Theme.scaled(48)
                    sourceSize.height: Theme.scaled(48)
                    fillMode: Image.PreserveAspectFit
                }
            }
            // Medium / 60 (neutral face).
            AccessibleButton {
                accessibleName: TranslationManager.translate(
                    "rating.quick.ok.accessibility", "Rate this shot okay")
                Layout.preferredWidth: Theme.scaled(72)
                Layout.preferredHeight: Theme.scaled(72)
                onClicked: root.rateClicked(60)
                contentItem: Image {
                    source: Theme.emojiToImage("😐")  // 😐
                    sourceSize.width: Theme.scaled(48)
                    sourceSize.height: Theme.scaled(48)
                    fillMode: Image.PreserveAspectFit
                }
            }
            // Low / 40 (frowning face).
            AccessibleButton {
                accessibleName: TranslationManager.translate(
                    "rating.quick.bad.accessibility", "Rate this shot bad")
                Layout.preferredWidth: Theme.scaled(72)
                Layout.preferredHeight: Theme.scaled(72)
                onClicked: root.rateClicked(40)
                contentItem: Image {
                    source: Theme.emojiToImage("😟")  // 😟
                    sourceSize.width: Theme.scaled(48)
                    sourceSize.height: Theme.scaled(48)
                    fillMode: Image.PreserveAspectFit
                }
            }

            Item { Layout.fillWidth: true }
        }
    }
}
