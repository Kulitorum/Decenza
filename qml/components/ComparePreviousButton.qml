import QtQuick
import Decenza

// "Compare": opens the comparison with the shot pulled just before this one on
// the same profile and equipment as the base. Hidden until that shot is known
// to exist, so it never offers a comparison with nothing.
AccessibleButton {
    id: root

    property int shotId: 0
    property int previousShotId: 0

    visible: previousShotId > 0
    text: TranslationManager.translate("comparison.compareWithPrevious", "Compare")
    accessibleName: TranslationManager.translate("comparison.compareWithPreviousLong", "Compare with previous shot")

    function _request() {
        if (shotId > 0) MainController.shotHistory.requestPreviousShot(shotId)
    }
    onShotIdChanged: {
        previousShotId = 0
        _request()
    }
    Component.onCompleted: _request()

    Connections {
        target: MainController.shotHistory
        function onPreviousShotReady(id, previousId) {
            if (id === root.shotId) root.previousShotId = previousId
        }
        // A saved edit can re-point the shot's equipment (or anything else the
        // match keys on), so the previous shot is looked up again.
        function onShotMetadataUpdated(id, success) {
            if (success && id === root.shotId) root._request()
        }
    }

    onClicked: {
        MainController.shotComparison.clearAll()
        MainController.shotComparison.addShots([root.previousShotId, root.shotId])
        AppShell.shotComparisonRequested()
    }
}
