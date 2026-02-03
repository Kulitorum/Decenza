import QtQuick
import QtQuick.Layouts
import DecenzaDE1

FocusScope {
    id: root

    property var presets: []
    property int selectedIndex: -1
    property int focusedIndex: 0  // Currently focused pill for keyboard nav
    property real maxWidth: Math.min(Theme.scaled(825), parent ? parent.width - Theme.scaled(24) : Theme.scaled(825))  // Clamp to parent width with margins
    property bool supportLongPress: false  // Enable long-press on pills

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
            var name = presets[focusedIndex].name || ""
            var status = focusedIndex === selectedIndex ? ", " + TranslationManager.translate("presets.selected", "selected") : ""
            AccessibilityManager.announce(name + status)
        }
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

    // Recalculate when presets or width changes (deferred via timer to avoid
    // destroying Repeater delegates during signal handler chains)
    onPresetsChanged: recalcTimer.restart()
    onEffectiveMaxWidthChanged: recalcTimer.restart()

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

        // First pass: calculate pill widths based on actual text width
        var pillWidths = []
        var totalWidth = 0
        for (var i = 0; i < presets.length; i++) {
            var textWidth = measureTextWidth(presets[i].name || "")
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

                        width: pillText.implicitWidth + root.pillPadding
                        height: Theme.scaled(50)
                        radius: Theme.scaled(10)

                        color: isSelected ? Theme.primaryColor : Theme.backgroundColor
                        border.color: isSelected ? Theme.primaryColor : Theme.textSecondaryColor
                        border.width: 1

                        // Accessibility: Let AccessibleTapHandler handle screen reader interaction
                        // to avoid duplicate focus elements
                        Accessible.ignored: true

                        Behavior on color { ColorAnimation { duration: 150 } }

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

                        Text {
                            id: pillText
                            anchors.centerIn: parent
                            text: modelData.preset.name || ""
                            color: pill.isSelected ? "white" : Theme.textColor
                            font.pixelSize: Theme.scaled(16)
                            font.bold: true
                            // Decorative - accessibility handled by AccessibleTapHandler
                            Accessible.ignored: true
                        }

                        // Using TapHandler for better touch responsiveness (avoids Flickable conflicts)
                        AccessibleTapHandler {
                            anchors.fill: parent
                            supportLongPress: root.supportLongPress

                            accessibleName: {
                                if (!modelData || !modelData.preset) return ""
                                var name = modelData.preset.name || ""
                                var status = modelData.index === root.selectedIndex ? ", " + TranslationManager.translate("presets.selected", "selected") : ""
                                return name + status
                            }
                            accessibleItem: pill

                            onAccessibleClicked: {
                                if (!modelData || !modelData.preset) return
                                // Announce selection for accessibility feedback
                                if (typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled) {
                                    AccessibilityManager.announce(modelData.preset.name + " " + TranslationManager.translate("presetPill.selected", "selected"))
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
