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
        font: Theme.subtitleFont
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
            enabled: !BelkaPortal.machineBusy && BelkaPortal.state !== "disconnecting"
                && (BelkaPortal.active || BelkaPortal.owned)
            onClicked: BelkaPortal.active ? BelkaPortal.disconnectDevice() : BelkaPortal.reconnect()
        }
        AccessibleButton {
            text: TranslationManager.translate("portal.forget", "Forget")
            accessibleName: text + " PORTAL"
            enabled: !BelkaPortal.machineBusy && BelkaPortal.state !== "disconnecting"
                && (BelkaPortal.active || BelkaPortal.owned)
            onClicked: BelkaPortal.forgetDevice()
        }
        Item { Layout.fillWidth: true }
    }
    Text {
        Layout.fillWidth: true
        visible: !BelkaPortal.owned
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
        text: TranslationManager.translate("portal.connectionProblem", "PORTAL connection or measurement problem. Reconnect while the machine is idle; details are available below.")
        color: Theme.warningColor
        wrapMode: Text.Wrap
    }
    Text {
        Layout.fillWidth: true
        visible: BelkaPortal.displayCommandStatus === "failed"
        text: TranslationManager.translate("portal.displaySyncFailed", "PORTAL display sync failed. Measurements can continue; check the graph on PORTAL.")
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
            visible: BelkaPortal.errorMessage.length > 0
            // The raw message is diagnostic text from the transport, not a
            // translated string, so it is labelled rather than presented as the UI's own.
            text: TranslationManager.translate("portal.lastError", "Last error") + ": " + BelkaPortal.errorMessage
            color: Theme.textSecondaryColor
            wrapMode: Text.Wrap
        }
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
                  ? TranslationManager.translate("portal.commandPending", "Sending display command…")
                  : BelkaPortal.displayCommandStatus === "failed"
                    ? TranslationManager.translate("portal.commandFailed", "Display command failed. Check the PORTAL screen before trying again.") : ""
        }
        Text {
            Layout.fillWidth: true
            color: Theme.textSecondaryColor
            font: Theme.captionFont
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
