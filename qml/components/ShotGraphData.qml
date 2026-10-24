import QtQml

// Typed input contract for the shared graph view. Point and marker collections
// retain the QVariantList shapes already emitted by ShotDataModel; they contain
// QPointF values and marker-role records respectively, never service objects.
QtObject {
    property real rawTime: 0
    property var phaseMarkers: []
    property var pressureGoalPoints: []
    property var flowGoalPoints: []
    property var temperatureGoalPoints: []
    property var temperatureMixGoalPoints: []
    property real profileTargetWeight: 0
    property real machineTargetWeight: 0
    property real weightAtStop: 0
    property real finalWeight: 0
}
