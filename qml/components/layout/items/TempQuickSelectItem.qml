import QtQuick
import QtQuick.Layouts
import Decenza

// Layout widget: brew-temperature quick-select (composable-brew-bar). The
// temperature sibling of RatioQuickSelectItem / GrindQuickSelectItem — same base
// (LayoutWidgetItem) and same pill styling; each ships its own value picker (the
// pickers don't share code).
//
// Shows the effective brew temperature as a pill; when that temperature is within
// the quick-select range (see rows) tapping opens a value picker of up to 11
// temperatures at ±5 steps around the current one. Tapping a value arms it as the
// brew-temperature override AND pushes it to the machine via
// ProfileManager.applyTemperatureOverride() — the canonical writer, which does a
// set-or-clear against the profile baseline and re-uploads the profile, so the
// same override the shipped temperature UI (TemperatureItem, Brew Settings, the
// Shot Plan) reads takes effect on the DE1 immediately (a bare override write only
// lands on the next profile upload). When the temperature is OUTSIDE that range
// (e.g. a tea or calibration profile), the pill is a non-interactive read-out
// rather than a button over an empty picker.
//
// Values are stepped and STORED in Celsius (the internal unit); the pill and
// the picker DISPLAY them in the user's unit via Theme.formatTemperature, the
// same helper TemperatureItem uses. The step is a fixed 0.5 °C (see tempStepC) —
// there is no user setting for it.
LayoutWidgetItem {
    id: root
    readonly property string labelText: TranslationManager.translate("temp.quickSelect.label", "Temp")

    // The effective brew temperature (Celsius): the override when set, otherwise
    // the active profile's target temperature — the same source TemperatureItem
    // and Brew Settings use.
    readonly property double effectiveTempC: Settings.brew.hasTemperatureOverride
        ? Settings.brew.temperatureOverride
        : ProfileManager.profileTargetTemperature
    readonly property string valueText: root.effectiveTempC > 0
        ? Theme.formatTemperature(root.effectiveTempC, 1) : "—"

    // Step between offered values (°C). Fixed — no user setting (ratio uses fixed
    // presets, grind is history-derived, so neither sibling exposes one either).
    readonly property real tempStepC: 0.5

    // Override state against the active baseline — the session deviates from the
    // active recipe's / profile's designed temperature (recipe-baseline-not-
    // override). Shared with Brew Settings, the Shot Plan and TemperatureItem.
    readonly property bool overridden: MainController.temperatureIsRealOverride

    implicitWidth: col.implicitWidth
    implicitHeight: col.implicitHeight

    // Up to 11 rows for n = -5..+5 around the current temperature, clamped to the
    // brew-temperature range (70–100 °C — the same window the shipped override
    // editor uses, BrewDialog.qml) and de-duplicated. Near a bound fewer than 11
    // rows are produced; if the current temperature itself falls outside the range
    // (a tea / calibration profile) NO row is marked isCurrent and the ladder may
    // be empty — canQuickSelect gates the pill on exactly that. Each row:
    //   { value: <Celsius double>, label: <display string>, isCurrent: bool }.
    // Ordered cool -> hot (ascending Celsius). The labels come from
    // Theme.formatTemperature, which reads Settings.app.temperatureUnit in JS, so
    // this binding already re-evaluates on a unit change.
    readonly property var rows: {
        var cur = root.effectiveTempC
        var step = root.tempStepC
        var out = []
        var seen = ({})
        for (var n = -5; n <= 5; n++) {
            var v = cur + n * step
            if (!(v >= 70 && v <= 100)) continue    // clamp to the brew range (NaN-safe)
            var key = v.toFixed(2)                   // fold float dirt + de-duplicate
            if (seen[key]) continue
            seen[key] = true
            out.push({ value: v, label: Theme.formatTemperature(v, 1), isCurrent: n === 0 })
        }
        return out
    }
    // The quick-select only makes sense when the current temperature is itself in
    // range, i.e. the ladder actually contains it. Otherwise the pill is a plain
    // read-out (see the MouseArea/Accessible guards) so it never opens an empty or
    // currentless picker — the two guards can't drift because both key off rows.
    readonly property bool canQuickSelect: root.rows.some(function(r) { return r.isCurrent })

    function applyValueC(v) {
        // The canonical writer: set-or-clear the override against the profile
        // baseline (tapping the profile's own temperature disarms it) AND push it
        // to the machine. A bare Settings.brew.temperatureOverride write would not
        // reach the DE1 until the next profile upload.
        ProfileManager.applyTemperatureOverride(v)
    }

    ColumnLayout {
        id: col
        anchors.centerIn: parent
        width: parent.width
        spacing: Theme.scaled(2)

        Text {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideRight
            text: root.labelText
            color: root.zoneTextColor
            font: Theme.labelFont
        }

        Rectangle {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: tempValue.implicitWidth + Theme.spacingMedium * 2
            Layout.preferredHeight: Theme.scaled(32)
            radius: height / 2
            // Always a visible chip: this pill is tappable (opens the picker), so
            // it must read as a button. Mirrors RatioQuickSelectItem exactly — a
            // neutral glass scrim over a background image (Theme.actionButtonFillOn,
            // which also lets a chooser preview supply a candidate fill), else a
            // zone-appropriate solid chip (Theme.zoneChipColor).
            readonly property bool hasGlassChrome: Theme.glassChrome
            readonly property color pillFill: Theme.actionButtonFillOn(Theme.zoneChipColor(root.zoneStyle),
                                                                       root.zoneFillOverride)
            color: (tempMa.pressed && root.canQuickSelect) ? Qt.darker(pillFill, 1.15) : pillFill
            // Dim to a plain read-out when the current temperature is out of the
            // quick-select range (no ladder to open).
            opacity: root.canQuickSelect ? 1.0 : 0.55

            Accessible.role: root.canQuickSelect ? Accessible.Button : Accessible.StaticText
            Accessible.name: root.canQuickSelect
                ? root.labelText + " " + root.valueText + ". "
                  + TranslationManager.translate("temp.quickSelect.tapToChange", "Tap to change")
                : root.labelText + " " + root.valueText
            Accessible.focusable: true
            Accessible.onPressAction: { if (root.canQuickSelect) tempMa.clicked(null) }

            Text {
                id: tempValue
                anchors.centerIn: parent
                text: root.valueText
                Accessible.ignored: true   // the pill Rectangle carries Accessible.name
                // Override-highlight when the session deviates from the active
                // baseline — consistent with Brew Settings and the Shot Plan, and
                // wins in both modes. Over a background image the accent value
                // reads against the neutral glass scrim; otherwise the primary.
                color: root.overridden
                    ? Theme.highlightColor
                    : (parent.hasGlassChrome ? root.zoneTextColor : Theme.primaryColor)
                font.pixelSize: Theme.scaled(20)
                font.bold: true
            }
            MouseArea {
                id: tempMa
                anchors.fill: parent
                enabled: root.canQuickSelect   // inert read-out when the temp is out of range
                onClicked: tempDialog.open()
            }
        }
    }

    TempPickerDialog {
        id: tempDialog
        rows: root.rows
        onValuePicked: function(v) { root.applyValueC(v) }
    }
}
