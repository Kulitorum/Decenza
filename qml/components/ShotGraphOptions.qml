import QtQml

// Graph-specific user choices passed to the shared presentation component.
QtObject {
    property bool advancedMode: false
    property string rightAxisMode: "temperature"
    property int flowMultiplier: 1
    property bool showPressure: true
    property bool showFlow: true
    property bool showTemperature: true
    property bool showTemperatureMixGoal: true
    property bool showResistance: true
    property bool showConductance: true
    property bool showDarcyResistance: true
    property bool showWeightFlow: true
    property bool showTemperatureMix: true
    property bool showWeight: true
}
