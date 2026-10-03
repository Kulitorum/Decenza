import QtQuick
import QtGraphs

// One series per model entry, added to `graphsView`. A Repeater cannot do this: it
// creates Items, and a series is not one. A series also removes itself from its view
// when destroyed (qabstractseries.cpp:376-381).
Instantiator {
    id: root
    required property GraphsView graphsView

    onObjectAdded: (index, object) => root.graphsView.addSeries(object)
    onObjectRemoved: (index, object) => root.graphsView.removeSeries(object)
}
