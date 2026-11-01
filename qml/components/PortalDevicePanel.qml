pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Decenza

ColumnLayout {
    id: panel
    spacing: Theme.scaled(8)
    property var requestAction: function(operation, args) {}
    readonly property var portalState: SettingsConnectionsService.snapshot.portal || ({
        savedName: "", active: false, owned: false, state: "disconnected", machineBusy: false,
        syncDisplay: false, mountingMode: "onScale", errorMessage: "", displayCommandStatus: "",
        canControlDisplay: false, packetCount: 0, lastPacket: "", hasReading: false, ecRaw: 0, temperatureC: 0
    })
    readonly property var calibration: SettingsConnectionsService.portalCalibration

    Text {
        Layout.fillWidth: true
        text: "Belka PORTAL" + (panel.portalState.savedName.length ? " · " + panel.portalState.savedName : "")
        color: Theme.textColor
        font: Theme.subtitleFont
        wrapMode: Text.Wrap
    }
    PortalReadout { Layout.fillWidth: true; portalState: panel.portalState }
    RowLayout {
        Layout.fillWidth: true
        AccessibleButton {
            text: panel.portalState.active
                ? TranslationManager.translate("portal.disconnect", "Disconnect")
                : TranslationManager.translate("portal.connect", "Connect")
            accessibleName: text + " PORTAL"
            enabled: !panel.portalState.machineBusy && panel.portalState.state !== "disconnecting"
                && (panel.portalState.active || panel.portalState.owned)
            onClicked: panel.requestAction(
                panel.portalState.active ? "disconnectPortal" : "reconnectPortal", {})
        }
        AccessibleButton {
            text: TranslationManager.translate("portal.forget", "Forget")
            accessibleName: text + " PORTAL"
            enabled: !panel.portalState.machineBusy && panel.portalState.state !== "disconnecting"
                && (panel.portalState.active || panel.portalState.owned)
            onClicked: panel.requestAction("forgetPortal", {})
        }
        Item { Layout.fillWidth: true }
    }
    Text {
        Layout.fillWidth: true
        visible: !panel.portalState.owned
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
            checked: panel.portalState.syncDisplay
            enabled: !panel.portalState.machineBusy
            onToggled: panel.requestAction("setPortalSyncDisplay", { enabled: checked })
            accessibleName: syncLabel.text
        }
    }
    ColumnLayout {
        Layout.fillWidth: true
        spacing: Theme.scaled(4)
        Text {
            Layout.fillWidth: true
            text: TranslationManager.translate("portal.mountingMode", "Mounting geometry")
            color: Theme.textColor
            font: Theme.bodyFont
            wrapMode: Text.Wrap
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.scaled(6)
            AccessibleButton {
                text: TranslationManager.translate("portal.mountingOnScale", "On cup / scale (tare before shot)")
                accessibleName: text
                primary: (panel.portalState.mountingMode || "onScale") !== "independent"
                enabled: !panel.portalState.machineBusy
                onClicked: panel.requestAction("setPortalMountingMode", { mode: "onScale" })
            }
            AccessibleButton {
                text: TranslationManager.translate("portal.mountingIndependent", "Independent stand")
                accessibleName: text
                primary: panel.portalState.mountingMode === "independent"
                enabled: !panel.portalState.machineBusy
                onClicked: panel.requestAction("setPortalMountingMode", { mode: "independent" })
            }
            Item { Layout.fillWidth: true }
        }
    }
    ColumnLayout {
        Layout.fillWidth: true
        spacing: Theme.scaled(4)
        Text {
            Layout.fillWidth: true
            text: TranslationManager.translate("portal.calibration.title", "Refractometer calibration")
            color: Theme.textColor
            font: Theme.subtitleFont
            wrapMode: Text.Wrap
        }
        Text {
            Layout.fillWidth: true
            text: panel.calibration.loading
                ? TranslationManager.translate("portal.calibration.loading", "Updating calibration…")
                : panel.calibration.error
                  ? TranslationManager.translate("portal.calibration.error", "Calibration unavailable. Reopen Settings to retry.")
                  : panel.calibration.calibrated
                    ? "k = " + Number(panel.calibration.proportionalK).toFixed(4)
                      + " · " + (panel.calibration.coffee || "")
                    : TranslationManager.translate("portal.calibration.noReferences", "No suitable paired EC / TDS measurements for this coffee and mounting mode.")
            color: Theme.textColor
            font: Theme.bodyFont
            wrapMode: Text.Wrap
        }
        Text {
            Layout.fillWidth: true
            visible: Boolean(panel.calibration.calibrated)
            text: TranslationManager.translate("portal.calibration.formula", "TDS% ≈ k × mass-weighted EC; EY% ≈ TDS% × beverage / dose.")
                  + (panel.calibration.activeModel === "affine"
                     ? "\nTDS% ≈ " + Number(panel.calibration.affineSlopeA).toFixed(4) + " × EC "
                       + (panel.calibration.affineInterceptB >= 0 ? "+ " : "− ")
                       + Math.abs(panel.calibration.affineInterceptB).toFixed(2)
                       + " · " + TranslationManager.translate("portal.calibration.affine", "Active fit includes an offset; k above is the proportional comparison.") : "")
            color: Theme.textSecondaryColor
            wrapMode: Text.Wrap
        }
        Text {
            Layout.fillWidth: true
            visible: Boolean(panel.calibration.calibrated)
            text: TranslationManager.translate("portal.calibration.counts", "%1 included / %2 excluded")
                    .arg(panel.calibration.shotCount || 0).arg(panel.calibration.excludedShotCount || 0)
                  + ((panel.calibration.shotCount || 0) < 5
                     ? " · " + TranslationManager.translate("portal.calibration.preliminary", "Preliminary: fewer than five reference shots") : "")
                  + ((panel.calibration.shotCount || 0) >= 2
                     ? "\n" + TranslationManager.translate("portal.calibration.errorTds", "Leave-one-out error: %1 TDS percentage points")
                         .arg(Number(panel.calibration.activeModel === "affine"
                                     ? panel.calibration.affineLoocvMaeTdsPct
                                     : panel.calibration.proportionalLoocvMaeTdsPct).toFixed(2)) : "")
                  + "\n" + TranslationManager.translate("portal.calibration.range", "Measured ratio range: 1:%1–1:%2")
                      .arg(Number(panel.calibration.minRatio || 0).toFixed(2))
                      .arg(Number(panel.calibration.maxRatio || 0).toFixed(2))
            color: Theme.textSecondaryColor
            wrapMode: Text.Wrap
        }
        Text {
            Layout.fillWidth: true
            text: TranslationManager.translate("portal.calibration.help", "Save refractometer TDS on a shot in History or Shot Review. The fit updates automatically. Slow wetting, EC dips, missing data and high-temperature warnings exclude a shot. Use the same PORTAL, water and coffee; a new coffee needs its own references.")
            color: Theme.textSecondaryColor
            wrapMode: Text.Wrap
        }
        AccessibleButton {
            id: referencesToggle
            property bool expanded: false
            visible: (panel.calibration.referenceShots || []).length > 0
            text: TranslationManager.translate("portal.calibration.references", "Calibration shots") + (expanded ? " −" : " +")
            accessibleName: text
            onClicked: expanded = !expanded
        }
        Repeater {
            model: referencesToggle.expanded ? (panel.calibration.referenceShots || []).slice(0, 20) : []
            delegate: Text {
                required property var modelData
                Layout.fillWidth: true
                text: String(modelData.dateTime).replace("T", " ").slice(0, 16)
                      + " · 1:" + Number(modelData.ratio).toFixed(2)
                      + " · TDS " + Number(modelData.measuredTdsPct).toFixed(2) + "%"
                      + " · " + (modelData.meanRawEc > 0
                          ? "k " + Number(modelData.individualK).toFixed(4)
                          : TranslationManager.translate("portal.calibration.noEc", "EC unavailable"))
                      + " · " + (modelData.included
                          ? TranslationManager.translate("portal.calibration.included", "Included")
                          : modelData.exclusionReason === "mounting_mismatch"
                            ? TranslationManager.translate("portal.calibration.mountMismatch", "Excluded: different or unknown mounting")
                            : TranslationManager.translate("portal.calibration.excluded", "Excluded: measurement warnings / invalid data; see shot review"))
                color: modelData.included ? Theme.textSecondaryColor : Theme.warningColor
                font: Theme.captionFont
                wrapMode: Text.Wrap
            }
        }
    }
    Text {
        Layout.fillWidth: true
        visible: panel.portalState.errorMessage.length > 0
        text: TranslationManager.translate("portal.connectionProblem", "PORTAL connection or measurement problem. Reconnect while the machine is idle; details are available below.")
        color: Theme.warningColor
        wrapMode: Text.Wrap
    }
    Text {
        Layout.fillWidth: true
        visible: panel.portalState.displayCommandStatus === "failed"
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
            visible: panel.portalState.errorMessage.length > 0
            // The raw message is diagnostic text from the transport, not a
            // translated string, so it is labelled rather than presented as the UI's own.
            text: TranslationManager.translate("portal.lastError", "Last error") + ": " + panel.portalState.errorMessage
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
                enabled: panel.portalState.canControlDisplay && !panel.portalState.machineBusy && panel.portalState.displayCommandStatus !== "requested"
                onClicked: panel.requestAction("setPortalGraphView", { show: true })
            }
            AccessibleButton {
                text: TranslationManager.translate("portal.hideGraph", "End device graph")
                accessibleName: text
                enabled: panel.portalState.canControlDisplay && !panel.portalState.machineBusy && panel.portalState.displayCommandStatus !== "requested"
                onClicked: panel.requestAction("setPortalGraphView", { show: false })
            }
        }
        Text {
            Layout.fillWidth: true
            color: Theme.textSecondaryColor
            wrapMode: Text.Wrap
            text: panel.portalState.displayCommandStatus === "acknowledged"
                ? TranslationManager.translate("portal.commandAck", "Display command delivered. Check the PORTAL screen.")
                : panel.portalState.displayCommandStatus === "requested"
                  ? TranslationManager.translate("portal.commandPending", "Sending display command…")
                  : panel.portalState.displayCommandStatus === "failed"
                    ? TranslationManager.translate("portal.commandFailed", "Display command failed. Check the PORTAL screen before trying again.") : ""
        }
        Text {
            Layout.fillWidth: true
            color: Theme.textSecondaryColor
            font: Theme.captionFont
            wrapMode: Text.WrapAnywhere
            text: TranslationManager.translate("portal.packets", "Received packets") + ": "
                + panel.portalState.packetCount + "\n" + panel.portalState.lastPacket
        }
    }
    Rectangle {
        Layout.fillWidth: true
        implicitHeight: 1
        color: Theme.textSecondaryColor
        opacity: 0.3
    }
}
