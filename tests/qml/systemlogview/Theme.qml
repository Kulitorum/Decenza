pragma Singleton
import QtQuick

QtObject {
    readonly property color surfaceColor: "#303030"
    readonly property color textSecondaryColor: "#dddddd"
    readonly property string monoFontFamily: "monospace"

    function scaled(value) { return value }
    function capAccessibleText(value) { return value }
}
