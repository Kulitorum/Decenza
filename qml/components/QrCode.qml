import QtQuick
import "qrcode.js" as QR

Item {
    id: root

    property string value: ""
    property color foreground: "#000000"
    property color background: "#ffffff"
    property int quietZone: 4

    // Internal: computed QR matrix
    property var _matrix: null
    property int _modules: 0

    onValueChanged: _rebuild()
    Component.onCompleted: _rebuild()

    function _rebuild() {
        if (!value) {
            _matrix = null;
            _modules = 0;
            return;
        }
        var m = QR.generate(value);
        if (!m) {
            _matrix = null;
            _modules = 0;
            return;
        }
        _modules = m.length;
        // Flatten to a simple array for the Repeater
        var flat = [];
        for (var y = 0; y < m.length; y++)
            for (var x = 0; x < m.length; x++)
                flat.push(m[y][x] ? 1 : 0);
        _matrix = flat;
    }

    // White background with quiet zone
    Rectangle {
        anchors.fill: parent
        color: root.background
    }

    // Grid of modules
    Grid {
        id: qrGrid
        anchors.centerIn: parent
        columns: root._modules
        visible: root._matrix !== null && root._modules > 0

        // Cell size: integer pixels for sharp edges
        property int cellSize: {
            if (root._modules <= 0) return 1;
            var total = root._modules + root.quietZone * 2;
            var s = Math.floor(Math.min(root.width, root.height) / total);
            return Math.max(s, 1);
        }

        width: root._modules * cellSize
        height: root._modules * cellSize

        Repeater {
            model: root._matrix ? root._matrix.length : 0

            Rectangle {
                required property int index
                width: qrGrid.cellSize
                height: qrGrid.cellSize
                color: root._matrix[index] ? root.foreground : root.background
            }
        }
    }
}
