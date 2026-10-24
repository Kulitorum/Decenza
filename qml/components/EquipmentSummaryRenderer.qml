import QtQuick
import QtQuick.Layouts

// Theme- and backend-neutral view used by EquipmentSummary and by the Wasm
// feasibility screen. Callers provide already translated/formatted strings and
// explicit typography/colors; this component owns the production line order,
// wrapping, visibility and elision behavior without reaching into Decenza's
// native singletons.
ColumnLayout {
    id: view

    property string lineageText: ""
    property string titleRichText: ""
    property string grinderBurrsRichText: ""
    property string lastDialLineRichText: ""
    property string basketLineRichText: ""
    property string puckPrepLineRichText: ""

    // Typed visual inputs keep this component usable in a standalone QML module
    // while the native EquipmentSummary supplies the app's Theme values.
    property font bodyFont
    property font labelFont
    property font captionFont
    property real titlePixelSize: labelFont.pixelSize
    property color primaryTextColor: "black"
    property color secondaryTextColor: "gray"
    property real lineSpacing: 2

    spacing: lineSpacing

    Text {
        Layout.fillWidth: true
        visible: view.lineageText.length > 0
        text: view.lineageText
        font: view.labelFont
        color: view.secondaryTextColor
        wrapMode: Text.WordWrap
        Accessible.ignored: true
    }

    Text {
        Layout.fillWidth: true
        textFormat: Text.StyledText
        text: view.titleRichText
        font.family: view.bodyFont.family
        font.pixelSize: view.titlePixelSize
        font.bold: true
        color: view.primaryTextColor
        elide: Text.ElideRight
        Accessible.ignored: true
    }

    Text {
        Layout.fillWidth: true
        visible: view.grinderBurrsRichText.length > 0
        textFormat: Text.StyledText
        text: view.grinderBurrsRichText
        font: view.labelFont
        color: view.secondaryTextColor
        elide: Text.ElideRight
        Accessible.ignored: true
    }

    Text {
        Layout.fillWidth: true
        visible: view.lastDialLineRichText.length > 0
        text: view.lastDialLineRichText
        textFormat: Text.StyledText
        font: view.captionFont
        color: view.primaryTextColor
        elide: Text.ElideRight
        Accessible.ignored: true
    }

    // Keep separate basket and puck-prep details after the grinder identity
    // and dial-in line.
    Text {
        Layout.fillWidth: true
        visible: view.basketLineRichText.length > 0
        textFormat: Text.StyledText
        text: view.basketLineRichText
        font: view.labelFont
        color: view.secondaryTextColor
        elide: Text.ElideRight
        Accessible.ignored: true
    }

    Text {
        Layout.fillWidth: true
        visible: view.puckPrepLineRichText.length > 0
        text: view.puckPrepLineRichText
        textFormat: Text.StyledText
        font: view.labelFont
        color: view.secondaryTextColor
        wrapMode: Text.WordWrap
        Accessible.ignored: true
    }
}
