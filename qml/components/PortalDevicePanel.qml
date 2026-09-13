pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Decenza

ColumnLayout {
    id: panel
    spacing: Theme.scaled(8)
    Text {
        Layout.fillWidth: true
        text: "Belka PORTAL" + (BelkaPortal.savedName.length ? " · " + BelkaPortal.savedName : "")
        color: Theme.textColor
        font.pixelSize: Theme.scaled(16)
        font.bold: true
        wrapMode: Text.Wrap
    }
    PortalReadout { Layout.fillWidth: true }
    RowLayout {
        Layout.fillWidth: true
        AccessibleButton {
            text: BelkaPortal.active
                ? TranslationManager.translate("portal.disconnect", "Disconnect")
                : TranslationManager.translate("portal.connect", "Connect")
            accessibleName: text + " PORTAL"
            enabled: !BelkaPortal.machineBusy && BelkaPortal.savedAddress.length > 0
            onClicked: BelkaPortal.active ? BelkaPortal.disconnectDevice() : BelkaPortal.reconnect()
        }
        AccessibleButton {
            text: TranslationManager.translate("portal.forget", "Forget")
            accessibleName: text + " PORTAL"
            enabled: !BelkaPortal.machineBusy && BelkaPortal.savedAddress.length > 0
            onClicked: BelkaPortal.forgetDevice()
        }
        Item { Layout.fillWidth: true }
    }
    Text {
        Layout.fillWidth: true
        visible: BelkaPortal.savedAddress.length === 0
        text: TranslationManager.translate("portal.commonScan", "Use Scan for Devices to select PORTAL.")
        color: Theme.textSecondaryColor
        wrapMode: Text.Wrap
    }
    RowLayout {
        Layout.fillWidth: true
        Text {
            id: syncLabel
            Layout.fillWidth: true
            text: TranslationManager.translate("portal.syncDisplay", "Sync PORTAL display with the shot")
            color: Theme.textColor
            wrapMode: Text.Wrap
            font: Theme.bodyFont
        }
        StyledSwitch {
            checked: BelkaPortal.syncDisplay
            enabled: !BelkaPortal.machineBusy
            onToggled: BelkaPortal.syncDisplay = checked
            accessibleName: syncLabel.text
        }
    }
    Text {
        Layout.fillWidth: true
        visible: BelkaPortal.errorMessage.length > 0
        text: BelkaPortal.errorMessage
        color: Theme.warningColor
        wrapMode: Text.Wrap
    }
    AccessibleButton {
        id: diagnosticToggle
        property bool expanded: false
        text: TranslationManager.translate("portal.diagnostics", "PORTAL diagnostics") + (expanded ? " −" : " +")
        accessibleName: text
        onClicked: expanded = !expanded
    }
    ColumnLayout {
        Layout.fillWidth: true
        visible: diagnosticToggle.expanded
        Text {
            Layout.fillWidth: true
            text: TranslationManager.translate("portal.measurementNote", "EC is recorded as a raw value; units are unverified. The display commands do not report whether PORTAL saved a session.")
            color: Theme.textSecondaryColor
            wrapMode: Text.Wrap
        }
        GridLayout {
            Layout.fillWidth: true
            columns: panel.width >= Theme.scaled(420) ? 2 : 1
            AccessibleButton {
                text: TranslationManager.translate("portal.showGraph", "Show device graph")
                accessibleName: text
                enabled: BelkaPortal.canControlDisplay && !BelkaPortal.machineBusy && BelkaPortal.displayCommandStatus !== "requested"
                onClicked: BelkaPortal.setGraphView(true)
            }
            AccessibleButton {
                text: TranslationManager.translate("portal.hideGraph", "End device graph")
                accessibleName: text
                enabled: BelkaPortal.canControlDisplay && !BelkaPortal.machineBusy && BelkaPortal.displayCommandStatus !== "requested"
                onClicked: BelkaPortal.setGraphView(false)
            }
        }
        Text {
            Layout.fillWidth: true
            color: Theme.textSecondaryColor
            wrapMode: Text.Wrap
            text: BelkaPortal.displayCommandStatus === "acknowledged"
                ? TranslationManager.translate("portal.commandAck", "Display command delivered. Check the PORTAL screen.")
                : BelkaPortal.displayCommandStatus === "requested"
                  ? TranslationManager.translate("portal.commandPending", "Sending display command…") : ""
        }
        Text {
            Layout.fillWidth: true
            color: Theme.textSecondaryColor
            font.pixelSize: Theme.scaled(12)
            wrapMode: Text.WrapAnywhere
            text: TranslationManager.translate("portal.packets", "Received packets") + ": "
                + BelkaPortal.packetCount + "\n" + BelkaPortal.lastPacket
        }
    }
    Rectangle {
        Layout.fillWidth: true
        implicitHeight: 1
        color: Theme.textSecondaryColor
        opacity: 0.3
    }
}
