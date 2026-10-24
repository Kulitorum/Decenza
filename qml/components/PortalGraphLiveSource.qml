import QtQml

// Typed bridge between native shot capture and the shared PORTAL graph overlay.
// Browser history does not create this source; it supplies the saved sample list.
QtObject {
    function readSamples(): var { return [] }
    property int sampleCount: 0
    property real rawTime: 0
    property real ecMin: 0
    property real ecMax: 0.1

    signal sampleAdded(real time, real ecRaw, real temperatureC, bool breakBefore)
    signal cleared()
}
