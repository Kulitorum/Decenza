import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decenza
import "../../components"

// Provisioning wizard for the Decenza Scale's Wi-Fi connection. Three
// pages stacked behind a single Dialog frame: scale picker → credentials →
// status. The status page also serves as the success terminus so the user
// sees the confirmed IP before tapping Done.
//
// Wiring lives entirely in the Decenza-side AI's coordination — the BLE
// state machine is handled by C++ DecenzaWifiManager, which exposes a
// single phase string and a couple of forwarded signals.
Dialog {
    id: root
    modal: true
    anchors.centerIn: parent
    // CloseOnPressOutside is included so a misplaced tap doesn't trap the
    // user when no Cancel button is visible (e.g. step 2 mid-write); the
    // wizard never holds unsaved sensitive state worth defending against
    // accidental dismissal — the wizard's data is recreated on every
    // open, and we explicitly clearPhase() in onClosed.
    closePolicy: Dialog.CloseOnEscape | Dialog.CloseOnPressOutside
    width: Math.min(parent.width * 0.9, Theme.scaled(520))
    padding: 0

    // Wizard step: 0 = pick scale, 1 = enter credentials, 2 = status.
    property int step: 0
    property string selectedAddress: ""
    property string selectedName: ""

    background: Rectangle {
        color: Theme.surfaceColor
        radius: Theme.cardRadius
        border.color: Theme.primaryContrastColor
        border.width: 1
    }

    onOpened: {
        root.step = 0
        root.selectedAddress = ""
        root.selectedName = ""
        ssidField.text = ""
        passField.text = ""
        DecenzaWifiManager.clearPhase()
        // Kick a scan so the user sees nearby Decenza scales without an
        // extra tap. Filtering to "decenza" happens in the delegate.
        BLEManager.scanForDevices()
    }

    onClosed: {
        DecenzaWifiManager.clearPhase()
    }

    // React to terminal phases without forcing the user off the status page.
    Connections {
        target: DecenzaWifiManager
        function onProvisioningCompleted(mac, ip) { /* handled by phase prop */ }
        function onProvisioningFailed(reason) { /* handled by phase prop */ }
    }

    contentItem: ColumnLayout {
        spacing: 0
        Layout.preferredWidth: root.width

        // Title bar
        Text {
            Layout.fillWidth: true
            Layout.topMargin: Theme.scaled(20)
            Layout.bottomMargin: Theme.scaled(10)
            text: TranslationManager.translate("decenzaWifi.title", "Set up Decenza Scale Wi-Fi")
            color: Theme.textColor
            font.pixelSize: Theme.scaled(18)
            font.bold: true
            horizontalAlignment: Text.AlignHCenter
        }

        // Step indicator
        Text {
            Layout.fillWidth: true
            Layout.bottomMargin: Theme.scaled(10)
            text: {
                if (root.step === 0) return TranslationManager.translate("decenzaWifi.step1", "Step 1 of 3 — Pick scale")
                if (root.step === 1) return TranslationManager.translate("decenzaWifi.step2", "Step 2 of 3 — Wi-Fi credentials")
                return TranslationManager.translate("decenzaWifi.step3", "Step 3 of 3 — Connecting")
            }
            color: Theme.textSecondaryColor
            font.pixelSize: Theme.scaled(12)
            horizontalAlignment: Text.AlignHCenter
        }

        // Step 0 — scale picker
        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.scaled(20)
            Layout.rightMargin: Theme.scaled(20)
            visible: root.step === 0
            spacing: Theme.scaled(8)

            Text {
                Layout.fillWidth: true
                text: TranslationManager.translate("decenzaWifi.pickPrompt",
                    "Pick the Decenza Scale you want to provision. Make sure it is powered on and within Bluetooth range.")
                color: Theme.textColor
                wrapMode: Text.Wrap
                font.pixelSize: Theme.scaled(13)
            }

            // Filtered list — show only Decenza scales. The currently
            // connected scale is *always* surfaced first, even if a
            // fresh BLE scan hasn't re-discovered it yet (it usually
            // won't, because the OS doesn't re-advertise a peripheral
            // that's already in an active connection). Anything else
            // the scan turns up is appended after.
            ListView {
                id: scaleListView
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.scaled(180)
                clip: true
                model: {
                    var items = []
                    var seenAddresses = {}

                    // 1. Currently connected scale, if it's Decenza-typed.
                    if (ScaleDevice && ScaleDevice.connected
                        && ScaleDevice.name
                        && ScaleDevice.name.toLowerCase().indexOf("decenza") >= 0
                        && Settings.scaleAddress) {
                        items.push({
                            name: ScaleDevice.name,
                            address: Settings.scaleAddress,
                            isConnected: true
                        })
                        seenAddresses[Settings.scaleAddress.toLowerCase()] = true
                    }

                    // 2. Anything fresh from the BLE scan, deduped.
                    var scales = BLEManager.discoveredScales
                    for (var i = 0; i < scales.length; i++) {
                        if (!scales[i].name) continue
                        if (scales[i].name.toLowerCase().indexOf("decenza") < 0) continue
                        if (seenAddresses[scales[i].address.toLowerCase()]) continue
                        items.push({
                            name: scales[i].name,
                            address: scales[i].address,
                            isConnected: false
                        })
                    }
                    return items
                }
                delegate: ItemDelegate {
                    width: ListView.view.width

                    Accessible.role: Accessible.Button
                    Accessible.name: modelData.name + ", " + modelData.address
                    Accessible.focusable: true
                    Accessible.onPressAction: pickRow.clicked(null)

                    contentItem: RowLayout {
                        id: pickRow
                        Text {
                            text: modelData.name
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(13)
                            Layout.fillWidth: true
                        }
                        // Small "connected" pill so the user knows that
                        // this entry came from the active BLE link rather
                        // than a scan match.
                        Rectangle {
                            visible: modelData.isConnected === true
                            width: connectedPill.implicitWidth + Theme.scaled(10)
                            height: Theme.scaled(18)
                            radius: Theme.scaled(9)
                            color: Qt.rgba(0.3, 0.7, 0.4, 0.25)
                            Text {
                                id: connectedPill
                                anchors.centerIn: parent
                                text: TranslationManager.translate("decenzaWifi.connectedPill", "Connected")
                                color: "#3aa055"
                                font.pixelSize: Theme.scaled(10)
                                font.bold: true
                            }
                        }
                        Text {
                            text: modelData.address
                            color: Theme.textSecondaryColor
                            font.pixelSize: Theme.scaled(11)
                        }

                        function clicked() {
                            root.selectedAddress = modelData.address
                            root.selectedName = modelData.name
                            root.step = 1
                        }
                    }
                    background: Rectangle {
                        color: parent.hovered ? Qt.rgba(Theme.accentColor.r, Theme.accentColor.g, Theme.accentColor.b, 0.18) : "transparent"
                        radius: Theme.scaled(4)
                    }
                    onClicked: pickRow.clicked()
                }

                Tr {
                    anchors.centerIn: parent
                    visible: scaleListView.count === 0
                    key: "decenzaWifi.noScales"
                    fallback: "No Decenza scales found. Make sure the scale is on, then tap Scan again."
                    color: Theme.textSecondaryColor
                    wrapMode: Text.Wrap
                    width: scaleListView.width - Theme.scaled(20)
                    horizontalAlignment: Text.AlignHCenter
                }
            }

            // Cancel + Scan-again row — Cancel is *always* enabled so the
            // user can back out even when the scan is in progress and no
            // scales were found (e.g. the scale is across the room).
            // Without an explicit close affordance the dialog would trap
            // the user on a tablet that has no hardware Escape key.
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: Theme.scaled(4)
                Button {
                    text: TranslationManager.translate("common.button.cancel", "Cancel")
                    onClicked: root.close()
                }
                Item { Layout.fillWidth: true }
                Button {
                    text: BLEManager.scanning
                        ? TranslationManager.translate("decenzaWifi.scanning", "Scanning…")
                        : TranslationManager.translate("decenzaWifi.scanAgain", "Scan again")
                    enabled: !BLEManager.scanning
                    onClicked: BLEManager.scanForDevices()
                }
            }
        }

        // Step 1 — credentials
        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.scaled(20)
            Layout.rightMargin: Theme.scaled(20)
            visible: root.step === 1
            spacing: Theme.scaled(10)

            Text {
                Layout.fillWidth: true
                text: TranslationManager.translate("decenzaWifi.credPrompt",
                    "Enter the Wi-Fi network the scale should join. The credentials are stored on the scale only.")
                color: Theme.textColor
                wrapMode: Text.Wrap
                font.pixelSize: Theme.scaled(13)
            }

            // Selected scale summary
            Rectangle {
                Layout.fillWidth: true
                color: Qt.rgba(Theme.accentColor.r, Theme.accentColor.g, Theme.accentColor.b, 0.12)
                radius: Theme.scaled(4)
                height: Theme.scaled(36)
                Text {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.scaled(10)
                    verticalAlignment: Text.AlignVCenter
                    text: root.selectedName + "  ·  " + root.selectedAddress
                    color: Theme.textColor
                    font.pixelSize: Theme.scaled(12)
                }
            }

            StyledTextField {
                id: ssidField
                Layout.fillWidth: true
                placeholderText: TranslationManager.translate("decenzaWifi.ssidPlaceholder", "Wi-Fi network name (SSID)")
                Accessible.role: Accessible.EditableText
                Accessible.name: TranslationManager.translate("decenzaWifi.ssid", "Wi-Fi network name")
            }

            StyledTextField {
                id: passField
                Layout.fillWidth: true
                placeholderText: TranslationManager.translate("decenzaWifi.passPlaceholder", "Wi-Fi password")
                echoMode: showPassCheckbox.checked ? TextInput.Normal : TextInput.Password
                Accessible.role: Accessible.EditableText
                Accessible.name: TranslationManager.translate("decenzaWifi.pass", "Wi-Fi password")
            }

            CheckBox {
                id: showPassCheckbox
                text: TranslationManager.translate("decenzaWifi.showPass", "Show password")
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: Theme.scaled(8)
                Button {
                    text: TranslationManager.translate("common.button.back", "Back")
                    onClicked: root.step = 0
                }
                Item { Layout.fillWidth: true }
                Button {
                    text: TranslationManager.translate("decenzaWifi.connect", "Connect")
                    enabled: ssidField.text.length > 0
                    onClicked: {
                        Qt.inputMethod.commit()
                        root.step = 2
                        DecenzaWifiManager.provisionWifi(root.selectedAddress,
                                                        ssidField.text,
                                                        passField.text)
                    }
                }
            }
        }

        // Step 2 — status
        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.scaled(20)
            Layout.rightMargin: Theme.scaled(20)
            visible: root.step === 2
            spacing: Theme.scaled(12)

            Text {
                Layout.fillWidth: true
                text: {
                    var phase = DecenzaWifiManager.provisionPhase
                    if (phase === "bleConnecting") return TranslationManager.translate("decenzaWifi.bleConnecting", "Connecting to scale over Bluetooth…")
                    if (phase === "writing") return TranslationManager.translate("decenzaWifi.writing", "Sending Wi-Fi credentials to the scale…")
                    if (phase === "wifiConnecting") return TranslationManager.translate("decenzaWifi.wifiConnecting", "Scale is joining Wi-Fi…")
                    if (phase === "succeeded") return TranslationManager.translate("decenzaWifi.succeeded", "Wi-Fi connected — switching now")
                    if (phase === "failed") return TranslationManager.translate("decenzaWifi.failed", "Provisioning failed")
                    return TranslationManager.translate("decenzaWifi.starting", "Starting…")
                }
                color: Theme.textColor
                font.pixelSize: Theme.scaled(15)
                font.bold: true
                wrapMode: Text.Wrap
                horizontalAlignment: Text.AlignHCenter
            }

            BusyIndicator {
                Layout.alignment: Qt.AlignHCenter
                running: DecenzaWifiManager.provisionPhase !== "succeeded" && DecenzaWifiManager.provisionPhase !== "failed"
            }

            Text {
                Layout.fillWidth: true
                text: {
                    if (DecenzaWifiManager.provisionPhase === "succeeded") {
                        return TranslationManager.translate("decenzaWifi.successDetail",
                            "IP address: ") + DecenzaWifiManager.provisionIp
                    }
                    if (DecenzaWifiManager.provisionPhase === "failed") {
                        return DecenzaWifiManager.provisionMessage
                    }
                    return ""
                }
                color: DecenzaWifiManager.provisionPhase === "failed" ? "#cc4444" : Theme.textSecondaryColor
                font.pixelSize: Theme.scaled(13)
                wrapMode: Text.Wrap
                horizontalAlignment: Text.AlignHCenter
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: Theme.scaled(8)
                Button {
                    visible: DecenzaWifiManager.provisionPhase === "failed"
                    text: TranslationManager.translate("decenzaWifi.tryAgain", "Try again")
                    onClicked: {
                        DecenzaWifiManager.clearPhase()
                        root.step = 1
                    }
                }
                Item { Layout.fillWidth: true }
                Button {
                    text: DecenzaWifiManager.provisionPhase === "succeeded"
                        ? TranslationManager.translate("common.button.done", "Done")
                        : TranslationManager.translate("common.button.cancel", "Cancel")
                    enabled: DecenzaWifiManager.provisionPhase !== "writing"
                             && DecenzaWifiManager.provisionPhase !== "wifiConnecting"
                             && DecenzaWifiManager.provisionPhase !== "bleConnecting"
                    onClicked: root.close()
                }
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.scaled(20)
        }
    }
}
