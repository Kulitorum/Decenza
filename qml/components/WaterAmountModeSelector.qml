pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import Decenza

RowLayout {
    id: root

    property string amountMode: "fixed"
    property string measureMode: "weight"
    property int fixedAmount: 100
    property real waterRatio: 2.5
    property bool previewValid: false
    property int previewAmountG: 0

    function choose(mode) {
        if (root.amountMode === mode) return
        if (mode === "fixed") {
            if (root.previewValid) root.fixedAmount = root.previewAmountG
            root.measureMode = "weight"
        }
        root.amountMode = mode
    }

    AccessibleButton {
        objectName: "fixedWaterAmountMode"
        text: TranslationManager.translate("water.amount.fixed", "Fixed amount")
        accessibleName: text
        primary: root.amountMode === "fixed"
        onClicked: root.choose("fixed")
    }
    AccessibleButton {
        objectName: "ratioWaterAmountMode"
        text: TranslationManager.translate("water.amount.ratio", "Ratio to espresso")
        accessibleName: text
        primary: root.amountMode === "ratio"
        onClicked: root.choose("ratio")
    }
}
