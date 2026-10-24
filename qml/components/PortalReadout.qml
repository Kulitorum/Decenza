pragma ComponentBehavior: Bound
import QtQuick
import Decenza

Rectangle {
    id: readout
    implicitHeight: valueText.implicitHeight + Theme.scaled(14)
    color: Theme.cardBackgroundColor
    radius: Theme.cardRadius
    // The settings panel receives PORTAL state through the host-owned
    // Connections service. Other existing native/remote readouts keep using
    // the established BelkaPortal facade.
    property var portalState: null
    readonly property string sourceState: portalState ? portalState.state : BelkaPortal.state
    readonly property bool sourceHasReading: portalState ? portalState.hasReading : BelkaPortal.hasReading
    readonly property real sourceEcRaw: portalState ? portalState.ecRaw : BelkaPortal.ecRaw
    readonly property real sourceTemperatureC: portalState ? portalState.temperatureC : BelkaPortal.temperatureC
    readonly property string statusText: {
        switch (sourceState) {
        case "connecting": return TranslationManager.translate("portal.connecting", "Connecting")
        case "discovering": return TranslationManager.translate("portal.discovering", "Checking PORTAL")
        case "waiting": return TranslationManager.translate("portal.waiting", "Waiting for measurements")
        case "streaming": return TranslationManager.translate("portal.streaming", "Receiving measurements")
        case "stale": return TranslationManager.translate("portal.stale", "No current measurements")
        case "error": return TranslationManager.translate("portal.error", "Connection failed")
        case "disconnecting": return TranslationManager.translate("portal.disconnecting", "Disconnecting")
        default: return TranslationManager.translate("portal.disconnected", "Disconnected")
        }
    }
    Text {
        id: valueText
        anchors.centerIn: parent
        width: parent.width - Theme.scaled(20)
        wrapMode: Text.Wrap
        horizontalAlignment: Text.AlignHCenter
        color: Theme.textColor
        font: Theme.bodyFont
        text: readout.sourceHasReading
            ? "PORTAL · " + TranslationManager.translate("portal.ecRaw", "EC (raw)") + ": "
              + readout.sourceEcRaw.toFixed(3) + " · " + Theme.cToDisplay(readout.sourceTemperatureC).toFixed(1) + " " + Theme.tempUnitSuffix()
            : "PORTAL · " + readout.statusText
        Accessible.role: Accessible.StaticText
        Accessible.name: text
    }
}
