import QtQuick

// Resolved presentation inputs for the shared equipment-inventory page.
// Native Theme and browser appearance adapters provide these values.
QtObject {
    property font titleFont
    property font subtitleFont
    property font bodyFont

    property color textColor: "black"
    property color textSecondaryColor: "gray"

    property int pageTopMargin: 0
    property int pageBottomMargin: 0
    property int pageHorizontalMargin: 0
    property int contentBottomPadding: 0
    property int contentSpacing: 16
    property int headerSpacing: 16
    property int emptyTopMargin: 40
    property int emptySpacing: 8
    property int cardSpacing: 16
    property real minimumColumnWidth: 380
}
