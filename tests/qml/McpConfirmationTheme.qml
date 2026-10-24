pragma Singleton
import QtQuick

// Presentation tokens only; confirmation behavior comes from the production dialog.
SharedButtonStyle {
    property real scale: 1
    property font titleFont: bodyFont
    property font subtitleFont: bodyFont
    property font labelFont: bodyFont
    property string monoFontFamily: Qt.application.font.family
    property int dialogWidth: 440
    property int dialogPadding: 12
    property int cardRadius: 8
    property int buttonRadius: 8
    property color dialogDimColor: "#80000000"
    bodyFont: Qt.font({ family: Qt.application.font.family, pixelSize: 16 })
    function contrastColorFor(color: color): color { return "black" }
}
