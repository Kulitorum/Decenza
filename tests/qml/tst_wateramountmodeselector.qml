import QtQuick
import QtTest
import Decenza 1.0

TestCase {
    name: "WaterAmountModeSelector"
    when: windowShown
    visible: true
    width: 460
    height: 100

    WaterAmountModeSelector {
        id: selector
        width: 450
        height: 70
    }

    function test_selectedFixedModePreservesMillilitres() {
        selector.amountMode = "fixed"
        selector.measureMode = "volume"
        selector.fixedAmount = 150
        mouseClick(findChild(selector, "fixedWaterAmountMode"))
        compare(selector.amountMode, "fixed")
        compare(selector.measureMode, "volume")
        compare(selector.fixedAmount, 150)
    }

    function test_customRatioSurvivesRoundTrip() {
        selector.amountMode = "ratio"
        selector.waterRatio = 2.7
        selector.previewValid = true
        selector.previewAmountG = 108
        mouseClick(findChild(selector, "fixedWaterAmountMode"))
        compare(selector.amountMode, "fixed")
        compare(selector.measureMode, "weight")
        compare(selector.fixedAmount, 108)
        mouseClick(findChild(selector, "ratioWaterAmountMode"))
        compare(selector.amountMode, "ratio")
        compare(selector.waterRatio, 2.7)
    }
}
