import QtQuick
import Decenza

// One tappable keyword chip in the Shot History search Keywords sheet.
//
// The keyword string is declared ONCE, on `keyword`, and from there feeds the
// visible label, the accessible name, and whatever the handler inserts. This
// used to be eleven hand-copied Rectangle blocks, each restating its keyword in
// three separate places — a typo in any one of the three was invisible, because
// nothing compared them.
//
// `picked` carries no parameter on purpose: a signal argument named `keyword`
// would shadow the property inside the handler. Read `keyword` there instead.
Rectangle {
    id: chip

    property string keyword: ""
    signal picked()

    color: chipArea.pressed ? Theme.surfaceColor : "transparent"
    radius: Theme.scaled(4)
    implicitWidth: chipLabel.implicitWidth + Theme.scaled(8)
    implicitHeight: chipLabel.implicitHeight + Theme.scaled(4)

    Accessible.role: Accessible.Button
    Accessible.name: TranslationManager.translate("shothistory.insertKeyword", "Insert %1").arg(chip.keyword)
    Accessible.focusable: true
    Accessible.onPressAction: chip.picked()

    Text {
        id: chipLabel
        text: chip.keyword
        anchors.centerIn: parent
        font.pixelSize: Theme.labelFont.pixelSize
        color: Theme.primaryColor
        font.bold: true
        Accessible.ignored: true
    }

    MouseArea {
        id: chipArea
        anchors.fill: parent
        onClicked: chip.picked()
    }
}
