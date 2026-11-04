pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decenza

// The host's memory monitor: RSS and QObject totals, their sampled trend and
// the largest QObject classes with their growth since startup.
DecenzaDialog {
    id: memoryDialog
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(Theme.scaled(900), parent ? parent.width - Theme.spacingLarge : Theme.scaled(900))
    height: Math.min(Theme.scaled(720), parent ? parent.height - Theme.spacingLarge : Theme.scaled(720))
    title: TranslationManager.translate("settings.debug.memory", "Memory usage")
    modal: true
    background: Rectangle {
        color: Theme.surfaceColor
        radius: Theme.cardRadius
        border.color: Theme.borderColor
        border.width: 1
    }
    header: Text {
        text: memoryDialog.title
        color: Theme.textColor
        font: Theme.subtitleFont
        padding: Theme.spacingMedium
        Accessible.role: Accessible.Heading
        Accessible.name: memoryDialog.title
    }

    property var snapshot: ({})
    property string pendingId: ""
    property string errorMessage: ""
    property string clipboardRequestId: ""
    readonly property bool busy: pendingId !== ""
    readonly property var samples: snapshot.samples || []
    readonly property var topClasses: snapshot.topClasses || []

    function refresh(): void {
        if (memoryDialog.busy) return
        memoryDialog.errorMessage = ""
        memoryDialog.pendingId = SystemLogService.execute("memorySnapshot", {})
    }

    function megabytes(section: var): string {
        return section && section.rssMB !== undefined ? section.rssMB.toFixed(1) + " MB" : "—"
    }

    // The log service only holds a host connection while a view is active.
    onVisibleChanged: SystemLogService.setViewActive(memoryDialog, visible)
    Component.onDestruction: SystemLogService.setViewActive(memoryDialog, false)
    readonly property bool serviceReady: SystemLogService.ready
    onServiceReadyChanged: if (serviceReady && visible && !busy) refresh()
    onOpened: if (serviceReady) refresh()
    onClosed: memoryDialog.pendingId = ""

    Connections {
        target: SystemLogService
        function onOperationCompleted(id, operation, success, result, reason) {
            if (id !== memoryDialog.pendingId || operation !== "memorySnapshot") return
            memoryDialog.pendingId = ""
            if (success) memoryDialog.snapshot = result
            else memoryDialog.errorMessage = TranslationManager.translate("settings.debug.memoryFailed",
                "Memory usage could not be read (%1).").arg(reason)
        }
    }
    Connections {
        target: LocalClipboard
        function onOperationCompleted(requestId, operation, success, text, reason) {
            if (requestId !== memoryDialog.clipboardRequestId) return
            memoryDialog.clipboardRequestId = ""
            copiedToast.show(success ? TranslationManager.translate("settings.debug.memoryCopied", "Copied")
                                     : TranslationManager.translate("common.copyFailed", "Could not copy to the clipboard."))
        }
    }
    StatusToast { id: copiedToast; parent: Overlay.overlay }

    contentItem: ColumnLayout {
        spacing: Theme.spacingMedium
        Accessible.role: Accessible.Dialog
        Accessible.name: memoryDialog.title

        Text {
            Layout.fillWidth: true
            visible: memoryDialog.errorMessage !== ""
            text: memoryDialog.errorMessage
            color: Theme.errorColor
            font: Theme.bodyFont
            wrapMode: Text.WordWrap
            Accessible.role: Accessible.AlertMessage
            Accessible.name: text
        }

        Flow {
            Layout.fillWidth: true
            spacing: Theme.spacingSmall
            Repeater {
                model: [
                    { label: TranslationManager.translate("settings.debug.memoryCurrent", "Current"),
                      value: memoryDialog.megabytes(memoryDialog.snapshot.current) },
                    { label: TranslationManager.translate("settings.debug.memoryPeak", "Peak"),
                      value: memoryDialog.megabytes(memoryDialog.snapshot.peak) },
                    { label: TranslationManager.translate("settings.debug.memoryStartup", "At startup"),
                      value: memoryDialog.megabytes(memoryDialog.snapshot.startup) },
                    { label: TranslationManager.translate("settings.debug.memoryGrowth", "Growth"),
                      value: memoryDialog.snapshot.current && memoryDialog.snapshot.startup
                          ? (memoryDialog.snapshot.current.rssMB - memoryDialog.snapshot.startup.rssMB).toFixed(1) + " MB" : "—" },
                    { label: TranslationManager.translate("settings.debug.memoryObjects", "QObjects"),
                      value: memoryDialog.snapshot.current ? String(memoryDialog.snapshot.current.qobjectCount) : "—" },
                    { label: TranslationManager.translate("settings.debug.memoryUptime", "Uptime"),
                      value: memoryDialog.snapshot.uptimeMinutes !== undefined
                          ? TranslationManager.translate("settings.debug.memoryMinutes", "%1 min").arg(memoryDialog.snapshot.uptimeMinutes) : "—" }
                ]
                delegate: Rectangle {
                    id: tile
                    required property var modelData
                    width: Theme.scaled(130)
                    height: tileColumn.implicitHeight + Theme.spacingSmall * 2
                    radius: Theme.cardRadius
                    color: Theme.backgroundColor
                    Accessible.role: Accessible.StaticText
                    Accessible.name: tile.modelData.label + ": " + tile.modelData.value
                    Column {
                        id: tileColumn
                        anchors.centerIn: parent
                        width: parent.width - Theme.spacingSmall * 2
                        Text {
                            width: parent.width
                            text: tile.modelData.value
                            color: Theme.textColor
                            font: Theme.subtitleFont
                            horizontalAlignment: Text.AlignHCenter
                            elide: Text.ElideRight
                            Accessible.ignored: true
                        }
                        Text {
                            width: parent.width
                            text: tile.modelData.label
                            color: Theme.textSecondaryColor
                            font: Theme.captionFont
                            horizontalAlignment: Text.AlignHCenter
                            elide: Text.ElideRight
                            Accessible.ignored: true
                        }
                    }
                }
            }
        }

        Text {
            Layout.fillWidth: true
            visible: memoryDialog.snapshot.instrumentedBuild === true
            text: TranslationManager.translate("settings.debug.memoryInstrumented",
                "This is a debug or sanitizer build, which uses much more memory than a release build.")
            color: Theme.textSecondaryColor
            font: Theme.captionFont
            wrapMode: Text.WordWrap
        }

        Canvas {
            id: trend
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.scaled(140)
            visible: memoryDialog.samples.length > 1
            Accessible.role: Accessible.Graphic
            Accessible.name: TranslationManager.translate("settings.debug.memoryTrend", "Memory usage over time")
            onWidthChanged: requestPaint()
            Connections {
                target: memoryDialog
                function onSnapshotChanged() { trend.requestPaint() }
            }
            onPaint: {
                const ctx = getContext("2d")
                ctx.reset()
                const points = memoryDialog.samples
                if (points.length < 2) return
                let low = points[0].rss, high = points[0].rss
                for (const p of points) { low = Math.min(low, p.rss); high = Math.max(high, p.rss) }
                const span = Math.max(1, high - low)
                const t0 = points[0].t, tSpan = Math.max(1, points[points.length - 1].t - t0)
                ctx.strokeStyle = Theme.primaryColor
                ctx.lineWidth = Theme.scaled(2)
                ctx.beginPath()
                for (let i = 0; i < points.length; ++i) {
                    const x = (points[i].t - t0) / tSpan * width
                    const y = height - (points[i].rss - low) / span * (height - Theme.scaled(16)) - Theme.scaled(8)
                    if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y)
                }
                ctx.stroke()
                ctx.fillStyle = Theme.textSecondaryColor
                ctx.font = Theme.captionFont.pixelSize + "px '" + Theme.captionFont.family + "'"
                ctx.fillText(high.toFixed(0) + " MB", Theme.scaled(4), Theme.scaled(12))
                ctx.fillText(low.toFixed(0) + " MB", Theme.scaled(4), height - Theme.scaled(2))
            }
        }

        Text {
            text: TranslationManager.translate("settings.debug.memoryClasses", "Largest QObject classes (change since startup)")
            color: Theme.textColor
            font: Theme.bodyFont
        }
        ListView {
            id: classList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: memoryDialog.topClasses
            ScrollBar.vertical: ScrollBar {}
            delegate: RowLayout {
                id: classRow
                required property var modelData
                width: classList.width
                Accessible.role: Accessible.StaticText
                Accessible.name: classRow.modelData.name + ": " + classRow.modelData.count
                Text {
                    Layout.fillWidth: true
                    text: classRow.modelData.name
                    color: Theme.textColor
                    font: Theme.captionFont
                    elide: Text.ElideRight
                    Accessible.ignored: true
                }
                Text {
                    text: String(classRow.modelData.count)
                    color: Theme.textColor
                    font: Theme.captionFont
                    Accessible.ignored: true
                }
                Text {
                    Layout.preferredWidth: Theme.scaled(60)
                    horizontalAlignment: Text.AlignRight
                    text: classRow.modelData.delta > 0 ? "+" + classRow.modelData.delta : String(classRow.modelData.delta)
                    color: classRow.modelData.delta > 0 ? Theme.warningColor : Theme.textSecondaryColor
                    font: Theme.captionFont
                    Accessible.ignored: true
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            BusyIndicator {
                running: memoryDialog.busy
                visible: running
                Layout.preferredHeight: Theme.scaled(28)
                Layout.preferredWidth: Theme.scaled(28)
            }
            Item { Layout.fillWidth: true }
            AccessibleButton {
                accessibleName: TranslationManager.translate("settings.debug.memoryCopy", "Copy summary")
                text: accessibleName
                enabled: (memoryDialog.snapshot.summary || "") !== "" && memoryDialog.clipboardRequestId === ""
                onClicked: {
                    const requestId = LocalClipboard.newRequestId()
                    memoryDialog.clipboardRequestId = requestId
                    if (!LocalClipboard.writeText(requestId, memoryDialog.snapshot.summary)) {
                        memoryDialog.clipboardRequestId = ""
                        copiedToast.show(TranslationManager.translate("common.copyFailed", "Could not copy to the clipboard."))
                    }
                }
            }
            AccessibleButton {
                accessibleName: TranslationManager.translate("common.refresh", "Refresh")
                text: accessibleName
                enabled: !memoryDialog.busy && SystemLogService.ready
                onClicked: memoryDialog.refresh()
            }
            AccessibleButton {
                accessibleName: TranslationManager.translate("common.close", "Close")
                text: accessibleName
                onClicked: memoryDialog.close()
            }
        }
    }
}
