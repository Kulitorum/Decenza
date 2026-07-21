import QtQuick
import Decenza

// Shared grind/RPM dial-in control (replace-grind-inputs-with-picker), the one
// entry point for editing a grind value anywhere in the app. TAP-TO-OPEN in
// both presentations: the control displays the current value and opens
// GrindPickerDialog on tap — hosts keep no inline grind/RPM text inputs, and
// all typing happens inside the picker's text mode.
//
//   presentation: "pill"  — the brew bar's capsule: zone-aware fill, glass
//                           scrim over a background image, ratio-pill width
//                           parity. Visually unchanged from the pre-split
//                           GrindQuickSelectItem.
//   presentation: "field" — a bordered, field-shaped control matching
//                           StyledTextField's geometry (36 high, radius 8), so
//                           it drops in where a text input sat.
//
// The host supplies the VALUE (grindSetting / rpmValue) and the GRINDER
// IDENTITY that owns it (grinderBrand / grinderModel — the shot's grinder in
// post-shot review, the recipe's package in the wizard, the bag's equipment in
// the beans dialog, the active grinder only where the live dial-in is what is
// being edited). The control writes no Settings itself: commits surface as
// grindCommitted / rpmCommitted and the host applies them. An empty
// grindCommitted("") / rpmCommitted(0) is an EXPLICIT clear, and EVERY host
// applies it — emptying the field and pressing Done is as deliberate as typing
// a value, so dropping it would be a silent swallow. (An earlier draft exempted
// the brew-bar pill on the grounds that the live dial-in "had never been
// clearable"; that was preserving an artefact of a pill with no text entry, not
// a decision, and it silently ate real user clears.)
Item {
    id: root

    property string presentation: "field"

    // Grinder context — the grinder that OWNS the value, never assumed active.
    property string grinderBrand: ""
    property string grinderModel: ""

    // Committed values, host-supplied.
    property string grindSetting: ""
    property int rpmValue: 0

    // One capability function, this surface's own context (grind-value-entry).
    readonly property bool rpmCapable: rowSource.rpmCapable

    signal grindCommitted(string value)   // "" = explicit clear
    signal rpmCommitted(int rpm)          // 0  = explicit clear

    // pill-only zone styling (forwarded by the layout widget)
    property color zoneTextColor: Theme.textColor
    property string zoneStyle: "standard"

    // field-only
    property color fieldColor: Theme.insetBackgroundColor
    property string accessibleName: ""
    // Inline value text size — defaults to the ValueInput family's inline
    // size so the control reads as a sibling of the steppers it sits among.
    property int valueFontPixelSize: Theme.scaled(16)

    readonly property bool _isPill: presentation === "pill"
    readonly property bool showRpm: rpmCapable && rpmValue > 0

    // Combined display: "<grind> · <rpm>" when both halves are present; grind
    // alone, or (grind unset but rpm set) the rpm alone; "—" when neither.
    readonly property string valueText: {
        if (grindSetting.length > 0 && showRpm)
            return grindSetting + " · " + rpmValue
        if (grindSetting.length > 0)
            return grindSetting
        if (showRpm)
            return String(rpmValue)
        return TranslationManager.translate("grind.quickSelect.unset", "—")
    }
    // Spoken form for screen readers — announce both halves explicitly.
    readonly property string accessibleValue: {
        var parts = []
        if (grindSetting.length > 0)
            parts.push(grindSetting)
        if (showRpm)
            parts.push(rpmValue + " " + TranslationManager.translate("grind.quickSelect.rpmLabel", "RPM"))
        return parts.length > 0 ? parts.join(", ")
            : TranslationManager.translate("grind.quickSelect.unset", "—")
    }
    readonly property string _a11yName:
        (root.accessibleName.length > 0
            ? root.accessibleName
            : TranslationManager.translate("grind.quickSelect.label", "Grind"))
        + " " + root.accessibleValue + ". "
        + TranslationManager.translate("grind.quickSelect.tapToChange", "Tap to change")

    // Field width hugs its content (value + padding, with a floor so "—"
    // doesn't collapse to a sliver) — a tap-to-open VALUE chip, not a text
    // input that stretches to fill; hosts can still force Layout.fillWidth.
    implicitWidth: _isPill ? pill.implicitWidth
                           : Math.max(Theme.scaled(80),
                                      fieldText.implicitWidth + Theme.spacingMedium * 2)
    implicitHeight: _isPill ? Theme.scaled(32) : Theme.scaled(36)

    // Candidate/step source carrying the injected grinder context.
    readonly property GrindRowSource rowSource: GrindRowSource {
        grinderBrand: root.grinderBrand
        grinderModel: root.grinderModel
    }

    function openPicker() {
        picker.open()
    }

    // --- pill presentation (brew bar) ---
    Rectangle {
        id: pill
        visible: root._isPill
        anchors.centerIn: parent
        // Brew-bar symmetry: never narrower than a "1:X.X" Ratio Quick-Select
        // pill (same font + padding formula), so Grind and Ratio pills line up
        // at equal width side by side.
        implicitWidth: Math.max(pillValue.implicitWidth, pillRef.implicitWidth)
                       + Theme.spacingMedium * 2
        width: implicitWidth
        height: Theme.scaled(32)
        radius: height / 2
        // Always a visible chip: this pill is tappable, so it must read as a
        // button. Over a background image use the neutral glass scrim
        // (Theme.actionButtonFill); otherwise a zone-appropriate solid chip.
        readonly property bool hasGlassChrome: Theme.glassChrome
        readonly property color pillFill: Theme.actionButtonFill(Theme.zoneChipColor(root.zoneStyle))
        color: pillMa.pressed ? Qt.darker(pillFill, 1.15) : pillFill

        Accessible.role: Accessible.Button
        Accessible.name: root._a11yName
        Accessible.focusable: true
        Accessible.onPressAction: pillMa.clicked(null)

        // Hidden width reference: a representative ratio value in the identical
        // font, measured only (never drawn), for brew-bar width parity.
        Text {
            id: pillRef
            visible: false
            text: "1:2.0"
            font.pixelSize: Theme.scaled(20)
            font.bold: true
        }
        Text {
            id: pillValue
            anchors.centerIn: parent
            text: root.valueText
            // Accent-blue value reads on the solid chip; over a background
            // image the chip is the neutral glass scrim, so the value uses the
            // light zone text color (like the Sleep/Quit labels).
            color: pill.hasGlassChrome ? root.zoneTextColor : Theme.primaryColor
            font.pixelSize: Theme.scaled(20)
            font.bold: true
        }
        MouseArea { id: pillMa; anchors.fill: parent; onClicked: picker.open() }
    }

    // --- field presentation (form surfaces) ---
    Rectangle {
        id: field
        visible: !root._isPill
        anchors.fill: parent
        color: fieldMa.pressed ? Qt.darker(root.fieldColor, 1.1) : root.fieldColor
        // Match StyledTextField's geometry so the control reads as the same
        // family as the text fields it sits among.
        radius: Theme.scaled(8)
        border.color: Theme.textSecondaryColor
        border.width: 1

        Accessible.role: Accessible.Button
        Accessible.name: root._a11yName
        Accessible.focusable: true
        Accessible.onPressAction: fieldMa.clicked(null)

        // Centred bold value in the ValueInput family's style — this is a
        // value chip beside steppers showing "18.0g", not a form text input.
        Text {
            id: fieldText
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: Theme.scaled(10)
            anchors.rightMargin: Theme.scaled(10)
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideRight
            text: root.valueText
            color: root.grindSetting.length > 0 || root.showRpm
                ? Theme.textColor : Theme.textSecondaryColor
            font.pixelSize: root.valueFontPixelSize
            font.bold: true
        }
        MouseArea { id: fieldMa; anchors.fill: parent; onClicked: picker.open() }
    }

    GrindPickerDialog {
        id: picker
        rowSource: root.rowSource
        currentGrind: root.grindSetting
        currentRpm: root.rpmValue
        onGrindPicked: function(v) { root.grindCommitted(v) }
        onRpmPicked: function(v) { root.rpmCommitted(parseInt(v) || 0) }
    }
}
