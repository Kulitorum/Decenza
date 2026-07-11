import QtQuick
import QtQuick.Layouts
import Decenza

// One labeled component row inside the recipe card (Shot Detail / Shot Review):
// a caption label over its value; hides itself when the value is empty so absent
// components leave no gap.
ColumnLayout {
    id: rf
    property string fieldLabel: ""
    property string value: ""
    Layout.fillWidth: true
    spacing: 0
    visible: rf.value !== ""
    Text {
        text: rf.fieldLabel
        font: Theme.captionFont
        color: Theme.textSecondaryColor
        Accessible.ignored: true
    }
    Text {
        Layout.fillWidth: true
        text: rf.value
        font: Theme.bodyFont
        color: Theme.textColor
        wrapMode: Text.WordWrap
        Accessible.ignored: true
    }
}
