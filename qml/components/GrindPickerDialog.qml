import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decenza

// Grind/RPM value picker (replace-grind-inputs-with-picker), opened by
// GrindField on every surface that edits a dial-in. Two entry modes over the
// same pending value:
//
//   WHEELS (default)  — native picker wheels (Qt Tumbler), side by side under a
//                       shared selection band; candidates come from the host's
//                       GrindRowSource (history-derived step, grinder-notation
//                       aware). Fine -> coarse, top to bottom.
//   TEXT              — two text fields in the same slots, reached by the
//                       VISIBLE header toggle (no hidden gesture; the icon
//                       always shows the DESTINATION: keyboard glyph on the
//                       wheels, picker glyph on the fields). Grind is free
//                       text — notation is opaque ("C4", "3+2", "medium-fine")
//                       — RPM digits only. Switching back re-seeds the wheels
//                       CENTRED ON the typed value (never snapped to the old
//                       lattice). The dialog opens directly in text mode when
//                       the wheel would have fewer than two rows to offer (no
//                       value and no history, or an unparseable value with no
//                       history) — the create-a-bag / create-a-recipe path.
//
// Commit contract (unchanged): typing/spinning applies NOTHING; Done is the
// only commit path, Cancel discards, and Escape / tap-outside stay disabled
// (NoAutoClose) so a stray tap can't drop a half-made change. In text mode an
// empty grind or RPM at Done is an EXPLICIT clear — grindPicked("") /
// rpmPicked("") — and every host applies it, with no exceptions.
Dialog {
    id: root
    parent: Overlay.overlay
    // Explicit x/y instead of anchors.centerIn: a Popup's anchors group has
    // ONLY centerIn (no offsets — assigning verticalCenterOffset is a runtime
    // load error qmlcachegen does not catch), and the dialog must shift while
    // typing on mobile so the soft keyboard doesn't bury the Done button.
    // Centred normally; pinned near the top in mobile text entry. Event-driven
    // (focus state), no timers; desktop never shifts (no soft keyboard).
    x: parent ? (parent.width - width) / 2 : 0
    y: parent ? (root._mobileTyping ? Theme.scaled(12)
                                    : (parent.height - height) / 2) : 0
    // Narrow for a single Grind column; wider when the RPM column is present.
    width: Math.min(root._hasRpm ? Theme.scaled(420) : Theme.scaled(280),
                    parent ? parent.width * 0.95 : Theme.scaled(280))
    height: Math.min(Theme.scaled(480), parent ? parent.height * 0.92 : Theme.scaled(480))
    modal: true
    // Only Cancel / Done close the dialog — no tap-outside or Escape, so a
    // stray tap can't silently discard a half-made grind/RPM change.
    closePolicy: Popup.NoAutoClose
    padding: 0

    // --- Host API ---
    // The candidate/step source carrying the grinder context that OWNS the
    // value being edited (GrindRowSource — required).
    property var rowSource: null
    // The committed values the picker opens on.
    property string currentGrind: ""
    property int currentRpm: 0
    // Emitted only from Done. Strings travel verbatim; "" is an explicit clear.
    signal grindPicked(string value)
    signal rpmPicked(string value)

    // --- Pending state (nothing applies until Done) ---
    property bool textMode: false
    property string _pendingGrind: ""
    property string _pendingRpm: ""   // text form; "" = unset
    // Whether the user actually ENGAGED the RPM half this session. Gates the
    // wheel-branch commit — see _applyAndClose. Two independent signals, so a
    // miss in either still catches the engagement:
    //   _rpmTouched     — set on a user drag/flick (Tumbler.moving is
    //                     documented as user-driven only) or an RPM text edit.
    //   _rpmSnapIndex   — the row WE snapped to; a currentIndex that no longer
    //                     matches it means the user moved the wheel by ANY
    //                     means, keyboard included. (Tumbler has no `moved`
    //                     signal — assigning onMoved is a runtime load error
    //                     qmlcachegen does not catch.)
    property bool _rpmTouched: false
    property int _rpmSnapIndex: -1

    // True while the RPM wheel is parked on the SYNTHETIC anchor with nothing
    // committed — i.e. exactly when Done will NOT write an RPM. The centred row
    // is styled as a placeholder in that state: the accent styling means "this
    // is what Done commits", so an un-committed anchor must not wear it and
    // read as the current value. Mirrors the gate in _applyAndClose; goes false
    // the instant the user engages the wheel.
    readonly property bool _rpmPlaceholder:
        root._hasRpm && root.currentRpm <= 0 && !root._rpmTouched
        && (root._rpmSnapIndex < 0 || rpmTumbler.currentIndex === root._rpmSnapIndex)

    readonly property bool _hasRpm: rowSource ? rowSource.rpmCapable : false
    readonly property bool _mobileTyping:
        (Qt.platform.os === "android" || Qt.platform.os === "ios")
        && root.textMode && (grindText.activeFocus || rpmText.activeFocus)

    // Wheel candidates for the PENDING value, rebuilt as an explicit SNAPSHOT
    // (open / async-cache warm-up / text->wheel re-seed) rather than a reactive
    // binding: a binding regenerates the model UNDER the open Tumbler when the
    // distinct-value cache warms a moment after the first open, resetting the
    // view and animating hundreds of rows — the "wheel goes crazy on first
    // open" bug. Snapshots move only when we say so, and every move is
    // followed by a non-animated snap (_centerWheels).
    property var _grindRows: []
    property var _rpmRows: []

    function _rebuildRows() {
        root._grindRows = root.rowSource
            ? root.rowSource.grindRowsFor(root._pendingGrind) : []
        root._rpmRows = (root.rowSource && root.rowSource.rpmCapable)
            ? root.rowSource.rpmRowsFor(parseInt(root._pendingRpm) || 0) : []
    }

    // The step/history cache is async; when it warms while the dialog is open
    // (first open after app start), rebuild ONCE on the fresh step and snap
    // back to the current value — no animation, no reactive churn.
    readonly property Connections _cacheConn: Connections {
        target: root.rowSource
        function onDistinctCacheVersionChanged() {
            if (!root.opened)
                return   // next open rebuilds anyway
            root._rebuildRows()
            if (!root.textMode)
                Qt.callLater(root._centerWheels)
        }
    }

    // Index of the current value within a rows array (-1 if none is current).
    function _currentIndex(rows) {
        for (var i = 0; i < rows.length; i++)
            if (rows[i].isCurrent === true)
                return i
        return -1
    }

    // Resolve the Tumbler's REAL internal view. tumbler.contentItem is NOT the
    // ListView (despite the documented shape of a non-wrap Tumbler) — in Qt 6
    // it is a private TumblerView wrapper with none of the view API. Debug
    // logging proved it: contentY=undefined on the wrapper, so every
    // positionViewAtIndex/velocity call aimed at it was a silent no-op (QML
    // writes to nonexistent properties on a var reference do nothing), and the
    // raw currentIndex assignment animated alone at the default 400 px/s —
    // tens of seconds of travel across a wide window. The actual ListView
    // (wrap: false) is a CHILD of that wrapper; find it by duck-typing.
    function _view(tumbler) {
        var ci = tumbler.contentItem
        if (!ci)
            return null
        if (ci.positionViewAtIndex !== undefined)
            return ci
        for (var i = 0; i < ci.children.length; ++i)
            if (ci.children[i].positionViewAtIndex !== undefined)
                return ci.children[i]
        return null
    }

    function _snapTo(tumbler, index) {
        // An empty wheel has nothing to snap to (assigning would force -1).
        if (tumbler.count <= 0)
            return
        if (index < 0)
            index = 0
        // Kill the highlight animation BEFORE moving: ListView's default
        // highlightMoveVelocity is 400 px/s and a programmatic Tumbler index
        // change is a highlight move — jumping hundreds of rows at that
        // velocity is a minutes-long "the wheel spins on its own" animation.
        // Zero duration + unconstrained velocity = instant for programmatic
        // jumps; user flicks are Flickable physics and are unaffected.
        var lv = root._view(tumbler)
        if (!lv) {
            // Degrading here is not cosmetic: without the view we fall back to
            // a bare currentIndex assignment, which is the multi-minute
            // self-spinning animation this whole function exists to prevent
            // (~801 rows at the default 400 px/s). Say so — if a Qt upgrade
            // reshapes Tumbler's internals, this line is the only thing tying
            // the regression to its cause.
            console.warn("GrindPickerDialog: Tumbler internal view not found —"
                         + " snap disabled, wheel will animate (Qt internals changed?)")
        }
        if (lv) {
            if (lv.highlightMoveDuration !== undefined)
                lv.highlightMoveDuration = 0
            if (lv.highlightMoveVelocity !== undefined)
                lv.highlightMoveVelocity = -1
        }
        // ORDER MATTERS: position the content FIRST, then set currentIndex.
        // Setting currentIndex first starts the velocity-limited highlight
        // animation toward a far row; repositioning then fights it and
        // StrictlyEnforceRange re-animates — a visible tug-of-war. Positioned
        // first, the currentIndex assignment is a zero-distance move.
        if (lv)
            lv.positionViewAtIndex(index, ListView.Center)
        tumbler.currentIndex = index
        if (lv)
            lv.positionViewAtIndex(index, ListView.Center)
    }

    // Centre each wheel on its current row. A wheel with no current value (an
    // unset RPM) centres on the neutral ANCHOR row — found by value, not by
    // taking the middle: the window is wide and clamps at zero, so the middle
    // row is no longer the anchor.
    function _centerWheels() {
        var gi = root._currentIndex(root._grindRows)
        root._snapTo(grindTumbler, gi >= 0 ? gi : Math.floor(root._grindRows.length / 2))
        var ri = root._currentIndex(root._rpmRows)
        if (ri < 0 && root.rowSource) {
            var anchor = String(root.rowSource.rpmDefaultAnchor)
            for (var i = 0; i < root._rpmRows.length; i++)
                if (root._rpmRows[i].value === anchor) { ri = i; break }
        }
        const rpmIndex = ri >= 0 ? ri : Math.floor(root._rpmRows.length / 2)
        root._snapTo(rpmTumbler, rpmIndex)
        // Remember where we parked it, so a later currentIndex that differs is
        // proof the user moved it themselves.
        root._rpmSnapIndex = rpmTumbler.count > 0 ? rpmIndex : -1
    }

    // All setup happens BEFORE the dialog is visible, so its first frame
    // already shows the wheels sitting on the current value — no snap-into-
    // place flicker, no travel. onOpened repeats the (idempotent) snap once
    // in case the ListView finished layout only after showing.
    onAboutToShow: {
        root._rpmTouched = false
        root._rpmSnapIndex = -1
        root._pendingGrind = root.currentGrind
        root._pendingRpm = root.currentRpm > 0 ? String(root.currentRpm) : ""
        root._rebuildRows()
        // Auto text-mode: a wheel with fewer than two rows offers nothing to
        // spin. The GRIND half decides (RPM rows always generate from the
        // neutral anchor); both halves follow it together, matching the single
        // toggle. This replaces the old "set a grind value in Brew Settings
        // first" dead end — the empty state IS the on-ramp now.
        root.textMode = root._grindRows.length < 2
        grindText.text = root._pendingGrind
        rpmText.text = root._pendingRpm
        if (!root.textMode)
            root._centerWheels()
    }
    onOpened: {
        if (root.textMode)
            grindText.forceActiveFocus()
        else
            root._centerWheels()
    }

    // The header toggle. The icon names the DESTINATION, so switching is
    // self-describing in both directions.
    function _toggleMode() {
        if (root.textMode) {
            // Text -> wheels: adopt the typed values as pending; the wheels
            // re-seed centred on them. Commit the IME's in-progress word first
            // (QML gotcha: reading .text from a handler drops it otherwise).
            Qt.inputMethod.commit()
            root._pendingGrind = grindText.text.trim()
            root._pendingRpm = rpmText.text.trim()
            Qt.inputMethod.hide()
            root._rebuildRows()
            root.textMode = false
            Qt.callLater(root._centerWheels)
        } else {
            // Wheels -> text: seed the fields from what the wheels show now,
            // so both modes edit the same pending value.
            if (root._grindRows.length > 0 && grindTumbler.currentIndex >= 0
                    && grindTumbler.currentIndex < root._grindRows.length)
                root._pendingGrind = String(root._grindRows[grindTumbler.currentIndex].value)
            if (root._hasRpm && rpmTumbler.currentIndex >= 0
                    && rpmTumbler.currentIndex < root._rpmRows.length)
                root._pendingRpm = String(root._rpmRows[rpmTumbler.currentIndex].value)
            grindText.text = root._pendingGrind
            rpmText.text = root._pendingRpm
            root.textMode = true
            grindText.forceActiveFocus()
        }
    }

    // Apply whatever the active mode shows, then close — the only commit path.
    function _applyAndClose() {
        if (root.textMode) {
            Qt.inputMethod.commit()   // IME gotcha: flush the in-progress word
            root.grindPicked(grindText.text.trim())
            if (root._hasRpm)
                root.rpmPicked(rpmText.text.trim())
        } else {
            if (root._grindRows.length > 0 && grindTumbler.currentIndex >= 0
                    && grindTumbler.currentIndex < root._grindRows.length)
                root.grindPicked(String(root._grindRows[grindTumbler.currentIndex].value))
            // Commit RPM from the wheel ONLY when the half has a real basis:
            // the record already had an RPM (the wheel is parked on it, so an
            // untouched commit is a no-op), or the user engaged this wheel.
            // Otherwise the wheel is parked on the SYNTHETIC neutral anchor
            // (rpmDefaultAnchor, 1000) that _centerWheels seeds for an unset
            // RPM — committing that writes an RPM that never happened onto a
            // shot / bag / recipe, contradicting "only a seed, nothing written
            // until picked". The grind half needs no such gate: its parked
            // value is always REAL (the current value, or one of the user's own
            // observed history values), so committing what is visibly centred
            // is WYSIWYG rather than fabrication.
            const rpmMoved = root._rpmTouched
                || (root._rpmSnapIndex >= 0 && rpmTumbler.currentIndex !== root._rpmSnapIndex)
            if (root._hasRpm && (root.currentRpm > 0 || rpmMoved)
                    && rpmTumbler.currentIndex >= 0
                    && rpmTumbler.currentIndex < root._rpmRows.length)
                root.rpmPicked(String(root._rpmRows[rpmTumbler.currentIndex].value))
        }
        Qt.inputMethod.hide()
        root.close()
    }

    background: Rectangle {
        color: Theme.dialogBackgroundColor
        radius: Theme.cardRadius
        border.width: 1
        border.color: Theme.borderColor
    }

    // Shared wheel-row delegate: the centred item (displacement ~0) is bold and
    // accent-coloured; the rest fade with distance from the selection band.
    Component {
        id: wheelDelegate
        Item {
            required property var modelData
            required property int index
            readonly property real _dist: Math.abs(Tumbler.displacement)
            readonly property bool _centered: _dist < 0.5
            // An un-committed RPM anchor renders muted rather than accented —
            // see _rpmPlaceholder. Identified via the attached tumbler so the
            // delegate can stay shared between both wheels.
            readonly property bool _placeholder:
                Tumbler.tumbler === rpmTumbler && root._rpmPlaceholder
            Text {
                anchors.centerIn: parent
                text: String(modelData.value)
                color: (_centered && !_placeholder) ? Theme.primaryColor : Theme.textColor
                font.family: Theme.bodyFont.family
                font.pixelSize: Theme.bodyFont.pixelSize
                font.bold: _centered && !_placeholder
                opacity: (_centered && _placeholder ? 0.45 : 1.0)
                         - Math.min(0.72, _dist * 0.36)
            }
        }
    }

    contentItem: ColumnLayout {
        spacing: Theme.spacingMedium

        // --- Fixed header: title + subtitle, keyboard dismiss, mode toggle ---
        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: Theme.spacingLarge
            Layout.leftMargin: Theme.spacingLarge
            Layout.rightMargin: Theme.spacingLarge
            spacing: Theme.spacingSmall

            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.scaled(2)
                Text {
                    text: TranslationManager.translate("grind.picker.title", "Grind Setting")
                    color: Theme.textColor
                    font: Theme.titleFont
                }
                Text {
                    text: root.textMode
                        ? TranslationManager.translate("grind.picker.subtitleType", "Type a value, then Done")
                        : TranslationManager.translate("grind.picker.subtitle", "Spin to choose, then Done")
                    color: Theme.textSecondaryColor
                    font: Theme.labelFont
                }
            }

            // Modal dialogs need their own keyboard dismiss — the global one in
            // main.qml sits behind the modal overlay. Auto-hides on desktop and
            // when no text input has focus.
            HideKeyboardButton {}

            // The mode toggle: always visible, no hidden gesture. Keyboard
            // glyph on the wheels (goes to text), picker glyph on the fields
            // (goes back to the wheels).
            StyledIconButton {
                icon.source: root.textMode ? "qrc:/icons/picker-wheel.svg" : "qrc:/icons/keyboard.svg"
                accessibleName: root.textMode
                    ? TranslationManager.translate("grind.picker.wheelMode", "Back to picker")
                    : TranslationManager.translate("grind.picker.keyboardMode", "Type a value")
                onClicked: root._toggleMode()
            }
        }

        // --- Column labels (one per half, shared by both modes) ---
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spacingLarge
            Layout.rightMargin: Theme.spacingLarge
            spacing: Theme.spacingMedium
            Text {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                text: TranslationManager.translate("grind.quickSelect.label", "Grind").toUpperCase()
                color: Theme.textColor
                font: Theme.subtitleFont
            }
            Text {
                visible: root._hasRpm
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                text: TranslationManager.translate("grind.quickSelect.rpmLabel", "RPM").toUpperCase()
                color: Theme.textColor
                font: Theme.subtitleFont
            }
        }

        // --- Body ---
        Item {
            id: bodyArea
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: Theme.spacingLarge
            Layout.rightMargin: Theme.spacingLarge

            // Wheels: side by side under a shared selection band.
            Item {
                anchors.fill: parent
                visible: !root.textMode

                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    height: parent.height / grindTumbler.visibleItemCount
                    radius: Theme.buttonRadius
                    color: Theme.primaryColor
                    opacity: 0.14
                }

                RowLayout {
                    anchors.fill: parent
                    spacing: Theme.spacingMedium

                    Tumbler {
                        id: grindTumbler
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        model: root._grindRows
                        visibleItemCount: 5
                        wrap: false
                        delegate: wheelDelegate
                        Accessible.role: Accessible.Slider
                        Accessible.name: TranslationManager.translate("grind.quickSelect.label", "Grind")
                        // Announce the SELECTED value, not just the column
                        // name (see the RPM wheel below).
                        Accessible.description: (root._grindRows.length > 0
                            && grindTumbler.currentIndex >= 0
                            && grindTumbler.currentIndex < root._grindRows.length)
                            ? String(root._grindRows[grindTumbler.currentIndex].value) : ""
                    }

                    Tumbler {
                        id: rpmTumbler
                        visible: root._hasRpm
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        model: root._rpmRows
                        visibleItemCount: 5
                        wrap: false
                        delegate: wheelDelegate
                        // Tumbler.moving is documented as user-driven only
                        // (dragging or flicking), so it never fires for the
                        // programmatic snap in _centerWheels.
                        onMovingChanged: if (rpmTumbler.moving) root._rpmTouched = true
                        Accessible.role: Accessible.Slider
                        Accessible.name: TranslationManager.translate("grind.quickSelect.rpmLabel", "RPM")
                        // Announce the SELECTED value, not just the column
                        // name — a screen-reader user spinning the wheel is
                        // otherwise told nothing about what they landed on.
                        Accessible.description: (root._rpmRows.length > 0
                            && rpmTumbler.currentIndex >= 0
                            && rpmTumbler.currentIndex < root._rpmRows.length)
                            ? String(root._rpmRows[rpmTumbler.currentIndex].value) : ""
                    }
                }
            }

            // Text mode: the same two slots as editable fields. Grind is free
            // text (notation is opaque; nothing is validated — the wheel is an
            // accelerator over the text value, never a gate on it). RPM is
            // digits only.
            RowLayout {
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.topMargin: Theme.spacingMedium
                spacing: Theme.spacingMedium
                visible: root.textMode

                StyledTextField {
                    id: grindText
                    Layout.fillWidth: true
                    horizontalAlignment: TextInput.AlignHCenter
                    accessibleName: TranslationManager.translate("grind.quickSelect.label", "Grind")
                    Keys.onReturnPressed: root._applyAndClose()
                    Keys.onEnterPressed: root._applyAndClose()
                }

                StyledTextField {
                    id: rpmText
                    visible: root._hasRpm
                    Layout.fillWidth: true
                    horizontalAlignment: TextInput.AlignHCenter
                    onTextEdited: root._rpmTouched = true
                    inputMethodHints: Qt.ImhDigitsOnly
                    validator: RegularExpressionValidator { regularExpression: /\d{0,5}/ }
                    accessibleName: TranslationManager.translate("grind.quickSelect.rpmLabel", "RPM")
                    Keys.onReturnPressed: root._applyAndClose()
                    Keys.onEnterPressed: root._applyAndClose()
                }
            }
        }

        // --- Fixed footer: Cancel dismisses, Done applies the active mode ---
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spacingLarge
            Layout.rightMargin: Theme.spacingLarge
            Layout.bottomMargin: Theme.spacingLarge
            spacing: Theme.spacingMedium

            // Cancel — secondary style; closes with no change.
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.scaled(48)
                radius: Theme.buttonRadius
                color: cancelMa.pressed ? Qt.darker(Theme.surfaceColor, 1.1) : "transparent"
                border.width: 1
                border.color: Theme.borderColor
                Accessible.role: Accessible.Button
                Accessible.name: TranslationManager.translate("common.button.cancel", "Cancel")
                Accessible.focusable: true
                Accessible.onPressAction: cancelMa.clicked(null)
                Text {
                    anchors.centerIn: parent
                    text: TranslationManager.translate("common.button.cancel", "Cancel")
                    color: Theme.textColor
                    font: Theme.bodyFont
                }
                MouseArea {
                    id: cancelMa
                    anchors.fill: parent
                    onClicked: { Qt.inputMethod.hide(); root.close() }
                }
            }

            // Done — primary style; applies the active mode then closes.
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.scaled(48)
                radius: Theme.buttonRadius
                color: doneMa.pressed ? Qt.darker(Theme.primaryColor, 1.15) : Theme.primaryColor
                Accessible.role: Accessible.Button
                Accessible.name: TranslationManager.translate("common.button.done", "Done")
                Accessible.focusable: true
                Accessible.onPressAction: doneMa.clicked(null)
                Text {
                    anchors.centerIn: parent
                    text: TranslationManager.translate("common.button.done", "Done")
                    color: Theme.primaryContrastColor
                    font: Theme.bodyFont
                }
                MouseArea { id: doneMa; anchors.fill: parent; onClicked: root._applyAndClose() }
            }
        }
    }
}
