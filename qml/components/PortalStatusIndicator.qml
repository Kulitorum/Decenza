import QtQuick
import QtQuick.Controls
import Decenza

Item {
    id: root
    readonly property bool connected: BelkaPortal.hasReading
    readonly property bool pending: BelkaPortal.state === "connecting" || BelkaPortal.state === "discovering"
        || BelkaPortal.state === "waiting" || BelkaPortal.state === "disconnecting"
    readonly property bool stale: BelkaPortal.state === "stale"
    readonly property bool canReconnect: BelkaPortal.savedAddress.length > 0
        && !BelkaPortal.active && !pending && !BelkaPortal.machineBusy
    readonly property string ecText: "EC " + BelkaPortal.ecRaw.toFixed(3)
    readonly property string statusText: connected ? ecText
        : pending ? "PORTAL…"
        : stale ? TranslationManager.translate("portal.statusStale", "PORTAL stale")
        : TranslationManager.translate("portal.statusOffline", "PORTAL offline")
    readonly property string statusDescription: connected
        ? TranslationManager.translate("portal.statusConnected", "PORTAL connected")
          + " · " + TranslationManager.translate("portal.ecRaw", "EC (raw)") + ": " + BelkaPortal.ecRaw.toFixed(3)
        : pending ? TranslationManager.translate("portal.statusPending", "PORTAL connection in progress")
        : stale ? TranslationManager.translate("portal.statusNoData", "PORTAL connected, no recent measurements")
        : BelkaPortal.machineBusy
          ? TranslationManager.translate("portal.statusBusy", "PORTAL offline. Reconnect when the machine is idle.")
          : TranslationManager.translate("portal.statusReconnect", "PORTAL offline. Tap to reconnect.")
    readonly property color statusColor: connected ? Theme.successColor
        : pending ? Theme.textSecondaryColor : stale ? Theme.warningColor : Theme.primaryContrastColor

    implicitWidth: content.implicitWidth + Theme.spacingMedium
    implicitHeight: Theme.touchTargetMin

    Rectangle {
        anchors.fill: parent
        radius: Theme.scaled(4)
        color: !root.connected && !root.pending && !root.stale ? Theme.errorColor : "transparent"
    }
    Row {
        id: content
        anchors.centerIn: parent
        spacing: Theme.spacingSmall
        ThemedIcon {
            anchors.verticalCenter: parent.verticalCenter
            source: "qrc:/icons/portal.svg"
            iconSize: Theme.scaled(18)
            color: root.statusColor
        }
        Text {
            text: root.statusText
            color: root.statusColor
            font: Theme.bodyFont
            Accessible.ignored: true
        }
    }
    AccessibleMouseArea {
        id: tapArea
        anchors.fill: parent
        hoverEnabled: true
        accessibleName: root.statusDescription
        accessibleRole: root.canReconnect ? Accessible.Button : Accessible.StaticText
        cursorShape: root.canReconnect ? Qt.PointingHandCursor : Qt.ArrowCursor
        onAccessibleClicked: { if (root.canReconnect) BelkaPortal.reconnect() }
    }
    ToolTip.visible: tapArea.containsMouse
    ToolTip.text: statusDescription
    ToolTip.delay: 500
}
