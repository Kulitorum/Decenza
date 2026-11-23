// One person's donation column in the Support This Project dialog.
import QtQuick
import QtQuick.Layouts
import Decenza

ColumnLayout {
    id: option

    property string who
    property string qrSource
    property real qrSize
    property bool active
    property string caption
    property string buttonText
    property string link

    Layout.fillWidth: true
    Layout.fillHeight: true
    Layout.preferredWidth: qrSize
    spacing: Theme.spacingSmall

    Text {
        Layout.fillWidth: true
        text: option.who
        font: Theme.subtitleFont
        color: Theme.textColor
        horizontalAlignment: Text.AlignHCenter
        elide: Text.ElideRight
    }

    // The only item that shrinks when the dialog is taller than the window.
    // Decoded only while open.
    Image {
        Layout.alignment: Qt.AlignHCenter
        Layout.preferredWidth: option.qrSize
        Layout.preferredHeight: option.qrSize
        Layout.fillHeight: true
        source: option.active ? option.qrSource : ""
        fillMode: Image.PreserveAspectFit
        mipmap: true
        Accessible.role: Accessible.Graphic
        Accessible.name: TranslationManager.translate("about.donateQrCodeFor", "Donation QR code for %1").arg(option.who)
    }

    Text {
        Layout.fillWidth: true
        text: option.caption
        font: Theme.captionFont
        color: Theme.textSecondaryColor
        horizontalAlignment: Text.AlignHCenter
        elide: Text.ElideMiddle
    }

    AccessibleButton {
        Layout.fillWidth: true
        primary: true
        text: option.buttonText
        accessibleName: option.buttonText + ", " + option.who
        onClicked: Qt.openUrlExternally(option.link)
    }
}
