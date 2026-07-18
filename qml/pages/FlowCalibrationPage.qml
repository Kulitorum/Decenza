import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtGraphs
import Decenza
import "../components"
import "../components/graphs"

Page {
    id: calibrationPage
    // Declarative so it re-evaluates on a language change. This used to be an
    // imperative assignment in onCompleted/onActivated, which ran once and left
    // page titles in the previous language until you navigated away and back.
    readonly property string pageTitle: TranslationManager.translate("flowCalibration.title", "Flow Calibration")

    objectName: "flowCalibrationPage"
    background: ThemedPageBackground {}

    Component.onCompleted: {
        FlowCalibrationModel.loadRecentShots()
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.scaled(12)
        anchors.topMargin: Theme.pageTopMargin
        spacing: Theme.scaled(8)

        // Graph area
        GraphsView {
            id: chart
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: Theme.scaled(200)

            theme: DecenzaGraphsTheme {}

            axisX: timeAxis
            axisY: valueAxis

            ValueAxis {
                id: timeAxis
                min: 0
                // Round up to a whole second so the rightmost tick (placed by
                // tickInterval: 1) coincides with the right edge of the plot
                // — no dead space past the last tick.
                max: Math.max(5, Math.ceil((FlowCalibrationModel?.maxTime ?? 0) + 1))
                tickInterval: 1
                subTickCount: 0
                labelFormat: "%.0f"
                titleText: "s"
            }

            AutoRangingAxis {
                id: valueAxis
                series: [flowSeries, weightFlowSeries]
                padding: 0.05
                minFloor: 0
                fallbackMax: 2.0
                tickInterval: 2
                subTickCount: 0
                labelFormat: "%.1f"
                titleText: "mL/s  ·  g/s"
            }

            LineSeries {
                id: flowSeries
                name: TranslationManager.translate("flowCalibration.flow", "Flow (calibrated)")
                color: Theme.flowColor
                width: Theme.graphLineWidth
            }

            LineSeries {
                id: weightFlowSeries
                name: TranslationManager.translate("flowCalibration.weightFlow", "Weight flow")
                color: Theme.weightColor
                width: Theme.graphLineWidth
            }
        }

        CustomLegend {
            Layout.fillWidth: true
            entries: [
                { label: flowSeries.name,       color: Theme.flowColor,   active: flowSeries.visible },
                { label: weightFlowSeries.name, color: Theme.weightColor, active: weightFlowSeries.visible }
            ]
            onEntryToggled: (index, nowActive) => {
                if (index === 0) flowSeries.visible = nowActive
                else             weightFlowSeries.visible = nowActive
            }
        }

        BusyIndicator {
            Layout.alignment: Qt.AlignHCenter
            running: FlowCalibrationModel?.loading ?? false
            visible: FlowCalibrationModel?.loading ?? false
            Accessible.ignored: true
        }

        // Error message (shown when no data)
        Text {
            Layout.fillWidth: true
            text: FlowCalibrationModel?.errorMessage ?? ""
            color: Theme.textSecondaryColor
            font.pixelSize: Theme.scaled(14)
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            visible: !(FlowCalibrationModel?.hasData ?? true)
                     && (FlowCalibrationModel?.errorMessage?.length ?? 0) > 0
                     && !(FlowCalibrationModel?.loading ?? false)
        }

        // Shot navigation row
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.scaled(10)

            AccessibleButton {
                accessibleName: TranslationManager.translate("flowCalibration.previousShot", "Previous shot")
                text: "◀"
                enabled: (FlowCalibrationModel?.hasPreviousShot ?? false) && !(FlowCalibrationModel?.loading ?? false)
                onClicked: FlowCalibrationModel.previousShot()
            }

            Text {
                Layout.fillWidth: true
                text: (FlowCalibrationModel?.hasData ?? false)
                      ? TranslationManager.translate("flowCalibration.shotCounter", "Shot") + " "
                        + ((FlowCalibrationModel?.currentShotIndex ?? 0) + 1) + "/" + (FlowCalibrationModel?.shotCount ?? 0)
                        + "    " + (FlowCalibrationModel?.shotInfo ?? "")
                      : TranslationManager.translate("flowCalibration.noData", "No shots available")
                color: Theme.textColor
                font.pixelSize: Theme.scaled(13)
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
            }

            AccessibleButton {
                accessibleName: TranslationManager.translate("flowCalibration.nextShot", "Next shot")
                text: "▶"
                enabled: (FlowCalibrationModel?.hasNextShot ?? false) && !(FlowCalibrationModel?.loading ?? false)
                onClicked: FlowCalibrationModel.nextShot()
            }
        }

        // Multiplier controls
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: multiplierContent.implicitHeight + Theme.scaled(20)
            color: Theme.cardBackgroundColor
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
                        text: (FlowCalibrationModel?.multiplier ?? 1.0).toFixed(2)
                        color: Theme.primaryColor
                        font.pixelSize: Theme.scaled(18)
                        font.bold: true
                    }

                    Item { Layout.fillWidth: true }

                    AccessibleButton {
                        text: "-0.01"
                        accessibleName: TranslationManager.translate("flowCalibration.decrease", "Decrease multiplier")
                        enabled: (FlowCalibrationModel?.hasData ?? false) && (FlowCalibrationModel?.multiplier ?? 0) > 0.36
                        onClicked: FlowCalibrationModel.multiplier = Math.max(0.35, FlowCalibrationModel.multiplier - 0.01)
                    }

                    AccessibleButton {
                        text: "+0.01"
                        accessibleName: TranslationManager.translate("flowCalibration.increase", "Increase multiplier")
                        enabled: (FlowCalibrationModel?.hasData ?? false) && (FlowCalibrationModel?.multiplier ?? 0) < 2.99
                        onClicked: FlowCalibrationModel.multiplier = Math.min(3.0, FlowCalibrationModel.multiplier + 0.01)
                    }
                }

                Slider {
                    id: multiplierSlider
                    Layout.fillWidth: true
                    Layout.preferredHeight: Theme.scaled(40)
                    from: 0.35
                    to: 3.0
                    stepSize: 0.01
                    value: FlowCalibrationModel?.multiplier ?? 1.0
                    enabled: FlowCalibrationModel?.hasData ?? false
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
                enabled: FlowCalibrationModel?.hasData ?? false
                onClicked: FlowCalibrationModel.resetToFactory()
            }

            Item { Layout.fillWidth: true }

            AccessibleButton {
                text: TranslationManager.translate("flowCalibration.save", "Save")
                accessibleName: TranslationManager.translate("flowCalibration.saveAccessible", "Save flow calibration to machine")
                primary: true
                enabled: FlowCalibrationModel?.hasData ?? false
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

        var fData = FlowCalibrationModel.flowData
        for (var i = 0; i < fData.length; i++) {
            flowSeries.append(fData[i].x, fData[i].y)
        }

        var wfData = FlowCalibrationModel.weightFlowData
        for (i = 0; i < wfData.length; i++) {
            weightFlowSeries.append(wfData[i].x, wfData[i].y)
        }
    }
}
