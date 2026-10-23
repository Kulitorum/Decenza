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
}
