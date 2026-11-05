pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import OwnershipProbe

StackView {
    id: root
    width: 800
    height: 600
    readonly property QtObject compareButton: currentItem ? currentItem.compareButton : null
    Component { id: review; ReplayPage {} }
    initialItem: review
    function reopen(): void { replaceCurrentItem(review) }
    function finishLoad(): void { currentItem.finishLoad() }
    function clearLoad(): void { currentItem.clearLoad() }
    function focusCompare(): void { currentItem.focusCompare() }
}
