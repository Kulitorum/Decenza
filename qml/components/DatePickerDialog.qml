import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decenza
import "DateUtils.js" as DateUtils

Dialog {
    id: root
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Theme.scaled(320)
    modal: true
    padding: 0

    property date selectedDate: new Date()

    signal dateSelected(string dateString)

    function openWithDate(dateString) {
        // Defocus any focused text field before the dialog opens. Qt Dialog records
        // the active focus item when opening and restores it on close. If a text field
        // has focus here, closing the dialog would restore focus to it and re-show
        // the keyboard. Clearing focus first means there is nothing keyboard-triggering
        // to restore.
        var overlay = Overlay.overlay
        if (overlay) {
            var win = overlay.Window.window
            if (win && win.activeFocusItem) {
                win.activeFocusItem.focus = false
            }
        }
        Qt.inputMethod.hide()

        dateString = DateUtils.normalizeDateString(dateString || "")

        if (dateString && dateString.length === 10) {
            var parts = dateString.split("-")
            var d = new Date(parseInt(parts[0]), parseInt(parts[1]) - 1, parseInt(parts[2]))
            if (!isNaN(d.getTime())) {
                selectedDate = d
                monthGrid.month = d.getMonth()
                monthGrid.year = d.getFullYear()
                open()
                return
            }
        }
        // Default to today
        var today = new Date()
        selectedDate = today
        monthGrid.month = today.getMonth()
        monthGrid.year = today.getFullYear()
        open()
    }

    onOpened: {
        if (typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled) {
            AccessibilityManager.announce(
                TranslationManager.translate("datepicker.opened", "Date picker. Use arrows to change month.")
            )
        }
    }

    // When the dialog closes, Qt restores focus to whatever was focused before it opened
    // (e.g. the date text field the user clicked before tapping the calendar button).
    // Restoring focus to a TextField automatically shows the keyboard. Hide it here —
    // the user just used a picker and does not want to type.
    onClosed: Qt.callLater(function() { Qt.inputMethod.hide() })

    background: Rectangle {
        color: Theme.surfaceColor
        radius: Theme.cardRadius
        border.width: 1
        border.color: Theme.borderColor
    }

    contentItem: ColumnLayout {
        spacing: 0

        // Header with month/year navigation
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: Theme.scaled(12)
            spacing: Theme.scaled(8)

            AccessibleButton {
                Layout.preferredWidth: Theme.scaled(36)
                Layout.preferredHeight: Theme.scaled(36)
                text: "<"
                accessibleName: TranslationManager.translate("datepicker.previousMonth", "Previous month")
                leftPadding: Theme.scaled(4)
                rightPadding: Theme.scaled(4)
                onClicked: {
                    if (monthGrid.month === 0) {
                        monthGrid.month = 11
                        monthGrid.year--
                    } else {
                        monthGrid.month--
                    }
                }
            }

            Text {
                Layout.fillWidth: true
                text: new Date(monthGrid.year, monthGrid.month).toLocaleDateString(Qt.locale(), "MMMM yyyy")
                font: Theme.subtitleFont
                color: Theme.textColor
                horizontalAlignment: Text.AlignHCenter
                Accessible.ignored: true
            }

            AccessibleButton {
                Layout.preferredWidth: Theme.scaled(36)
                Layout.preferredHeight: Theme.scaled(36)
                text: ">"
                accessibleName: TranslationManager.translate("datepicker.nextMonth", "Next month")
                leftPadding: Theme.scaled(4)
                rightPadding: Theme.scaled(4)
                onClicked: {
                    if (monthGrid.month === 11) {
                        monthGrid.month = 0
                        monthGrid.year++
                    } else {
                        monthGrid.month++
                    }
                }
            }
        }

        // Day-of-week headers
        DayOfWeekRow {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.scaled(12)
            Layout.rightMargin: Theme.scaled(12)
            locale: Qt.locale()

            delegate: Text {
                required property var model
                text: model.shortName
                font: Theme.captionFont
                color: Theme.textSecondaryColor
                horizontalAlignment: Text.AlignHCenter
                Accessible.ignored: true
            }
        }

        // Calendar grid
        MonthGrid {
            id: monthGrid
            Layout.fillWidth: true
            Layout.leftMargin: Theme.scaled(12)
            Layout.rightMargin: Theme.scaled(12)
            Layout.bottomMargin: Theme.scaled(8)
            locale: Qt.locale()

            delegate: Rectangle {
                id: dayDelegate
                required property var model

                implicitWidth: Theme.scaled(36)
                implicitHeight: Theme.scaled(36)
                radius: Theme.scaled(18)

                property bool isSelected: model.day === root.selectedDate.getDate() &&
                                          model.month === root.selectedDate.getMonth() &&
                                          model.year === root.selectedDate.getFullYear()
                property bool isToday: {
                    var today = new Date()
                    return model.day === today.getDate() &&
                           model.month === today.getMonth() &&
                           model.year === today.getFullYear()
                }
                property bool isCurrentMonth: model.month === monthGrid.month

                color: isSelected ? Theme.primaryColor : "transparent"

                Text {
                    anchors.centerIn: parent
                    text: dayDelegate.model.day
                    font.pixelSize: Theme.scaled(14)
                    font.family: Theme.bodyFont.family
                    font.bold: dayDelegate.isToday
                    color: {
                        if (dayDelegate.isSelected) return Theme.primaryContrastColor
                        if (!dayDelegate.isCurrentMonth) return Theme.textSecondaryColor
                        if (dayDelegate.isToday) return Theme.primaryColor
                        return Theme.textColor
                    }
                    opacity: dayDelegate.isCurrentMonth ? 1.0 : 0.4
                    Accessible.ignored: true
                }

                // Today underline indicator
                Rectangle {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: Theme.scaled(4)
                    width: Theme.scaled(16)
                    height: Theme.scaled(2)
                    radius: 1
                    color: dayDelegate.isSelected ? Theme.primaryContrastColor : Theme.primaryColor
                    visible: dayDelegate.isToday
                }

                Accessible.role: Accessible.Button
                Accessible.name: new Date(dayDelegate.model.year, dayDelegate.model.month, dayDelegate.model.day).toLocaleDateString()
                Accessible.focusable: true
                Accessible.onPressAction: dayArea.clicked(null)

                MouseArea {
                    id: dayArea
                    anchors.fill: parent
                    onClicked: {
                        if (dayDelegate.isCurrentMonth) {
                            var d = new Date(dayDelegate.model.year, dayDelegate.model.month, dayDelegate.model.day)
                            root.selectedDate = d
                            var mm = String(d.getMonth() + 1).padStart(2, '0')
                            var dd = String(d.getDate()).padStart(2, '0')
                            root.dateSelected(d.getFullYear() + "-" + mm + "-" + dd)
                            root.close()
                        }
                    }
                }
            }
        }

        // Bottom buttons
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: Theme.scaled(12)
            Layout.topMargin: 0
            spacing: Theme.scaled(8)

            AccessibleButton {
                text: TranslationManager.translate("datepicker.today", "Today")
                accessibleName: TranslationManager.translate("datepicker.selectToday", "Select today's date")
                primary: true
                Layout.fillWidth: true
                onClicked: {
                    var today = new Date()
                    root.selectedDate = today
                    var mm = String(today.getMonth() + 1).padStart(2, '0')
                    var dd = String(today.getDate()).padStart(2, '0')
                    root.dateSelected(today.getFullYear() + "-" + mm + "-" + dd)
                    root.close()
                }
            }

            AccessibleButton {
                text: TranslationManager.translate("datepicker.clear", "Clear")
                accessibleName: TranslationManager.translate("datepicker.clearDate", "Clear date")
                Layout.fillWidth: true
                onClicked: {
                    root.dateSelected("")
                    root.close()
                }
            }
        }
    }
}
