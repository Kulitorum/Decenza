import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCharts
import Decenza
import "../components"

Page {
    id: calibrationPage
    objectName: "flowCalibrationPage"
    background: Rectangle { color: Theme.backgroundColor }

    property double maxFlow: 6.0  // Dynamic Y-axis max, updated by loadData()

    Component.onCompleted: {
        root.currentPageTitle = TranslationManager.translate("flowCalibration.title", "Flow Calibration")
        FlowCalibrationModel.loadRecentShots()
    }
    StackView.onActivated: root.currentPageTitle = TranslationManager.translate("flowCalibration.title", "Flow Calibration")

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.scaled(12)
        anchors.topMargin: Theme.pageTopMargin
        spacing: Theme.scaled(8)

        // Graph area
        ChartView {
            id: chart
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: Theme.scaled(200)
            antialiasing: true
            backgroundColor: "transparent"
            plotAreaColor: Qt.darker(Theme.surfaceColor, 1.3)
            legend.visible: true
            legend.labelColor: Theme.textSecondaryColor
            legend.alignment: Qt.AlignBottom

            margins.top: 0
            margins.bottom: 0
            margins.left: 0
            margins.right: 0

            ValueAxis {
                id: timeAxis
                min: 0
                max: Math.max(5, FlowCalibrationModel.maxTime + 2)
                tickCount: 7
                labelFormat: "%.0f"
                labelsColor: Theme.textSecondaryColor
                gridLineColor: Qt.rgba(255, 255, 255, 0.1)
                titleText: "s"
                titleBrush: Theme.textSecondaryColor
            }

            ValueAxis {
                id: valueAxis
                min: 0
                max: calibrationPage.maxFlow
                tickCount: 5
                labelFormat: "%.1f"
                labelsColor: Theme.textSecondaryColor
                gridLineColor: Qt.rgba(255, 255, 255, 0.1)
                titleText: "mL/s  ·  g/s"
                titleBrush: Theme.textSecondaryColor
            }

            LineSeries {
                id: flowSeries
                name: TranslationManager.translate("flowCalibration.flow", "Flow (calibrated)")
                color: Theme.flowColor
                width: Theme.graphLineWidth
                axisX: timeAxis
                axisY: valueAxis
            }

            LineSeries {
                id: weightFlowSeries
                name: TranslationManager.translate("flowCalibration.weightFlow", "Weight flow")
                color: Theme.weightColor
                width: Theme.graphLineWidth
                axisX: timeAxis
                axisY: valueAxis
            }
        }

        // Error message (shown when no data)
        Text {
            Layout.fillWidth: true
            text: FlowCalibrationModel.errorMessage
            color: Theme.textSecondaryColor
            font.pixelSize: Theme.scaled(14)
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            visible: !FlowCalibrationModel.hasData && FlowCalibrationModel.errorMessage.length > 0
        }

        // Shot navigation row
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.scaled(10)

            AccessibleButton {
                accessibleName: TranslationManager.translate("flowCalibration.previousShot", "Previous shot")
                text: "\u25C0"
                enabled: FlowCalibrationModel.hasPreviousShot
                onClicked: FlowCalibrationModel.previousShot()
            }

            Text {
                Layout.fillWidth: true
                text: FlowCalibrationModel.hasData
                      ? TranslationManager.translate("flowCalibration.shotCounter", "Shot") + " "
                        + (FlowCalibrationModel.currentShotIndex + 1) + "/" + FlowCalibrationModel.shotCount
                        + "    " + FlowCalibrationModel.shotInfo
                      : TranslationManager.translate("flowCalibration.noData", "No shots available")
                color: Theme.textColor
                font.pixelSize: Theme.scaled(13)
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
            }

            AccessibleButton {
                accessibleName: TranslationManager.translate("flowCalibration.nextShot", "Next shot")
                text: "\u25B6"
                enabled: FlowCalibrationModel.hasNextShot
                onClicked: FlowCalibrationModel.nextShot()
            }
        }

        // Multiplier controls
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: multiplierContent.implicitHeight + Theme.scaled(20)
            color: Theme.surfaceColor
            radius: Theme.cardRadius

            ColumnLayout {
                id: multiplierContent
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: Theme.scaled(10)
                spacing: Theme.scaled(6)

                RowLayout {
                    Layout.fillWidth: true

                    Text {
                        text: TranslationManager.translate("flowCalibration.multiplier", "Multiplier:")
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(14)
                    }

                    Text {
                        text: FlowCalibrationModel.multiplier.toFixed(2)
                        color: Theme.primaryColor
                        font.pixelSize: Theme.scaled(18)
                        font.bold: true
                    }

                    Item { Layout.fillWidth: true }

                    AccessibleButton {
                        text: "-0.01"
                        accessibleName: TranslationManager.translate("flowCalibration.decrease", "Decrease multiplier")
                        enabled: FlowCalibrationModel.hasData && FlowCalibrationModel.multiplier > 0.36
                        onClicked: FlowCalibrationModel.multiplier = Math.max(0.35, FlowCalibrationModel.multiplier - 0.01)
                    }

                    AccessibleButton {
                        text: "+0.01"
                        accessibleName: TranslationManager.translate("flowCalibration.increase", "Increase multiplier")
                        enabled: FlowCalibrationModel.hasData && FlowCalibrationModel.multiplier < 1.99
                        onClicked: FlowCalibrationModel.multiplier = Math.min(2.0, FlowCalibrationModel.multiplier + 0.01)
                    }
                }

                Slider {
                    id: multiplierSlider
                    Layout.fillWidth: true
                    Layout.preferredHeight: Theme.scaled(40)
                    from: 0.35
                    to: 2.0
                    stepSize: 0.01
                    value: FlowCalibrationModel.multiplier
                    enabled: FlowCalibrationModel.hasData
                    onMoved: FlowCalibrationModel.multiplier = value

                    Accessible.role: Accessible.Slider
                    Accessible.name: TranslationManager.translate("flowCalibration.multiplierSlider", "Flow calibration multiplier")
                }
            }
        }

        // Tip text
        Text {
            Layout.fillWidth: true
            text: TranslationManager.translate("flowCalibration.tip",
                  "Tip: Adjust until the flow curve matches the weight flow during the steady pour phase.")
            color: Theme.textSecondaryColor
            font.pixelSize: Theme.scaled(11)
            font.italic: true
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
        }

        // Action buttons
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.scaled(10)

            AccessibleButton {
                text: TranslationManager.translate("flowCalibration.reset", "Reset to 1.0")
                accessibleName: TranslationManager.translate("flowCalibration.resetAccessible", "Reset multiplier to factory default")
                enabled: FlowCalibrationModel.hasData
                onClicked: FlowCalibrationModel.resetToFactory()
            }

            Item { Layout.fillWidth: true }

            AccessibleButton {
                text: TranslationManager.translate("flowCalibration.save", "Save")
                accessibleName: TranslationManager.translate("flowCalibration.saveAccessible", "Save flow calibration to machine")
                primary: true
                enabled: FlowCalibrationModel.hasData
                onClicked: {
                    FlowCalibrationModel.save()
                    pageStack.pop()
                }
            }
        }
    }

    // Reload chart data when model data changes
    Connections {
        target: FlowCalibrationModel
        function onDataChanged() {
            loadData()
        }
    }

    function loadData() {
        flowSeries.clear()
        weightFlowSeries.clear()

        var peak = 0

        var fData = FlowCalibrationModel.flowData
        for (var i = 0; i < fData.length; i++) {
            flowSeries.append(fData[i].x, fData[i].y)
            if (fData[i].y > peak) peak = fData[i].y
        }

        var wfData = FlowCalibrationModel.weightFlowData
        for (i = 0; i < wfData.length; i++) {
            weightFlowSeries.append(wfData[i].x, wfData[i].y)
            if (wfData[i].y > peak) peak = wfData[i].y
        }

        // Set Y-axis to peak rounded up to nearest 0.5, minimum 2.0
        calibrationPage.maxFlow = Math.max(2.0, Math.ceil(peak * 2) / 2)
    }
}
