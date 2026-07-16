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
                    { value: "sour",     label: trSour.text },
                    { value: "balanced", label: trBalanced.text },
                    { value: "bitter",   label: trBitter.text }
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
                    { value: "thin",   label: trThin.text },
                    { value: "medium", label: trMedium.text },
                    { value: "heavy",  label: trHeavy.text }
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
        Rectangle {
            required property var modelData
            readonly property bool selected: root.tasteBalance === modelData.value
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.scaled(44)
            radius: Theme.scaled(6)
            color: selected ? Theme.primaryColor : Theme.surfaceColor
            border.color: selected ? Theme.primaryColor : Theme.borderColor
            border.width: 1

            Text {
                anchors.centerIn: parent
                text: parent.modelData.label
                font: Theme.bodyFont
                color: parent.selected ? Theme.primaryContrastColor : Theme.textColor
                Accessible.ignored: true
            }

            AccessibleMouseArea {
                anchors.fill: parent
                accessibleName: parent.modelData.label
                accessibleItem: parent
                accessibleChecked: parent.selected
                onAccessibleClicked: {
                    var v = parent.selected ? "" : parent.modelData.value
                    root.tasteBalance = v
                    root.tasteBalanceModified(v)
                }
            }
        }
    }

    // Body chip: sets tasteBody (toggles off if re-tapped).
    Component {
        id: bodyChipComponent
        Rectangle {
            required property var modelData
            readonly property bool selected: root.tasteBody === modelData.value
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.scaled(44)
            radius: Theme.scaled(6)
            color: selected ? Theme.primaryColor : Theme.surfaceColor
            border.color: selected ? Theme.primaryColor : Theme.borderColor
            border.width: 1

            Text {
                anchors.centerIn: parent
                text: parent.modelData.label
                font: Theme.bodyFont
                color: parent.selected ? Theme.primaryContrastColor : Theme.textColor
                Accessible.ignored: true
            }

            AccessibleMouseArea {
                anchors.fill: parent
                accessibleName: parent.modelData.label
                accessibleItem: parent
                accessibleChecked: parent.selected
                onAccessibleClicked: {
                    var v = parent.selected ? "" : parent.modelData.value
                    root.tasteBody = v
                    root.tasteBodyModified(v)
                }
            }
        }
    }
}
