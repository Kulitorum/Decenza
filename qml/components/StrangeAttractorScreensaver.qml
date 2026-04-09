import QtQuick
import Decenza

Item {
    id: root

    property bool running: true
    property int pointsPerFrame: 500

    // Background
    Rectangle {
        anchors.fill: parent
        color: "black"
    }

    // Strange attractor renderer
    StrangeAttractorRenderer {
        id: renderer
        anchors.fill: parent
        running: root.running && root.visible
        pointsPerFrame: root.pointsPerFrame
    }

    // Subtle attractor name in corner (fades out)
    Text {
        id: attractorLabel
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.margins: Theme.scaled(20)
        text: renderer.attractorName
        color: Theme.primaryContrastColor
        opacity: 0.4
        font.pixelSize: Theme.scaled(14)
        font.weight: Font.Light

        OpacityAnimator {
            id: labelFadeOut
            target: attractorLabel
            from: 0.4
            to: 0
            duration: 10000
            running: true
        }

        // Restart fade animation when attractor changes
        Connections {
            target: renderer
            function onAttractorNameChanged() {
                attractorLabel.opacity = 0.4
                labelFadeOut.restart()
            }
        }
    }

    // Reset on double-tap for a new attractor
    MouseArea {
        anchors.fill: parent
        onDoubleClicked: {
            renderer.randomize()
        }
    }

    function reset() {
        renderer.randomize()
    }
}
