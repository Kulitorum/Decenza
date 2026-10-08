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
        previousShotId = 0
        if (shotId > 0) MainController.shotHistory.requestPreviousShot(shotId)
    }
    onShotIdChanged: _request()
    Component.onCompleted: _request()

    Connections {
        target: MainController.shotHistory
        function onPreviousShotReady(id, previousId) {
            if (id === root.shotId) root.previousShotId = previousId
        }
    }

    onClicked: {
        MainController.shotComparison.clearAll()
        MainController.shotComparison.addShots([root.previousShotId, root.shotId])
        AppShell.shotComparisonRequested()
    }
}
