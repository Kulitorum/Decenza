import QtQuick
import QtQuick.Layouts
import Decenza

/**
 * TastePicker — tap-only capture of the two dial-in taste axes the shot curve
 * can't reveal, plus overall rating. Fully text-free. Shared by the AI taste
 * intake dialog (ConversationOverlay) and the post-shot review page, so there is
 * exactly one taste UI. See add-ai-taste-intake.
 *
 *   Extraction : Sour · Balanced · Bitter   → tasteBalance ("sour"|"balanced"|"bitter"|"")
 *   Body       : Thin · Medium · Heavy       → tasteBody    ("thin"|"medium"|"heavy"|"")
 *   Overall    : RatingInput (25/50/75/100)  → overall (0 = unset)
 *
 * Each row is optional and shown only when its `show*` flag is true (callers set
 * these from "is this axis still unset?"). Tapping a selected chip clears it.
 */
ColumnLayout {
    id: root

    // Current values ("" / 0 = unset). Two-way: callers bind and read these.
    property string tasteBalance: ""
    property string tasteBody: ""
    property int overall: 0

    // Row visibility — callers pass "only show what's unfilled".
    property bool showExtraction: true
    property bool showBody: true
    property bool showOverall: true

    // Emitted on a user tap (not on programmatic binding changes) so callers can
    // persist immediately.
    signal tasteBalanceModified(string value)
    signal tasteBodyModified(string value)
    signal overallModified(int value)

    spacing: Theme.spacingMedium

    // Hidden Tr instances so chip/label text resolves through TranslationManager.
    Tr { id: trExtraction; key: "tasteIntake.extraction"; fallback: "Taste"; visible: false }
    Tr { id: trBody;       key: "tasteIntake.body";       fallback: "Body"; visible: false }
    Tr { id: trOverall;    key: "tasteIntake.overall";    fallback: "Overall"; visible: false }
    Tr { id: trSour;       key: "tasteIntake.sour";       fallback: "Sour"; visible: false }
    Tr { id: trBalanced;   key: "tasteIntake.balanced";   fallback: "Balanced"; visible: false }
    Tr { id: trBitter;     key: "tasteIntake.bitter";     fallback: "Bitter"; visible: false }
    Tr { id: trThin;       key: "tasteIntake.thin";       fallback: "Thin"; visible: false }
    Tr { id: trMedium;     key: "tasteIntake.medium";     fallback: "Medium"; visible: false }
    Tr { id: trHeavy;      key: "tasteIntake.heavy";      fallback: "Heavy"; visible: false }

    // ---- Extraction row --------------------------------------------------
    ColumnLayout {
        Layout.fillWidth: true
        visible: root.showExtraction
        spacing: Theme.spacingSmall

        Text {
            text: trExtraction.text
            font.pixelSize: Theme.scaled(13)
            color: Theme.textSecondaryColor
            Accessible.ignored: true
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSmall
            Repeater {
                model: [
                    { value: "sour",     label: trSour.text,     icon: "qrc:/icons/taste-sour.svg" },
                    { value: "balanced", label: trBalanced.text, icon: "qrc:/icons/taste-balanced.svg" },
                    { value: "bitter",   label: trBitter.text,   icon: "qrc:/icons/taste-bitter.svg" }
                ]
                delegate: chipComponent
            }
        }
    }

    // ---- Body row --------------------------------------------------------
    ColumnLayout {
        Layout.fillWidth: true
        visible: root.showBody
        spacing: Theme.spacingSmall

        Text {
            text: trBody.text
            font.pixelSize: Theme.scaled(13)
            color: Theme.textSecondaryColor
            Accessible.ignored: true
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSmall
            Repeater {
                model: [
                    { value: "thin",   label: trThin.text,   icon: "qrc:/icons/body-thin.svg" },
                    { value: "medium", label: trMedium.text, icon: "qrc:/icons/body-medium.svg" },
                    { value: "heavy",  label: trHeavy.text,  icon: "qrc:/icons/body-heavy.svg" }
                ]
                delegate: bodyChipComponent
            }
        }
    }

    // ---- Overall row (reuses RatingInput — no parallel rating widget) ----
    ColumnLayout {
        Layout.fillWidth: true
        visible: root.showOverall
        spacing: Theme.spacingSmall

        Text {
            text: trOverall.text
            font.pixelSize: Theme.scaled(13)
            color: Theme.textSecondaryColor
            Accessible.ignored: true
        }
        RatingInput {
            Layout.fillWidth: true
            value: root.overall
            accessibleName: trOverall.text
            onValueModified: function(newValue) {
                root.overall = newValue
                root.overallModified(newValue)
            }
        }
    }

    // Extraction chip: sets tasteBalance (toggles off if re-tapped).
    Component {
        id: chipComponent
        TasteChip {
            selected: root.tasteBalance === modelData.value
            onTapped: {
                var v = selected ? "" : modelData.value
                root.tasteBalance = v
                root.tasteBalanceModified(v)
            }
        }
    }

    // Body chip: sets tasteBody (toggles off if re-tapped).
    Component {
        id: bodyChipComponent
        TasteChip {
            selected: root.tasteBody === modelData.value
            onTapped: {
                var v = selected ? "" : modelData.value
                root.tasteBody = v
                root.tasteBodyModified(v)
            }
        }
    }

    // Shared icon+label chip card. Card style mirrors the recipe wizard's
    // selectable option cards: border-highlighted (not filled) when selected,
    // with a subtle primary tint and the icon/label tinted to the primary color.
    component TasteChip: Rectangle {
        required property var modelData
        property bool selected: false
        signal tapped()

        Layout.fillWidth: true
        Layout.preferredHeight: Theme.scaled(56)
        radius: Theme.cardRadius
        color: selected ? Qt.rgba(Theme.primaryColor.r, Theme.primaryColor.g,
                                  Theme.primaryColor.b, 0.14)
                        : Theme.cardBackgroundColor
        border.color: selected ? Theme.primaryColor : Theme.borderColor
        border.width: selected ? 2 : 1

        RowLayout {
            anchors.centerIn: parent
            spacing: Theme.spacingSmall

            ColoredIcon {
                Layout.preferredWidth: Theme.scaled(22)
                Layout.preferredHeight: Theme.scaled(22)
                source: modelData.icon
                iconWidth: Theme.scaled(22)
                iconHeight: Theme.scaled(22)
                iconColor: selected ? Theme.primaryColor : Theme.textSecondaryColor
            }
            Text {
                text: modelData.label
                font: Theme.bodyFont
                color: selected ? Theme.primaryColor : Theme.textColor
                Accessible.ignored: true
            }
        }

        AccessibleMouseArea {
            anchors.fill: parent
            accessibleName: modelData.label
            accessibleItem: parent
            accessibleChecked: selected
            onAccessibleClicked: tapped()
        }
    }
}
