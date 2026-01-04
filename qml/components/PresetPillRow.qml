import QtQuick
import QtQuick.Layouts
import DecenzaDE1

FocusScope {
    id: root

    property var presets: []
    property int selectedIndex: -1
    property int focusedIndex: 0  // Currently focused pill for keyboard nav
    property real maxWidth: Math.min(Theme.scaled(825), parent ? parent.width - Theme.scaled(24) : Theme.scaled(825))  // Clamp to parent width with margins

    signal presetSelected(int index)

    implicitHeight: contentColumn.implicitHeight
    implicitWidth: maxWidth

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

    // Cached rows model to avoid binding loops
    property var rowsModel: calculateRows()

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

    // Recalculate when presets change
    onPresetsChanged: rowsModel = calculateRows()
    onMaxWidthChanged: rowsModel = calculateRows()

    // Group presets into rows, distributing evenly BY WIDTH for balanced aesthetics
    function calculateRows() {
        if (presets.length === 0) return []

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
        if (totalWidth <= maxWidth) {
            var singleRow = []
            for (i = 0; i < presets.length; i++) {
                singleRow.push({index: i, preset: presets[i], width: pillWidths[i]})
            }
            return [singleRow]
        }

        // Calculate number of rows needed
        var numRows = Math.ceil(totalWidth / maxWidth)

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
            // 1. Adding this pill would exceed maxWidth AND row is not empty, OR
            // 2. Current row width is already >= target AND there are enough pills left for remaining rows
            var remainingPills = presets.length - i
            var remainingRows = numRows - rows.length
            var shouldStartNewRow = false

            if (currentRow.length > 0 && widthIfAdded > maxWidth) {
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
                        border.width: Theme.scaled(1)

                        Accessible.role: Accessible.Button
                        Accessible.name: (modelData.preset.name || "") + (isSelected ? ", " + TranslationManager.translate("presets.selected", "selected") : "")
                        Accessible.description: TranslationManager.translate("presets.doubleTapToSelect", "Double-tap to select.")
                        Accessible.focusable: true

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
                        }

                        AccessibleMouseArea {
                            anchors.fill: parent

                            accessibleName: {
                                if (!modelData || !modelData.preset) return ""
                                var name = modelData.preset.name || ""
                                var status = modelData.index === root.selectedIndex ? ", " + TranslationManager.translate("presets.selected", "selected") : ""
                                return name + status
                            }
                            accessibleItem: pill

                            onAccessibleClicked: {
                                if (!modelData || !modelData.preset) return
                                // Announce selection
                                if (typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled) {
                                    AccessibilityManager.announce(modelData.preset.name + " " + TranslationManager.translate("presets.selected", "selected"))
                                }
                                root.presetSelected(modelData.index)
                            }
                        }
                    }
                }
            }
        }
    }
}
