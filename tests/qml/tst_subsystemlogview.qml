import QtQuick
import QtTest
import Decenza 1.0

TestCase {
    id: testCase
    name: "SubsystemLogView"
    when: windowShown
    visible: true
    width: 480
    height: 320
    property bool viewVisible: false

    SubsystemLogView {
        id: view
        x: 8
        y: 8
        width: 464
        height: 304
        visible: testCase.viewVisible
        markers: ["[Scale]"]
        showClear: false
        showShare: false
    }

    function init() {
        viewVisible = false
        tryCompare(SystemLogService, "activeViewCount", 0)
        SystemLogService.resetForTest()
        view.markers = ["[Scale]"]
        view.minLevel = "INFO"
        view.maxLines = 400
    }

    function test_clearIgnoresLateSnapshotAndKeepsOnlyLinesAfterBoundary() {
        viewVisible = true
        tryCompare(SystemLogService, "snapshotCount", 1)
        var oldSnapshotId = SystemLogService.lastSnapshotRequestId

        view.clearView()
        tryCompare(SystemLogService, "boundaryCount", 1)
        var boundaryId = SystemLogService.lastBoundaryRequestId

        SystemLogService.appendLine("[0.400] INFO [Scale] before boundary", 40)
        SystemLogService.appendLine("[0.500] INFO [Scale] after boundary", 51)
        SystemLogService.completeSnapshot(oldSnapshotId, true,
                                          ["[0.300] INFO [Scale] stale snapshot"], 30, "")
        compare(view._lines.length, 0)

        SystemLogService.completeBoundary(boundaryId, true, 50, "")
        compare(view._lines.length, 1)
        verify(view._lines[0].indexOf("after boundary") >= 0)
        verify(view._lines[0].indexOf("before boundary") < 0)
        compare(view._shownThrough, 51)
    }

    function test_everyLiveMultilineDetailSurvivesTheSnapshotFence() {
        viewVisible = true
        tryCompare(SystemLogService, "snapshotCount", 1)
        const request = SystemLogService.lastSnapshotRequestId
        // The first diagnostic is already in the snapshot; both of its queued
        // physical lines must be discarded, while all newer detail survives.
        SystemLogService.appendLine("[0.100] INFO [Scale] before first", 10)
        SystemLogService.appendLine("[0.100] INFO [Scale] before second", 11)
        SystemLogService.appendLine("[0.200] WARN [Scale] live first", 12)
        SystemLogService.appendLine("[0.200] WARN [Scale] live second", 13)
        SystemLogService.appendLine("[0.200] WARN [Scale] live third", 14)
        SystemLogService.completeSnapshot(request, true,
            ["[0.100] INFO [Scale] before first", "[0.100] INFO [Scale] before second"], 11, "")
        compare(view._lines.length, 5)
        compare(view._lines[2], "[0.200] WARN [Scale] live first")
        compare(view._lines[3], "[0.200] WARN [Scale] live second")
        compare(view._lines[4], "[0.200] WARN [Scale] live third")
        compare(view._shownThrough, 14)
        SystemLogService.appendLine("[0.200] WARN [Scale] duplicate", 14)
        compare(view._lines.length, 5)
    }

    function test_hiddenViewDetachesAndRefillsWhenShownAgain() {
        viewVisible = true
        tryCompare(SystemLogService, "activeViewCount", 1)
        tryCompare(SystemLogService, "snapshotCount", 1)
        var firstSnapshotId = SystemLogService.lastSnapshotRequestId
        SystemLogService.completeSnapshot(firstSnapshotId, true,
                                          ["[0.100] INFO [Scale] first fill"], 10, "")
        compare(view._lines.length, 1)

        viewVisible = false
        tryCompare(SystemLogService, "activeViewCount", 0)
        SystemLogService.appendLine("[0.200] INFO [Scale] hidden line", 11)
        compare(view._lines.length, 0)
        compare(SystemLogService.snapshotCount, 1)

        viewVisible = true
        tryCompare(SystemLogService, "activeViewCount", 1)
        tryCompare(SystemLogService, "snapshotCount", 2)
        var secondSnapshotId = SystemLogService.lastSnapshotRequestId
        SystemLogService.completeSnapshot(secondSnapshotId, true,
                                          ["[0.300] INFO [Scale] refilled"], 12, "")
        compare(view._lines.length, 1)
        verify(view._lines[0].indexOf("refilled") >= 0)
    }

    function test_waitsForReadinessAndReconnectRejectsOldCompletions() {
        SystemLogService.setReadyForTest(false)
        viewVisible = true
        compare(SystemLogService.snapshotCount, 0)
        SystemLogService.setReadyForTest(true)
        compare(SystemLogService.snapshotCount, 1)
        const abandonedSnapshot = SystemLogService.lastSnapshotRequestId
        view.clearView()
        const abandonedBoundary = SystemLogService.lastBoundaryRequestId
        SystemLogService.setReadyForTest(false)
        compare(view._clearing, false)
        compare(view._snapshotRequestId, "")
        compare(view._boundaryRequestId, "")
        compare(view._lines.length, 1)
        verify(view._lines[0].indexOf("disconnected") >= 0)
        SystemLogService.completeSnapshot(abandonedSnapshot, true, ["stale snapshot"], 200, "")
        SystemLogService.completeBoundary(abandonedBoundary, true, 300, "")
        compare(view._shownThrough, 0)
        SystemLogService.setReadyForTest(true)
        compare(SystemLogService.snapshotCount, 2)
        const recoveredSnapshot = SystemLogService.lastSnapshotRequestId
        verify(recoveredSnapshot !== abandonedSnapshot)
        SystemLogService.appendLine("[0.200] INFO [Scale] recovered stream", 21)
        SystemLogService.completeSnapshot(recoveredSnapshot, true,
            ["[0.100] INFO [Scale] recovered snapshot"], 20, "")
        compare(view._lines.length, 2)
        compare(view._shownThrough, 21)
        SystemLogService.completeSnapshot(abandonedSnapshot, true, ["stale snapshot"], 200, "")
        compare(view._lines.length, 2)
        compare(view._shownThrough, 21)
    }

    function test_filtersLiveLevelsAndMarkersAndKeepsBothBuffersBounded() {
        view.minLevel = "WARN"
        view.maxLines = 4
        viewVisible = true
        const request = SystemLogService.lastSnapshotRequestId
        for (let sequence = 1; sequence <= 1100; ++sequence)
            SystemLogService.appendLine("[0.100] WARN [Scale] line " + sequence, sequence)
        compare(view._pendingLines.length, view._maxPendingLines)
        SystemLogService.completeSnapshot(request, true, [], 0, "")
        compare(view._pendingLines.length, 0)
        compare(view._lines.length, 4)
        verify(view._lines[0].endsWith("1097"))
        verify(view._lines[3].endsWith("1100"))
        SystemLogService.appendLine("[0.100] ERROR [DE1] other subsystem", 1101)
        SystemLogService.appendLine("[0.100] INFO [Scale] below threshold", 1102)
        compare(view._lines.length, 4)
        SystemLogService.appendLine("[0.100] ERROR [Scale] matching error", 1103)
        compare(view._lines.length, 4)
        verify(view._lines[3].endsWith("matching error"))
        compare(view._shownThrough, 1103)
    }

    function test_snapshotFailureCanRecoverAfterViewReopens() {
        viewVisible = true
        SystemLogService.completeSnapshot(SystemLogService.lastSnapshotRequestId,
                                         false, [], 0, "fixture read failure")
        compare(view._lines[0], "fixture read failure")
        viewVisible = false
        viewVisible = true
        compare(SystemLogService.snapshotCount, 2)
        SystemLogService.completeSnapshot(SystemLogService.lastSnapshotRequestId,
            true, ["[0.100] INFO [Scale] recovered"], 9, "")
        compare(view._lines.length, 1)
        verify(view._lines[0].endsWith("recovered"))
    }
}
