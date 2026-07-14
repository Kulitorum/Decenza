import QtQuick
import QtQuick.Effects
import QtQuick.Layouts
import Decenza

FocusScope {
    id: root

    property var presets: []
    property int selectedIndex: -1
    property int focusedIndex: 0  // Currently focused pill for keyboard nav
    property real maxWidth: Math.min(Theme.scaled(825), parent ? parent.width - Theme.scaled(24) : Theme.scaled(825))  // Clamp to parent width with margins
    property bool supportLongPress: false  // Enable long-press on pills
    property var pillSuffixFn: null  // Optional: function(index) => string suffix appended to pill text (e.g. " (125g)")
    property int pillSuffixVersion: 0  // Increment from outside to force pill text refresh without full layout recalc
    property real pillSuffixMaxWidth: 0  // Reserve extra horizontal space per pill for the suffix
    property var pillLabelFn: null  // Optional: function(index, name) => transformed base label (e.g. append " Pitcher")
    // When true AND a pill is selected, append an "unsaved" marker to that pill.
    // Callers opt in by binding this to their own dirty state — e.g.
    // ProfileManager.profileModified for the espresso row. Rows that have no
    // dirty-state concept (steam / flush / hot water / beans) leave this at the
    // default false. (The beans row lost its dirty state when bags replaced
    // presets — bag edits write through, so there is nothing unsaved.)
    property bool modified: false
    // When modified, format as "Name (modified)" for read-only presets; otherwise "*Name".
    property bool modifiedIsReadOnly: false
    // Optional per-preset icon: a preset may carry an `icon` url (e.g. the
    // recipe pills' drink-type icon, add-recipe-wizard-tea). Presets without
    // one render exactly as before. Colorized to match the pill text.
    readonly property real pillIconSize: Theme.scaled(20)

    // Effective max width - ensures we never exceed parent width even if maxWidth is larger
    readonly property real effectiveMaxWidth: {
        var parentW = parent ? parent.width : 0
        if (parentW > 0 && maxWidth > 0) {
            return Math.min(maxWidth, parentW)
        }
        return maxWidth > 0 ? maxWidth : Theme.scaled(825)
    }

    signal presetSelected(int index)
    signal presetLongPressed(int index)

    implicitHeight: contentColumn.implicitHeight
    implicitWidth: effectiveMaxWidth
    width: effectiveMaxWidth

    // Keyboard navigation
    activeFocusOnTab: true

    Keys.onLeftPressed: {
        if (focusedIndex > 0) focusedIndex--
        announceCurrentPill()
    }
    Keys.onRightPressed: {
        if (focusedIndex < presets.length - 1) focusedIndex++
        announceCurrentPill()
    }
    Keys.onReturnPressed: presetSelected(focusedIndex)
    Keys.onEnterPressed: presetSelected(focusedIndex)
    Keys.onSpacePressed: presetSelected(focusedIndex)

    // Announce pill when focused
    onActiveFocusChanged: {
        if (activeFocus) announceCurrentPill()
    }

    function announceCurrentPill() {
        if (typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled && presets.length > 0) {
            // Route through pillLayoutName so keyboard/switch-access announcements match what
            // touch/screen-reader-tap users hear (e.g. pillLabelFn's "Small Pitcher" transform).
            var name = pillLayoutName(focusedIndex)
            var modifiedText = (root.modified && focusedIndex === selectedIndex) ? ", " + TranslationManager.translate("presets.unsaved", "unsaved changes") : ""
            var status = focusedIndex === selectedIndex ? ", " + TranslationManager.translate("presets.selected", "selected") : ""
            var hint = (presets[focusedIndex] && presets[focusedIndex].stateHint)
                ? ", " + presets[focusedIndex].stateHint : ""
            AccessibilityManager.announce(name + modifiedText + status + hint)
        }
    }

    // Base name for layout calculation (no live suffix — avoids layout recalc on every scale tick)
    function pillLayoutName(index) {
        var name = presets[index] ? (presets[index].name || "") : ""
        if (pillLabelFn) name = pillLabelFn(index, name)
        if (modified && index === selectedIndex) {
            name = modifiedIsReadOnly
                ? name + " " + TranslationManager.translate("presets.modified", "(modified)")
                : "*" + name
        }
        return name
    }

    // Display name for pill text — includes live suffix. Reads pillSuffixVersion so bindings
    // update when the parent increments it (e.g. on scale weight change).
    function pillDisplayName(index) {
        var _ = pillSuffixVersion  // Track for reactivity
        var name = pillLayoutName(index)
        if (pillSuffixFn) {
            var suffix = pillSuffixFn(index)
            if (suffix) name = name + suffix
        }
        return name
    }

    // Calculate how many pills fit per row
    readonly property real pillSpacing: Theme.scaled(12)
    readonly property real pillPadding: Theme.scaled(40)  // Horizontal padding inside pill

    // Cached rows model - populated by recalcTimer, not a binding
    property var rowsModel: []

    // Hidden TextMetrics for measuring pill text widths
    TextMetrics {
        id: textMetrics
        font.pixelSize: Theme.scaled(16)
        font.bold: true
    }

    function measureTextWidth(text) {
        textMetrics.text = text
        return textMetrics.width
    }

    // Recalculate when presets, width, modified state, or suffix changes (deferred
    // via timer to avoid destroying Repeater delegates during signal handler chains)
    onPresetsChanged: recalcTimer.restart()
    onEffectiveMaxWidthChanged: recalcTimer.restart()
    onPillSuffixFnChanged: recalcTimer.restart()
    onPillLabelFnChanged: recalcTimer.restart()
    // Dirty-state changes alter pill widths ("*Name" / " (modified)") so they trigger a
    // layout recalc. Because `modified` is a bindable property, we get changes from any
    // upstream source (ProfileManager, Settings, etc.) via the QML binding system without
    // a direct signal subscription here.
    onModifiedChanged: recalcTimer.restart()
    onModifiedIsReadOnlyChanged: recalcTimer.restart()

    // All model recalculations go through this timer to coalesce rapid changes
    // and ensure delegates aren't destroyed while their signal handlers run
    Timer {
        id: recalcTimer
        interval: 1
        onTriggered: rowsModel = calculateRows()
    }
    Component.onCompleted: recalcTimer.start()

    // Group presets into rows, distributing evenly BY WIDTH for balanced aesthetics
    function calculateRows() {
        if (presets.length === 0) return []

        // Use effective max width for calculations
        var availableWidth = effectiveMaxWidth
        if (availableWidth <= 0) {
            // Width not yet determined, will recalculate when layout completes
            return []
        }

        // First pass: calculate pill widths based on layout name (no live suffix — avoid recalc on scale ticks)
        // Reserve pillSuffixMaxWidth extra space per pill when a suffix function is provided
        var pillWidths = []
        var totalWidth = 0
        for (var i = 0; i < presets.length; i++) {
            var textWidth = measureTextWidth(pillLayoutName(i)) + (pillSuffixFn ? pillSuffixMaxWidth : 0)
            if (presets[i] && presets[i].icon)
                textWidth += pillIconSize + Theme.scaled(6)
            var pillWidth = textWidth + pillPadding
            pillWidths.push(pillWidth)
            totalWidth += pillWidth
        }
        // Add spacing between pills
        totalWidth += (presets.length - 1) * pillSpacing

        // If everything fits on one row, just return it
        if (totalWidth <= availableWidth) {
            var singleRow = []
            for (i = 0; i < presets.length; i++) {
                singleRow.push({index: i, preset: presets[i], width: pillWidths[i]})
            }
            return [singleRow]
        }

        // Calculate number of rows needed
        var numRows = Math.ceil(totalWidth / availableWidth)

        // Target width per row (distribute evenly by width, not count)
        var targetRowWidth = totalWidth / numRows

        // Fill rows by width, not count
        var rows = []
        var currentRow = []
        var currentRowWidth = 0

        for (i = 0; i < presets.length; i++) {
            var pillWidth = pillWidths[i]
            var spacingNeeded = currentRow.length > 0 ? pillSpacing : 0
            var widthIfAdded = currentRowWidth + spacingNeeded + pillWidth

            // Start new row if:
            // 1. Adding this pill would exceed availableWidth AND row is not empty, OR
            // 2. Current row width is already >= target AND there are enough pills left for remaining rows
            var remainingPills = presets.length - i
            var remainingRows = numRows - rows.length
            var shouldStartNewRow = false

            if (currentRow.length > 0 && widthIfAdded > availableWidth) {
                // Would overflow - must start new row
                shouldStartNewRow = true
            } else if (currentRow.length > 0 && currentRowWidth >= targetRowWidth * 0.9 && remainingPills >= remainingRows) {
                // Row is close to target width and we have enough pills for remaining rows
                shouldStartNewRow = true
            }

            if (shouldStartNewRow) {
                rows.push(currentRow)
                currentRow = []
                currentRowWidth = 0
                spacingNeeded = 0

                // Recalculate target for remaining pills
                var remainingWidth = 0
                for (var j = i; j < presets.length; j++) {
                    remainingWidth += pillWidths[j]
                    if (j > i) remainingWidth += pillSpacing
                }
                remainingRows = numRows - rows.length
                if (remainingRows > 0) {
                    targetRowWidth = remainingWidth / remainingRows
                }
            }

            currentRow.push({index: i, preset: presets[i], width: pillWidth})
            currentRowWidth += spacingNeeded + pillWidth
        }

        if (currentRow.length > 0) {
            rows.push(currentRow)
        }

        return rows
    }

    Column {
        id: contentColumn
        anchors.horizontalCenter: parent.horizontalCenter
        spacing: Theme.scaled(8)

        Repeater {
            model: root.rowsModel

            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: root.pillSpacing

                Repeater {
                    model: modelData

                    Rectangle {
                        id: pill

                        property bool isSelected: modelData.index === root.selectedIndex
                        property bool isFocused: root.activeFocus && modelData.index === root.focusedIndex
                        // A preset may mark itself as disabled (e.g. a steam "Off" preset
                        // that stops heating instead of setting a temp/time). Render it
                        // muted so it's visibly distinct from real time/temp presets.
                        property bool isDisabled: modelData && modelData.preset && modelData.preset.disabled === true
                        // A preset may mark itself dimmed (e.g. a stale recipe whose
                        // linked bag is finished): rendered faded but fully tappable —
                        // an indication, never a lock (recipe-bag-lifecycle). An
                        // optional `stateHint` string carries the reason to screen
                        // readers (the dimming alone is invisible to them).
                        property bool isDimmed: modelData && modelData.preset && modelData.preset.dimmed === true
                        property string stateHint: (modelData && modelData.preset && modelData.preset.stateHint) || ""

                        width: pillText.implicitWidth + root.pillPadding
                        height: Theme.scaled(50)
                        radius: Theme.scaled(10)
                        opacity: isDimmed ? 0.55 : 1.0

                        color: isSelected
                            ? (isDisabled ? Theme.textSecondaryColor : Theme.primaryColor)
                            : Theme.insetBackgroundColor
                        border.color: isSelected && !isDisabled ? Theme.primaryColor : Theme.textSecondaryColor
                        border.width: 1

                        // Accessibility: Let AccessibleTapHandler handle screen reader interaction
                        // to avoid duplicate focus elements
                        Accessible.ignored: true

                        Behavior on color { ColorAnimation { duration: 150 } }

                        // Selected indicator: a contrasting ring derived from the pill's own
                        // colour — lighter in dark mode, darker in light mode — so the active
                        // preset reads clearly as selected against both the fill and the page
                        // background, matching the active action-button highlight.
                        Rectangle {
                            anchors.fill: parent
                            visible: pill.isSelected && !pill.isDisabled
                            color: "transparent"
                            border.width: Theme.scaled(3)
                            border.color: Settings.theme.isDarkMode ? Qt.lighter(pill.color, 1.6) : Qt.darker(pill.color, 1.5)
                            radius: parent.radius
                        }

                        // Focus indicator
                        Rectangle {
                            anchors.fill: parent
                            anchors.margins: -Theme.focusMargin
                            visible: pill.isFocused
                            color: "transparent"
                            border.width: Theme.focusBorderWidth
                            border.color: Theme.focusColor
                            radius: parent.radius + Theme.focusMargin
                        }

                        Row {
                            anchors.centerIn: parent
                            spacing: Theme.scaled(6)
                            Image {
                                visible: !!(modelData.preset && modelData.preset.icon)
                                anchors.verticalCenter: parent.verticalCenter
                                source: (modelData.preset && modelData.preset.icon) || ""
                                sourceSize.width: root.pillIconSize
                                sourceSize.height: root.pillIconSize
                                fillMode: Image.PreserveAspectFit
                                Accessible.ignored: true
                                layer.enabled: true
                                layer.smooth: true
                                layer.effect: MultiEffect {
                                    colorization: 1.0
                                    colorizationColor: pill.isSelected ? Theme.primaryContrastColor : Theme.textColor
                                }
                            }
                            Text {
                                id: pillText
                                anchors.verticalCenter: parent.verticalCenter
                                text: pillDisplayName(modelData.index)
                                color: pill.isSelected ? Theme.primaryContrastColor : Theme.textColor
                                font.pixelSize: Theme.scaled(16)
                                font.bold: true
                                // Decorative - accessibility handled by AccessibleTapHandler
                                Accessible.ignored: true
                            }
                        }

                        // Using TapHandler for better touch responsiveness (avoids Flickable conflicts)
                        AccessibleTapHandler {
                            anchors.fill: parent
                            supportLongPress: root.supportLongPress

                            accessibleName: {
                                if (!modelData || !modelData.preset) return ""
                                var name = pillDisplayName(modelData.index)
                                var modifiedText = (root.modified && modelData.index === root.selectedIndex) ? ", " + TranslationManager.translate("presets.unsaved", "unsaved changes") : ""
                                var status = modelData.index === root.selectedIndex ? ", " + TranslationManager.translate("presets.selected", "selected") : ""
                                var hint = pill.stateHint !== "" ? ", " + pill.stateHint : ""
                                return name + modifiedText + status + hint
                            }
                            accessibleItem: pill

                            onAccessibleClicked: {
                                if (!modelData || !modelData.preset) return
                                // Announce selection for accessibility feedback. Route through
                                // pillDisplayName so the tap announcement matches what focus
                                // announces (e.g. pillLabelFn's "Small Pitcher" transform).
                                // pillDisplayName reads the LIVE presets list with this row's
                                // index, which the 1ms rowsModel rebuild can leave stale after
                                // a deletion/reorder — fall back to the row's snapshot name
                                // rather than announcing nothing.
                                if (typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled) {
                                    var announceName = pillDisplayName(modelData.index) || modelData.preset.name
                                    AccessibilityManager.announce(announceName + " " + TranslationManager.translate("presetPill.selected", "selected"))
                                }
                                root.presetSelected(modelData.index)
                            }

                            onAccessibleLongPressed: {
                                if (!modelData || !modelData.preset) return
                                root.presetLongPressed(modelData.index)
                            }
                        }
                    }
                }
            }
        }
    }
}
