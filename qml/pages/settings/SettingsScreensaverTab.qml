// The day-button delegate reads this file's `autoWakeCard` id; Bound makes it
// statically resolvable. All three delegates here declare every injected role they use
// required in the same edit -- without that, Bound stops role injection and the day
// buttons, the category list and the overlay chips all render blank at RUNTIME,
// silently. (`categoryList` is read only from sibling scopes, which need no pragma.)
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decenza

Item {
    id: screensaverTab

    // Track pending screensaver type change for dialog flow
    property string pendingScreensaverType: ""
    property var pendingScreensaverOperationIds: []
    readonly property var screensaverPreferences: SettingsPreferencesService.preferences

    function requestServiceOperation(operation) {
        let requestId = ""
        switch (operation) {
        case "clear-cache": requestId = ScreensaverService.clearVideoCache(); break
        case "clear-personal": requestId = ScreensaverService.clearPersonalMedia(); break
        case "refresh-categories": requestId = ScreensaverService.refreshCategories(); break
        case "refresh-catalog": requestId = ScreensaverService.refreshCatalog(); break
        }
        // ScreensaverService queues all completion paths, including unavailable
        // and busy failures, so the returned ID is recorded before onCompleted runs.
        if (requestId)
            pendingScreensaverOperationIds = pendingScreensaverOperationIds.concat([requestId])
        return requestId
    }

    function screenPreference(name, fallback) {
        const key = "screensaver." + name
        const value = screensaverPreferences[key]
        return screensaverPreferenceWrite.displayedValue(name, value === undefined ? fallback : value)
    }

    function writeScreenPreference(name, value) {
        if (!screensaverPreferenceWrite.ready) {
            preferenceError.show(TranslationManager.translate("common.settingsNotReady", "Settings are not connected yet."))
            return false
        }
        return screensaverPreferenceWrite.submit(name, value)
    }

    function updateAutoWakeDay(dayIndex, values) {
        const current = screenPreference("autoWakeSchedule", [])
        if (!Array.isArray(current) || current.length !== 7 || dayIndex < 0 || dayIndex >= 7)
            return false
        const schedule = current.map(function(day) {
            return { enabled: day.enabled, hour: day.hour, minute: day.minute }
        })
        schedule[dayIndex] = {
            enabled: values.enabled === undefined ? schedule[dayIndex].enabled : values.enabled,
            hour: values.hour === undefined ? schedule[dayIndex].hour : values.hour,
            minute: values.minute === undefined ? schedule[dayIndex].minute : values.minute
        }
        return writeScreenPreference("autoWakeSchedule", schedule)
    }

    PreferenceWrite {
        id: screensaverPreferenceWrite
        domain: "screensaver"
        onFailed: function(message) { preferenceError.show(message) }
    }
    StatusToast { id: preferenceError; error: true }

    Connections {
        target: ScreensaverService
        function onCompleted(requestId, operation, success, result, reason) {
            const pendingIndex = screensaverTab.pendingScreensaverOperationIds.indexOf(requestId)
            if (pendingIndex < 0)
                return
            const pendingIds = screensaverTab.pendingScreensaverOperationIds.slice()
            pendingIds.splice(pendingIndex, 1)
            screensaverTab.pendingScreensaverOperationIds = pendingIds
            if (!success)
                preferenceError.show(reason || TranslationManager.translate("common.error", "Operation failed"))
        }
    }

    // Dialog to offer clearing video cache when switching away from videos
    DecenzaDialog {
        id: clearCacheDialog
        modal: true
        anchors.centerIn: parent
        width: Theme.scaled(400)
        padding: 0

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            border.width: 1
            border.color: Theme.borderColor
        }

        contentItem: ColumnLayout {
            spacing: 0

            // Header
            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.scaled(50)

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: Theme.scaled(20)
                    anchors.verticalCenter: parent.verticalCenter
                    text: TranslationManager.translate("settings.screensaver.clearCacheTitle", "Clear Video Cache?")
                    font: Theme.titleFont
                    color: Theme.textColor
                }

                Rectangle {
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    height: 1
                    color: Theme.borderColor
                }
            }

            // Content
            ColumnLayout {
                Layout.fillWidth: true
                Layout.margins: Theme.scaled(20)
                spacing: Theme.scaled(15)

                Text {
                    Layout.fillWidth: true
                    text: TranslationManager.translate("settings.screensaver.clearCacheMessage",
                        "You have %1 MB of cached videos. Would you like to delete them to free up space?").arg(
                        (ScreensaverService.state.cacheUsedBytes / 1024 / 1024).toFixed(0))
                    color: Theme.textColor
                    font: Theme.bodyFont
                    wrapMode: Text.WordWrap
                }

                Text {
                    Layout.fillWidth: true
                    text: TranslationManager.translate("settings.screensaver.clearCacheWarning",
                        "Note: If you switch back to video screensaver later, videos will re-download slowly (one every 3 minutes) to conserve bandwidth. Images will download normally.")
                    color: Theme.warningColor
                    font.pixelSize: Theme.scaled(12)
                    wrapMode: Text.WordWrap
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(10)

                    Item { Layout.fillWidth: true }

                    AccessibleButton {
                        text: TranslationManager.translate("settings.screensaver.keepCache", "Keep Videos")
                        accessibleName: TranslationManager.translate("screensaver.keepCachedVideos", "Keep cached videos")
                        onClicked: {
                            // Apply type change without clearing cache
                            screensaverTab.writeScreenPreference("screensaverType", screensaverTab.pendingScreensaverType)
                            clearCacheDialog.close()
                        }
                    }

                    AccessibleButton {
                        text: TranslationManager.translate("settings.screensaver.clearCache", "Delete Videos")
                        accessibleName: TranslationManager.translate("screensaver.deleteCachedVideos", "Delete cached videos")
                        destructive: true
                        onClicked: {
                            // Clear cache with rate limiting, then apply type change
                            screensaverTab.requestServiceOperation("clear-cache")
                            screensaverTab.writeScreenPreference("screensaverType", screensaverTab.pendingScreensaverType)
                            clearCacheDialog.close()
                        }
                    }
                }
            }
        }
    }

    // Dialog to confirm clearing personal media
    DecenzaDialog {
        id: clearPersonalMediaDialog
        modal: true
        anchors.centerIn: parent
        width: Theme.scaled(400)
        padding: 0

        property bool isDeleting: false
        property int itemCount: ScreensaverService.state.personalMediaCount

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            border.width: 1
            border.color: Theme.borderColor
        }

        contentItem: ColumnLayout {
            spacing: 0

            // Header
            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.scaled(50)

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: Theme.scaled(20)
                    anchors.verticalCenter: parent.verticalCenter
                    text: TranslationManager.translate("settings.screensaver.clearPersonalTitle", "Clear Personal Media?")
                    font: Theme.titleFont
                    color: Theme.textColor
                }

                Rectangle {
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    height: 1
                    color: Theme.borderColor
                }
            }

            // Content
            ColumnLayout {
                Layout.fillWidth: true
                Layout.margins: Theme.scaled(20)
                spacing: Theme.scaled(15)

                Text {
                    Layout.fillWidth: true
                    visible: !clearPersonalMediaDialog.isDeleting
                    text: TranslationManager.translate("settings.screensaver.clearPersonalMessage",
                        "This will delete all %1 personal media files. This action cannot be undone.").arg(clearPersonalMediaDialog.itemCount)
                    color: Theme.textColor
                    font: Theme.bodyFont
                    wrapMode: Text.WordWrap
                }

                // Deleting state
                ColumnLayout {
                    Layout.fillWidth: true
                    visible: clearPersonalMediaDialog.isDeleting
                    spacing: Theme.scaled(10)

                    Text {
                        Layout.fillWidth: true
                        text: TranslationManager.translate("settings.screensaver.deletingPersonal", "Deleting personal media...")
                        color: Theme.textColor
                        font: Theme.bodyFont
                        horizontalAlignment: Text.AlignHCenter
                    }

                    // Progress bar (indeterminate)
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: Theme.scaled(6)
                        radius: Theme.scaled(3)
                        color: Qt.darker(Theme.surfaceColor, 1.3)

                        Rectangle {
                            id: progressIndicator
                            width: parent.width * 0.3
                            height: parent.height
                            radius: Theme.scaled(3)
                            color: Theme.primaryColor

                            SequentialAnimation on x {
                                loops: Animation.Infinite
                                running: clearPersonalMediaDialog.isDeleting
                                NumberAnimation {
                                    from: 0
                                    to: progressIndicator.parent.width - progressIndicator.width
                                    duration: 800
                                    easing.type: Easing.InOutQuad
                                }
                                NumberAnimation {
                                    from: progressIndicator.parent.width - progressIndicator.width
                                    to: 0
                                    duration: 800
                                    easing.type: Easing.InOutQuad
                                }
                            }
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(10)
                    visible: !clearPersonalMediaDialog.isDeleting

                    Item { Layout.fillWidth: true }

                    AccessibleButton {
                        text: TranslationManager.translate("common.cancel", "Cancel")
                        accessibleName: TranslationManager.translate("screensaver.cancel", "Cancel")
                        onClicked: clearPersonalMediaDialog.close()
                    }

                    AccessibleButton {
                        text: TranslationManager.translate("settings.screensaver.deleteAll", "Delete All")
                        accessibleName: TranslationManager.translate("screensaver.deleteAllPersonalMedia", "Delete all personal media")
                        destructive: true
                        onClicked: {
                            clearPersonalMediaDialog.isDeleting = true
                            // Use a timer to allow the UI to update before blocking deletion
                            deleteTimer.start()
                        }
                    }
                }
            }
        }

        Timer {
            id: deleteTimer
            interval: 100  // Brief delay to show progress animation
            onTriggered: {
                screensaverTab.requestServiceOperation("clear-personal")
                clearPersonalMediaDialog.isDeleting = false
                clearPersonalMediaDialog.close()
            }
        }

        onClosed: {
            isDeleting = false
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: Theme.scaled(15)

        // Column 1: Auto-Wake + Screen Timing (always visible)
        //
        // fillWidth with a maximum, NOT a bare preferredWidth: `Layout.fillWidth: false` makes a
        // child FIXED — the layout will not shrink it either. With both fixed columns holding
        // their full width, a window narrower than their sum starves column 3 (the only fill
        // child) to zero width, and their own contents paint outside them.
        Item {
            // 340, not the old 280: the Wake row (label + switch + two time steppers) needs
            // roughly 300 scaled units, so 280 never actually fitted it — which is what made
            // the row overrun the card in the first place.
            Layout.preferredWidth: Theme.scaled(340)
            Layout.maximumWidth: Theme.scaled(340)
            // Never below what the Wake row itself needs (~sc(292) plus the card's sc(10)
            // margins). The Flickable below only scrolls VERTICALLY, so a width under that
            // leaves the minute stepper clipped by two clip:true layers and unreachable —
            // strictly worse than the overrun this change replaced, which was at least visible
            // and tappable.
            Layout.minimumWidth: Theme.scaled(315)
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true

            // Scrollable rather than sized to fit: this column's height is content-driven
            // (three cards, the last of which grows with the Auto-Wake rows), and a longer
            // translation or a larger accessibility font size pushes it past the page. Fitting
            // it for today's strings would only move the problem. Matches the Flickable pattern
            // in SettingsMachineTab.
            Flickable {
                anchors.fill: parent
                contentHeight: column1Layout.implicitHeight
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                flickableDirection: Flickable.VerticalFlick
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            ColumnLayout {
                id: column1Layout
                width: parent.width
                spacing: Theme.scaled(10)

            // Screen card (Sleep only)
            SettingsCard {
                searchId: "autoSleep"
                title: TranslationManager.translate("settings.screensaver.screen", "Screen")
                description: TranslationManager.translate("settings.preferences.autoSleepDesc", "Put the machine to sleep after inactivity")
                keywords: ["sleep", "timeout", "power", "idle", "standby"]
                contentMargins: Theme.scaled(10)

                // Sleep after
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(10)

                    Tr {
                        key: "settings.screensaver.sleepAfter"
                        fallback: "Sleep after"
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(14)
                    }

                    Item { Layout.fillWidth: true }

                    ValueInput {
                        Layout.preferredWidth: Theme.scaled(120)
                        value: screensaverTab.screenPreference("autoSleepMinutes", 60)
                        from: 0
                        to: 240
                        stepSize: 5
                        decimals: 0
                        displayText: value === 0 ? TranslationManager.translate("settings.preferences.never", "Never") :
                                                   (value + " " + TranslationManager.translate("settings.preferences.min", "min"))
                        accessibleName: TranslationManager.translate("settings.preferences.autoSleep", "Auto-Sleep")
                        onValueModified: function(newValue) { screensaverTab.writeScreenPreference("autoSleepMinutes", newValue) }
                    }
                }
            }

            // Screensaver card (Dim settings, hidden when screensaver disabled)
            SettingsCard {
                searchId: "screensaverDim"
                title: TranslationManager.translate("settings.screensaver.screensaver", "Screensaver")
                description: TranslationManager.translate("settings.search.screensaverDimDesc", "Dim the screen after inactivity")
                keywords: ["dim", "brightness", "screensaver", "display", "darkness"]
                shown: screensaverTab.screenPreference("screensaverType", "videos") !== "disabled"
                contentMargins: Theme.scaled(10)

                // Dim after
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(10)

                    Tr {
                        key: "settings.screensaver.dimAfter"
                        fallback: "Dim after"
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(14)
                    }

                    Item { Layout.fillWidth: true }

                    ValueInput {
                        Layout.preferredWidth: Theme.scaled(120)
                        value: screensaverTab.screenPreference("dimDelayMinutes", 15)
                        from: 0
                        to: 45
                        stepSize: 5
                        decimals: 0
                        displayText: value === 0 ? TranslationManager.translate("settings.screensaver.immediately", "Immediately") : value + " " + TranslationManager.translate("settings.preferences.min", "min")
                        accessibleName: TranslationManager.translate("settings.screensaver.dimAfterAccessible", "Dim screen after delay in minutes")
                        onValueModified: function(newValue) { screensaverTab.writeScreenPreference("dimDelayMinutes", newValue) }
                    }
                }

                // Dim amount
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(10)

                    Tr {
                        key: "settings.screensaver.dimAmount"
                        fallback: "Dim amount"
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(14)
                    }

                    Item { Layout.fillWidth: true }

                    ValueInput {
                        Layout.preferredWidth: Theme.scaled(120)
                        value: screensaverTab.screenPreference("dimPercent", 0)
                        from: 0
                        to: 100
                        stepSize: 5
                        decimals: 0
                        displayText: value === 0 ? TranslationManager.translate("settings.screensaver.off", "Off") : value + "%"
                        accessibleName: TranslationManager.translate("settings.screensaver.dimAmountAccessible", "Screen dim amount percentage")
                        onValueModified: function(newValue) { screensaverTab.writeScreenPreference("dimPercent", newValue) }
                    }
                }
            }

            // Auto-Wake Timer card
            SettingsCard {
                id: autoWakeCard
                searchId: "autoWake"
                title: TranslationManager.translate("settings.options.autoWake", "Auto-Wake")
                description: TranslationManager.translate("settings.search.autoWakeDesc", "Schedule automatic wake-up times")
                keywords: ["wake", "schedule", "alarm", "morning", "timer", "power"]
                contentMargins: Theme.scaled(10)
                property int selectedDay: 0
                property var schedule: screensaverTab.screenPreference("autoWakeSchedule", [])
                property var selectedDayData: schedule[selectedDay] || {enabled: false, hour: 7, minute: 0}

                // Day buttons
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(3)

                    Repeater {
                        SettingsSearch.title: TranslationManager.translate("settings.search.autoWakeDays", "Wake days")
                        model: ["M", "T", "W", "T", "F", "S", "S"]

                        Rectangle {
                            id: dayButton
                            required property string modelData
                            required property int index

                            Layout.fillWidth: true
                            Layout.preferredHeight: Theme.scaled(28)
                            radius: Theme.scaled(5)

                            property bool isSelected: autoWakeCard.selectedDay === dayButton.index
                            property bool isEnabled: {
                                var sched = autoWakeCard.schedule
                                return sched[dayButton.index] ? sched[dayButton.index].enabled : false
                            }

                            color: isSelected ? Qt.lighter(Theme.primaryColor, 1.3) :
                                   isEnabled ? Theme.primaryColor :
                                   Theme.insetBackgroundColor
                            border.color: isSelected ? Theme.primaryContrastColor :
                                          isEnabled ? Theme.primaryColor : Theme.borderColor
                            border.width: isSelected ? 2 : 1

                            Accessible.role: Accessible.Button
                            Accessible.name: {
                                var dayNames = [
                                TranslationManager.translate("common.day.monday", "Monday"),
                                TranslationManager.translate("common.day.tuesday", "Tuesday"),
                                TranslationManager.translate("common.day.wednesday", "Wednesday"),
                                TranslationManager.translate("common.day.thursday", "Thursday"),
                                TranslationManager.translate("common.day.friday", "Friday"),
                                TranslationManager.translate("common.day.saturday", "Saturday"),
                                TranslationManager.translate("common.day.sunday", "Sunday")
                            ]
                                return dayNames[dayButton.index] +
                                       (isEnabled ? ", " + TranslationManager.translate("accessibility.enabled", "enabled") : "") +
                                       (isSelected ? ", " + TranslationManager.translate("accessibility.selected", "selected") : "")
                            }
                            Accessible.focusable: true
                            Accessible.onPressAction: dayArea.clicked(null)

                            Text {
                                anchors.centerIn: parent
                                text: dayButton.modelData
                                color: parent.isSelected || parent.isEnabled ? Theme.primaryContrastColor : Theme.textSecondaryColor
                                font.pixelSize: Theme.scaled(12)
                                font.bold: parent.isSelected || parent.isEnabled
                                Accessible.ignored: true
                            }

                            MouseArea {
                                id: dayArea
                                anchors.fill: parent
                                onClicked: autoWakeCard.selectedDay = dayButton.index
                            }
                        }
                    }
                }

                // Wake toggle + time on one compact row
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(6)

                    Text {
                        text: TranslationManager.translate("settings.preferences.wake", "Wake")
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(14)
                    }

                    StyledSwitch {
                        checked: autoWakeCard.selectedDayData.enabled || false
                        accessibleName: TranslationManager.translate("settings.preferences.wakeEnabledForDay", "Wake enabled for selected day")
                        onToggled: screensaverTab.updateAutoWakeDay(autoWakeCard.selectedDay, { enabled: checked })
                    }

                    Item { Layout.fillWidth: true }

                    ValueInput {
                        // Natural width, no cap. A cap of sc(80) alongside
                        // `minimumWidth: implicitWidth` was dead: ValueInput's
                        // implicitWidth is sc(56) + text metrics + sc(16) ≈ sc(91), so
                        // the minimum EXCEEDED the maximum and Qt resolves that in
                        // favour of the minimum (qgridlayoutengine.cpp:87,
                        // q_maximumSize = qMax(q_minimumSize, maxMax)) — the steppers
                        // ended up wider than the sc(80) they had before, which is the
                        // opposite of what the cap was for.
                        Layout.preferredWidth: implicitWidth
                        Layout.preferredHeight: Theme.scaled(34)
                        from: 0
                        to: 23
                        stepSize: 1
                        decimals: 0
                        value: autoWakeCard.selectedDayData.hour ?? 7
                        enabled: autoWakeCard.selectedDayData.enabled ?? false
                        valueColor: enabled ? Theme.primaryColor : Theme.textSecondaryColor
                        displayText: value < 10 ? "0" + value.toFixed(0) : value.toFixed(0)
                        accessibleName: TranslationManager.translate("settings.options.wakeHour", "Wake hour")
                        onValueModified: function(newValue) {
                            screensaverTab.updateAutoWakeDay(autoWakeCard.selectedDay, { hour: newValue })
                        }
                    }

                    Text {
                        text: ":"
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(16)
                        font.bold: true
                    }

                    ValueInput {
                        // Natural width, no cap. A cap of sc(80) alongside
                        // `minimumWidth: implicitWidth` was dead: ValueInput's
                        // implicitWidth is sc(56) + text metrics + sc(16) ≈ sc(91), so
                        // the minimum EXCEEDED the maximum and Qt resolves that in
                        // favour of the minimum (qgridlayoutengine.cpp:87,
                        // q_maximumSize = qMax(q_minimumSize, maxMax)) — the steppers
                        // ended up wider than the sc(80) they had before, which is the
                        // opposite of what the cap was for.
                        Layout.preferredWidth: implicitWidth
                        Layout.preferredHeight: Theme.scaled(34)
                        from: 0
                        to: 59
                        stepSize: 5
                        decimals: 0
                        value: autoWakeCard.selectedDayData.minute ?? 0
                        enabled: autoWakeCard.selectedDayData.enabled ?? false
                        valueColor: enabled ? Theme.primaryColor : Theme.textSecondaryColor
                        displayText: value < 10 ? "0" + value.toFixed(0) : value.toFixed(0)
                        accessibleName: TranslationManager.translate("settings.options.wakeMinute", "Wake minute")
                        onValueModified: function(newValue) {
                            screensaverTab.updateAutoWakeDay(autoWakeCard.selectedDay, { minute: newValue })
                        }
                    }
                }

                // Stay awake toggle + duration on one compact row
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(6)

                    Text {
                        text: TranslationManager.translate("settings.preferences.stayAwakeFor", "Stay awake")
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(14)
                    }

                    StyledSwitch {
                        id: stayAwakeSwitch
                        checked: screensaverTab.screenPreference("autoWakeStayAwakeEnabled", false)
                        accessibleName: TranslationManager.translate("settings.preferences.stayAwakeAfterWake", "Stay awake after auto-wake")
                        onToggled: screensaverTab.writeScreenPreference("autoWakeStayAwakeEnabled", checked)
                    }

                    Item { Layout.fillWidth: true }

                    ValueInput {
                        visible: screensaverTab.screenPreference("autoWakeStayAwakeEnabled", false)
                        Layout.preferredWidth: Theme.scaled(80)
                        Layout.preferredHeight: Theme.scaled(34)
                        from: 15
                        to: 720
                        stepSize: 15
                        decimals: 0
                        value: screensaverTab.screenPreference("autoWakeStayAwakeMinutes", 60)
                        valueColor: Theme.primaryColor
                        displayText: {
                            var mins = value
                            if (mins >= 60) {
                                let hours = Math.floor(mins / 60)
                                let rem = mins % 60
                                if (rem === 0) return hours + TranslationManager.translate("common.unit.h", "h")
                                return hours + TranslationManager.translate("common.unit.h", "h") + " " + rem + TranslationManager.translate("common.unit.m", "m")
                            }
                            return mins + " " + TranslationManager.translate("common.unit.min", "min")
                        }
                        accessibleName: TranslationManager.translate("settings.options.stayAwakeDuration", "Stay awake duration")
                        onValueModified: function(newValue) { screensaverTab.writeScreenPreference("autoWakeStayAwakeMinutes", newValue) }
                    }
                }
        }

            } // ColumnLayout
            } // Flickable
        }

        // Column 2: Video Category (videos mode only, full height)
        Item {
            // See column 1 — fixed width is what let these two overrun the page.
            Layout.preferredWidth: Theme.scaled(260)
            Layout.maximumWidth: Theme.scaled(260)
            Layout.minimumWidth: Theme.scaled(150)
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            visible: screensaverTab.screenPreference("screensaverType", "videos") === "videos"

            SettingsCard {
                anchors.fill: parent
                searchId: "videoCategory"
                title: TranslationManager.translate("settings.screensaver.videoCategory", "Video Category")
                description: TranslationManager.translate("settings.search.videoCategoryDesc", "Choose which videos the screensaver plays")
                keywords: ["video", "category", "screensaver", "pexels", "clips"]
                fillContent: true
                spacing: Theme.scaled(10)

                // Category list
                // Use local model copy to avoid delegate crash during rapid updates
                ListView {
                    id: categoryList
                    SettingsSearch.title: TranslationManager.translate("settings.screensaver.videoCategory", "Video Category")
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: categoryModelCopy
                    spacing: Theme.scaled(2)
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                    property var categoryModelCopy: []

                    Connections {
                        target: ScreensaverService.state
                        function onCategoriesChanged() {
                            // Defer one event loop tick so any in-progress delegate
                            // layout completes before the model is replaced.
                            Qt.callLater(function() {
                                categoryList.categoryModelCopy = ScreensaverService.state.categories
                            })
                        }
                    }

                    Component.onCompleted: {
                        categoryModelCopy = ScreensaverService.state.categories
                    }

                    delegate: ItemDelegate {
                        id: delegate
                        required property var modelData

                        width: ListView.view ? ListView.view.width : 0
                        height: Theme.scaled(36)
                        highlighted: delegate.modelData && delegate.modelData.id === screensaverTab.screenPreference("selectedCategoryId", "")

                        background: Rectangle {
                            color: delegate.highlighted ? Theme.primaryColor :
                                   delegate.hovered ? Qt.darker(Theme.insetBackgroundColor, 1.2) : Theme.insetBackgroundColor
                            radius: Theme.scaled(6)
                        }

                        contentItem: Text {
                            text: delegate.modelData ? delegate.modelData.name : ""
                            color: delegate.highlighted ? Theme.primaryContrastColor : Theme.textColor
                            font.pixelSize: Theme.scaled(14)
                            font.bold: delegate.highlighted
                            verticalAlignment: Text.AlignVCenter
                            leftPadding: Theme.scaled(10)
                        }

                        // contentItem is set rather than `text`, so Qt derives no
                        // default accessible name from this row.
                        Accessible.role: Accessible.Button
                        Accessible.name: delegate.modelData ? delegate.modelData.name : ""
                        Accessible.focusable: true
                        Accessible.checkable: true
                        Accessible.checked: delegate.highlighted
                        Accessible.onPressAction: delegate.clicked()

                        onClicked: {
                            if (delegate.modelData) {
                                screensaverTab.writeScreenPreference("selectedCategoryId", delegate.modelData.id)
                            }
                        }
                    }

                    // `parent` here is the ListView's contentItem
                    // (qquickflickable.cpp:2462), not the ListView -- `parent.count` was
                    // undefined, so neither of these placeholders has ever appeared.
                    Tr {
                        anchors.horizontalCenter: parent.horizontalCenter
                        y: (categoryList.height - height) / 2
                        key: "settings.screensaver.loading"
                        fallback: "Loading..."
                        visible: categoryList.count === 0 && ScreensaverService.state.isFetchingCategories
                        color: Theme.textSecondaryColor
                    }

                    Tr {
                        anchors.horizontalCenter: parent.horizontalCenter
                        y: (categoryList.height - height) / 2
                        key: "settings.screensaver.noCategories"
                        fallback: "No categories"
                        visible: categoryList.count === 0 && !ScreensaverService.state.isFetchingCategories
                        color: Theme.textSecondaryColor
                    }
                }

                AccessibleButton {
                    text: TranslationManager.translate("settings.screensaver.refreshCategories", "Refresh Categories")
                    accessibleName: TranslationManager.translate("screensaver.refreshCategories", "Refresh screensaver categories")
                    Layout.fillWidth: true
                    enabled: !ScreensaverService.state.isFetchingCategories
                    onClicked: screensaverTab.requestServiceOperation("refresh-categories")
                }
            }
        }

        // Screensaver settings
        SettingsCard {
            searchId: "screensaver"
            title: TranslationManager.translate("settings.screensaver.settings", "Screensaver Settings")
            description: TranslationManager.translate("settings.search.screensaverDesc", "Choose screensaver type and settings")
            keywords: ["screensaver", "attractor", "pipes", "clock", "video", "image", "dim"]
            showHeader: false
            fillContent: true
            spacing: Theme.scaled(15)

            // Overlay chip group — the readouts/link shown on top of whichever
            // background is active. Clock reads/writes the per-type boolean
            // below (kept as-is); Water Level/Shot Plan/Battery/Link Button are
            // each a single global setting shared across every background.
            // Placed above Display so all the screensaver-overlay controls read
            // together before the background-specific settings below.
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingSmall
                visible: screensaverTab.screenPreference("screensaverType", "videos") !== "disabled"

                Tr {
                    key: "settings.screensaver.overlayInfo"
                    fallback: "Info"
                    color: Theme.textColor
                    font.pixelSize: Theme.scaled(14)
                }

                Flow {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(8)

                    Repeater {
                        SettingsSearch.title: TranslationManager.translate("settings.search.screensaverInfo", "Screensaver info overlay")
                        model: ScreensaverService.overlayChipsForType(screensaverTab.screenPreference("screensaverType", "videos"))

                        Rectangle {
                            id: overlayChip
                            required property string modelData

                            function chipValue() {
                                switch (overlayChip.modelData) {
                                case "clock":
                                    switch (screensaverTab.screenPreference("screensaverType", "videos")) {
                                    case "videos": return screensaverTab.screenPreference("videosShowClock", true)
                                    case "pipes": return screensaverTab.screenPreference("pipesShowClock", true)
                                    case "attractor": return screensaverTab.screenPreference("attractorShowClock", false)
                                    case "shotmap": return screensaverTab.screenPreference("shotMapShowClock", true)
                                    }
                                    return false
                                case "waterLevel": return screensaverTab.screenPreference("overlayShowWaterLevel", false)
                                case "shotPlan": return screensaverTab.screenPreference("overlayShowShotPlan", false)
                                case "battery": return screensaverTab.screenPreference("overlayShowBattery", false)
                                case "linkButton": return screensaverTab.screenPreference("overlayLinkButtonEnabled", false)
                                }
                                return false
                            }

                            function toggleChip() {
                                switch (overlayChip.modelData) {
                                case "clock":
                                    switch (screensaverTab.screenPreference("screensaverType", "videos")) {
                                    case "videos": screensaverTab.writeScreenPreference("videosShowClock", !screensaverTab.screenPreference("videosShowClock", true)); break
                                    case "pipes": screensaverTab.writeScreenPreference("pipesShowClock", !screensaverTab.screenPreference("pipesShowClock", true)); break
                                    case "attractor": screensaverTab.writeScreenPreference("attractorShowClock", !screensaverTab.screenPreference("attractorShowClock", false)); break
                                    case "shotmap": screensaverTab.writeScreenPreference("shotMapShowClock", !screensaverTab.screenPreference("shotMapShowClock", true)); break
                                    }
                                    break
                                case "waterLevel": screensaverTab.writeScreenPreference("overlayShowWaterLevel", !screensaverTab.screenPreference("overlayShowWaterLevel", false)); break
                                case "shotPlan": screensaverTab.writeScreenPreference("overlayShowShotPlan", !screensaverTab.screenPreference("overlayShowShotPlan", false)); break
                                case "battery": screensaverTab.writeScreenPreference("overlayShowBattery", !screensaverTab.screenPreference("overlayShowBattery", false)); break
                                case "linkButton": screensaverTab.writeScreenPreference("overlayLinkButtonEnabled", !screensaverTab.screenPreference("overlayLinkButtonEnabled", false)); break
                                }
                            }

                            readonly property bool isOn: chipValue()
                            readonly property string chipLabel: {
                                var _ = TranslationManager.translationVersion
                                switch (overlayChip.modelData) {
                                case "clock": return TranslationManager.translate("layoutEditor.chipTime", "Time")
                                case "waterLevel": return TranslationManager.translate("layoutEditor.chipWater", "Water")
                                case "shotPlan": return TranslationManager.translate("layoutEditor.chipShotPlan", "Shot Plan")
                                case "battery": return TranslationManager.translate("layoutEditor.chipBattery", "Battery")
                                case "linkButton": return TranslationManager.translate("settings.screensaver.linkButton", "Link Button")
                                }
                                return overlayChip.modelData
                            }

                            width: chipText.implicitWidth + Theme.scaled(24)
                            height: Theme.scaled(32)
                            radius: Theme.scaled(16)
                            color: isOn ? Theme.primaryColor : "transparent"
                            border.color: isOn ? Theme.primaryColor : Theme.borderColor
                            border.width: 1

                            Text {
                                id: chipText
                                anchors.centerIn: parent
                                text: overlayChip.chipLabel
                                color: overlayChip.isOn ? Theme.primaryContrastColor : Theme.textColor
                                font.pixelSize: Theme.scaled(13)
                                Accessible.ignored: true
                            }

                            AccessibleMouseArea {
                                anchors.fill: parent
                                accessibleName: overlayChip.chipLabel
                                accessibleRole: Accessible.CheckBox
                                accessibleChecked: overlayChip.isOn
                                onAccessibleClicked: overlayChip.toggleChip()
                            }
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(10)
                    visible: screensaverTab.screenPreference("overlayLinkButtonEnabled", false)

                    StyledTextField {
                        Layout.preferredWidth: Theme.scaled(160)
                        placeholderText: TranslationManager.translate("settings.screensaver.linkButtonLabelPlaceholder", "Button label")
                        text: screensaverTab.screenPreference("overlayLinkButtonLabel", "")
                        accessibleName: TranslationManager.translate("settings.screensaver.linkButtonLabel", "Link button label")
                        onEditingFinished: screensaverTab.writeScreenPreference("overlayLinkButtonLabel", text)
                    }

                    StyledTextField {
                        Layout.fillWidth: true
                        placeholderText: TranslationManager.translate("settings.screensaver.linkButtonUrlPlaceholder", "https://…")
                        text: screensaverTab.screenPreference("overlayLinkButtonUrl", "")
                        accessibleName: TranslationManager.translate("settings.screensaver.linkButtonUrl", "Link button URL")
                        onEditingFinished: screensaverTab.writeScreenPreference("overlayLinkButtonUrl", text)
                    }
                }
            }

            Tr {
                key: "settings.screensaver.display"
                fallback: "Display"
                color: Theme.textColor
                font.pixelSize: Theme.scaled(16)
                font.bold: true
            }

            // Screensaver type selector
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.scaled(15)

                Tr {
                    key: "settings.screensaver.type"
                    fallback: "Type"
                    color: Theme.textColor
                    font.pixelSize: Theme.scaled(14)
                }

                StyledComboBox {
                    id: typeComboBox
                    Layout.preferredWidth: Theme.scaled(220)
                    accessibleLabel: TranslationManager.translate("settings.screensaver.type.accessible", "Screensaver type")
                    model: [
                        TranslationManager.translate("settings.screensaver.type.disabled", "Turn Screen Off"),
                        TranslationManager.translate("settings.screensaver.type.videos", "Videos & Images"),
                        TranslationManager.translate("settings.screensaver.type.pipes", "3D Pipes"),
                        TranslationManager.translate("settings.screensaver.type.flipclock", "Flip Clock"),
                        TranslationManager.translate("settings.screensaver.type.attractor", "Strange Attractors"),
                        TranslationManager.translate("settings.screensaver.type.shotmap", "Shot Map")
                    ]
                    readonly property var typeValues: ["disabled", "videos", "pipes", "flipclock", "attractor", "shotmap"]
                    // Shows the pending pick only while the clear-cache dialog is up, so
                    // every way of closing it (either button, Escape, tap outside) falls
                    // back to the type actually in force.
                    currentIndex: Math.max(0, typeValues.indexOf(clearCacheDialog.visible
                        ? screensaverTab.pendingScreensaverType : screensaverTab.screenPreference("screensaverType", "videos")))
                    onActivated: {
                        var newType = typeValues[currentIndex]

                        // If switching away from videos and we have cached videos, offer to clear
                        if (screensaverTab.screenPreference("screensaverType", "videos") === "videos" &&
                            newType !== "videos" &&
                            ScreensaverService.state.cacheUsedBytes > 0) {
                            screensaverTab.pendingScreensaverType = newType
                            clearCacheDialog.open()
                        } else {
                            screensaverTab.writeScreenPreference("screensaverType", newType)
                        }
                    }
                }

                Item { Layout.fillWidth: true }
            }

            // Pipes settings (pipes mode only)
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.scaled(30)
                visible: screensaverTab.screenPreference("screensaverType", "videos") === "pipes"

                RowLayout {
                    spacing: Theme.scaled(10)

                    Tr {
                        key: "settings.screensaver.pipesSpeed"
                        fallback: "Speed"
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(14)
                    }

                    ValueInput {
                        id: pipesSpeedInput
                        value: screensaverTab.screenPreference("pipesSpeed", 0.5)
                        suffix: "x"
                        from: 0.1
                        to: 2.0
                        stepSize: 0.1
                        decimals: 1
                        accessibleName: TranslationManager.translate("settings.screensaver.pipesSpeedAccessible", "Pipes animation speed")
                        onValueModified: function(newValue) { screensaverTab.writeScreenPreference("pipesSpeed", newValue) }
                    }
                }

                RowLayout {
                    spacing: Theme.scaled(10)

                    Tr {
                        key: "settings.screensaver.cameraSpeed"
                        fallback: "Camera rotation"
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(14)
                    }

                    ValueInput {
                        id: cameraSpeedInput
                        value: screensaverTab.screenPreference("pipesCameraSpeed", 60)
                        suffix: "s"
                        from: 10
                        to: 300
                        stepSize: 1
                        decimals: 0
                        accessibleName: TranslationManager.translate("settings.screensaver.cameraSpeedAccessible", "Camera rotation speed")
                        onValueModified: function(newValue) { screensaverTab.writeScreenPreference("pipesCameraSpeed", newValue) }
                    }
                }

                Item { Layout.fillWidth: true }
            }

            // Flip Clock settings (flipclock mode only)
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.scaled(30)
                visible: screensaverTab.screenPreference("screensaverType", "videos") === "flipclock"

                RowLayout {
                    spacing: Theme.scaled(10)

                    Tr {
                        key: "settings.screensaver.flipclock3D"
                        fallback: "3D perspective"
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(14)
                    }

                    StyledSwitch {
                        checked: screensaverTab.screenPreference("flipClockUse3D", true)
                        accessibleName: TranslationManager.translate("settings.screensaver.flipclock3D", "3D perspective")
                        onToggled: screensaverTab.writeScreenPreference("flipClockUse3D", checked)
                    }
                }

                Item { Layout.fillWidth: true }
            }

            // Strange Attractor has no type-specific settings beyond the
            // shared overlay chip group above.

            // Shot Map settings (shotmap mode only)
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.scaled(30)
                visible: screensaverTab.screenPreference("screensaverType", "videos") === "shotmap"

                RowLayout {
                    spacing: Theme.scaled(8)
                    visible: Settings.app.hasQuick3D  // Globe requires Quick3D; hide selector without it

                    Tr {
                        key: "settings.screensaver.shotmap.shape"
                        fallback: "Shape"
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(14)
                    }

                    StyledComboBox {
                        id: shotMapShapeCombo
                        Layout.preferredWidth: Theme.scaled(120)
                        accessibleLabel: TranslationManager.translate("settings.screensaver.shotmap.shape", "Shape")
                        model: [
                            TranslationManager.translate("settings.screensaver.shotmap.flat", "Flat"),
                            TranslationManager.translate("settings.screensaver.shotmap.globe", "Globe")
                        ]
                        currentIndex: screensaverTab.screenPreference("shotMapShape", "flat") === "globe" ? 1 : 0
                        onActivated: screensaverTab.writeScreenPreference("shotMapShape", ["flat", "globe"][currentIndex])
                    }
                }

                RowLayout {
                    spacing: Theme.scaled(8)

                    Tr {
                        key: "settings.screensaver.shotmap.texture"
                        fallback: "Map"
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(14)
                    }

                    StyledComboBox {
                        id: shotMapTextureCombo
                        Layout.preferredWidth: Theme.scaled(130)
                        accessibleLabel: TranslationManager.translate("settings.screensaver.shotmap.texture.accessible", "Texture")
                        model: [
                            TranslationManager.translate("settings.screensaver.shotmap.dark", "Dark"),
                            TranslationManager.translate("settings.screensaver.shotmap.bright", "Bright"),
                            TranslationManager.translate("settings.screensaver.shotmap.satellite", "Satellite")
                        ]
                        currentIndex: screensaverTab.screenPreference("shotMapTexture", "dark") === "bright" ? 1 :
                                      screensaverTab.screenPreference("shotMapTexture", "dark") === "satellite" ? 2 : 0
                        onActivated: screensaverTab.writeScreenPreference("shotMapTexture", ["dark", "bright", "satellite"][currentIndex])
                    }
                }

                Item { Layout.fillWidth: true }
            }

            // Shot Map toggles (shotmap mode only)
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.scaled(30)
                visible: screensaverTab.screenPreference("screensaverType", "videos") === "shotmap"

                RowLayout {
                    spacing: Theme.scaled(8)

                    Tr {
                        key: "settings.screensaver.shotmap.profiles"
                        fallback: "Profiles"
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(14)
                    }

                    StyledSwitch {
                        checked: screensaverTab.screenPreference("shotMapShowProfiles", true)
                        accessibleName: TranslationManager.translate("settings.screensaver.shotmap.profiles", "Profiles")
                        onToggled: screensaverTab.writeScreenPreference("shotMapShowProfiles", checked)
                    }
                }

                RowLayout {
                    spacing: Theme.scaled(8)

                    Tr {
                        key: "settings.screensaver.shotmap.terminator"
                        fallback: "Day/Night"
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(14)
                    }

                    StyledSwitch {
                        checked: screensaverTab.screenPreference("shotMapShowTerminator", true)
                        accessibleName: TranslationManager.translate("settings.screensaver.shotmap.terminator.accessible", "Day/Night terminator")
                        onToggled: screensaverTab.writeScreenPreference("shotMapShowTerminator", checked)
                    }
                }

                Item { Layout.fillWidth: true }
            }

            // Status row (only visible for videos mode)
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.scaled(20)
                visible: screensaverTab.screenPreference("screensaverType", "videos") === "videos"

                ColumnLayout {
                    spacing: Theme.scaled(4)

                    Tr {
                        key: "settings.screensaver.currentCategory"
                        fallback: "Current Category"
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.scaled(12)
                    }

                    Text {
                        text: ScreensaverService.state.selectedCategoryName
                        color: Theme.primaryColor
                        font.pixelSize: Theme.scaled(16)
                        font.bold: true
                    }
                }

                ColumnLayout {
                    spacing: Theme.scaled(4)

                    Tr {
                        key: "settings.screensaver.videos"
                        fallback: "Videos"
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.scaled(12)
                    }

                    Text {
                        text: ScreensaverService.state.itemCount + (ScreensaverService.state.isDownloading ? " (" + TranslationManager.translate("settings.screensaver.downloading", "downloading...") + ")" : "")
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(16)
                    }
                }

                ColumnLayout {
                    spacing: Theme.scaled(4)

                    Tr {
                        key: "settings.screensaver.cacheUsage"
                        fallback: "Cache Usage"
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.scaled(12)
                    }

                    Text {
                        text: (ScreensaverService.state.cacheUsedBytes / 1024 / 1024).toFixed(0) + " MB"
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(16)
                    }
                }

                // Rate limit indicator
                ColumnLayout {
                    spacing: Theme.scaled(4)
                    visible: ScreensaverService.state.isRateLimited

                    Text {
                        text: TranslationManager.translate("settings.screensaver.rateLimited", "Slow Download")
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.scaled(12)
                    }

                    Text {
                        text: ScreensaverService.state.rateLimitMinutesRemaining > 0
                            ? TranslationManager.translate("settings.screensaver.nextIn", "Next in %1 min").arg(ScreensaverService.state.rateLimitMinutesRemaining)
                            : TranslationManager.translate("settings.screensaver.ready", "Ready")
                        color: Theme.warningColor
                        font.pixelSize: Theme.scaled(16)
                    }
                }

                Item { Layout.fillWidth: true }
            }

            // Download progress (videos mode only)
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.scaled(6)
                radius: Theme.scaled(3)
                color: Qt.darker(Theme.insetBackgroundColor, 1.3)
                visible: screensaverTab.screenPreference("screensaverType", "videos") === "videos" && ScreensaverService.state.isDownloading

                Rectangle {
                    width: parent.width * ScreensaverService.state.downloadProgress
                    height: parent.height
                    radius: Theme.scaled(3)
                    color: Theme.primaryColor

                    Behavior on width { NumberAnimation { duration: 200 } }
                }
            }

            // Toggles row (videos mode only)
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.scaled(30)
                visible: screensaverTab.screenPreference("screensaverType", "videos") === "videos"

                // Cache toggle
                RowLayout {
                    spacing: Theme.scaled(10)

                    Tr {
                        key: "settings.screensaver.cacheVideos"
                        fallback: "Cache Videos"
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(14)
                    }

                    StyledSwitch {
                        checked: screensaverTab.screenPreference("cacheEnabled", true)
                        accessibleName: TranslationManager.translate("settings.screensaver.cacheVideos", "Cache Videos")
                        onToggled: screensaverTab.writeScreenPreference("cacheEnabled", checked)
                    }
                }

                // Show date toggle - only visible for Personal category
                RowLayout {
                    spacing: Theme.scaled(10)
                    visible: ScreensaverService.state.isPersonalCategory

                    Tr {
                        key: "settings.screensaver.showDate"
                        fallback: "Show Date"
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(14)
                    }

                    StyledSwitch {
                        checked: screensaverTab.screenPreference("showDateOnPersonal", false)
                        accessibleName: TranslationManager.translate("settings.screensaver.showDate", "Show Date")
                        onToggled: screensaverTab.writeScreenPreference("showDateOnPersonal", checked)
                    }
                }

                Item { Layout.fillWidth: true }
            }

            Item { Layout.fillHeight: true }

            // Action buttons (videos mode only)
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.scaled(10)
                visible: screensaverTab.screenPreference("screensaverType", "videos") === "videos"

                AccessibleButton {
                    visible: !ScreensaverService.state.isPersonalCategory
                    text: TranslationManager.translate("settings.screensaver.refreshVideos", "Refresh Videos")
                    accessibleName: TranslationManager.translate("screensaver.refreshVideos", "Refresh screensaver videos")
                    onClicked: screensaverTab.requestServiceOperation("refresh-catalog")
                    enabled: !ScreensaverService.state.isRefreshing
                }

                AccessibleButton {
                    visible: ScreensaverService.state.isPersonalCategory
                    text: TranslationManager.translate("settings.screensaver.clearPersonal", "Clear Personal Media")
                    accessibleName: TranslationManager.translate("screensaver.clearPersonalMedia", "Clear personal media")
                    onClicked: clearPersonalMediaDialog.open()
                }

                Item { Layout.fillWidth: true }
            }
        }
    }
}
