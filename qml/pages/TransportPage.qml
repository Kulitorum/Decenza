import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decenza
import "../components"

Page {
    id: transportPage
    // Declarative so it re-evaluates on a language change (see DescalingPage).
    readonly property string pageTitle: TranslationManager.translate("transport.title", "Transport Mode")

    objectName: "transportPage"
    background: ThemedPageBackground {}

    property bool isPurging: MachineState.phase === MachineStateType.Phase.Transport
    property bool wasPurging: false
    property bool showComplete: false
    // Set when the user aborts via STOP, so the end-of-purge handler does NOT
    // claim the machine is empty (an aborted drain may leave water inside).
    property bool userStopped: false

    // Ready gate: current firmware (1333/1352) can silently drop an AirPurge
    // request while the machine is still heating on GHC hardware, so only allow
    // the drain to start once the machine has reached ready temperature. In
    // simulation there is no firmware limitation, so the gate is bypassed for
    // testing. (Cold-machine starts are handled by a separate, on-hold change.)
    readonly property bool machineReady: DE1Device.simulationMode ||
                                         MachineState.phase === MachineStateType.Phase.Ready

    onIsPurgingChanged: {
        if (isPurging) wasPurging = true
    }

    // If the page is created while a drain is already running (auto-nav on the
    // Transport phase, or reopened mid-drain), onIsPurgingChanged won't fire for
    // the initial binding value, so latch it here too — otherwise the end-of-
    // drain handler would early-return on !wasPurging and skip the confirmation.
    Component.onCompleted: {
        if (isPurging) wasPurging = true
    }

    // When the drain ends, decide what to show. We only reach here after the
    // machine actually entered the Transport phase (wasPurging), so a request
    // the firmware refused outright never produces a false "complete".
    Connections {
        target: MachineState
        function onPhaseChanged() {
            if (!wasPurging || isPurging || !transportPage.visible)
                return
            // Only a settled landing means the drain actually ran to a stop:
            // Idle/Ready normally, or Heating if the machine briefly reheats
            // afterward. A lost connection (updatePhase() forces Disconnected on
            // a BLE drop) or Sleep is NOT completion — the steam/espresso paths
            // in machinestate.cpp warn that a dropped link must not be treated
            // as a completion event. Whitelisting avoids that trap.
            var phase = MachineState.phase
            if (phase !== MachineStateType.Phase.Idle &&
                phase !== MachineStateType.Phase.Ready &&
                phase !== MachineStateType.Phase.Heating)
                return
            wasPurging = false
            if (userStopped) {
                // User aborted via the in-app STOP — return to the prepare view.
                userStopped = false
                showComplete = false
                return
            }
            // The drain left the Transport phase for a settled state. We cannot
            // positively confirm the tank is empty — a physical GHC stop lands
            // here too, indistinguishable from a natural end — so the completion
            // view gives conditional guidance rather than asserting emptiness.
            showComplete = true
        }
    }

    ScrollView {
        anchors.fill: parent
        anchors.margins: Theme.standardMargin
        anchors.topMargin: Theme.pageTopMargin
        anchors.bottomMargin: Theme.scaled(80)
        contentWidth: availableWidth
        clip: true

        ColumnLayout {
            width: parent.width
            spacing: Theme.scaled(16)

            // === DRAINING IN PROGRESS VIEW ===
            Item {
                visible: isPurging
                Layout.fillWidth: true
                Layout.fillHeight: true

                ColumnLayout {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: Math.min(Theme.scaled(500), parent.width - Theme.scaled(40))
                    spacing: Theme.scaled(20)

                    Item { Layout.preferredHeight: Theme.scaled(40) }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: Theme.scaled(200)
                        color: Theme.cardBackgroundColor
                        radius: Theme.cardRadius

                        ColumnLayout {
                            anchors.centerIn: parent
                            spacing: Theme.scaled(16)

                            Image {
                                Layout.alignment: Qt.AlignHCenter
                                source: Theme.emojiToImage("🧳")
                                sourceSize.width: Theme.scaled(48)
                                sourceSize.height: Theme.scaled(48)
                                Accessible.ignored: true
                            }

                            Tr {
                                Layout.alignment: Qt.AlignHCenter
                                key: "transport.inprogress.title"
                                fallback: "Removing water from your machine"
                                font: Theme.titleFont
                                color: Theme.textColor
                            }

                            BusyIndicator {
                                Layout.alignment: Qt.AlignHCenter
                                running: isPurging
                                Accessible.ignored: true
                            }
                        }
                    }

                    Tr {
                        Layout.alignment: Qt.AlignHCenter
                        Layout.fillWidth: true
                        key: "transport.inprogress.dontstop"
                        fallback: "Do not power off until draining completes"
                        font: Theme.captionFont
                        color: Theme.warningColor
                        wrapMode: Text.WordWrap
                        horizontalAlignment: Text.AlignHCenter
                    }

                    Item { Layout.fillHeight: true }

                    // Stop button (emergency only, for headless machines)
                    Rectangle {
                        id: purgeStopButton
                        Layout.alignment: Qt.AlignHCenter
                        Layout.preferredWidth: Theme.scaled(200)
                        Layout.preferredHeight: Theme.scaled(50)
                        visible: DE1Device.isHeadless
                        radius: Theme.cardRadius
                        color: stopTapHandler.isPressed ? Qt.darker(Theme.errorColor, 1.2) : Theme.errorColor
                        border.color: Theme.primaryContrastColor
                        border.width: Theme.scaled(2)

                        Text {
                            anchors.centerIn: parent
                            text: TranslationManager.translate("transport.button.stop", "STOP")
                            color: Theme.primaryContrastColor
                            font.pixelSize: Theme.scaled(18)
                            font.weight: Font.Bold
                            Accessible.ignored: true
                        }

                        AccessibleTapHandler {
                            id: stopTapHandler
                            anchors.fill: parent
                            accessibleName: TranslationManager.translate("transport.accessible.emergencyStop", "Emergency stop draining")
                            accessibleItem: purgeStopButton
                            onAccessibleClicked: {
                                userStopped = true
                                DE1Device.stopOperation()
                            }
                        }
                    }

                    Item { Layout.preferredHeight: Theme.scaled(20) }
                }
            }

            // === COMPLETE VIEW ===
            ColumnLayout {
                visible: showComplete && !isPurging
                Layout.fillWidth: true
                spacing: Theme.scaled(12)

                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: completeContent.implicitHeight + Theme.scaled(32)
                    color: Theme.cardBackgroundColor
                    radius: Theme.cardRadius

                    ColumnLayout {
                        id: completeContent
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: Theme.scaled(16)
                        spacing: Theme.scaled(12)

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.scaled(8)

                            Image {
                                source: Theme.emojiToImage("✅")
                                sourceSize.width: Theme.scaled(24)
                                sourceSize.height: Theme.scaled(24)
                                Accessible.ignored: true
                            }

                            Tr {
                                key: "transport.complete.title"
                                fallback: "Draining finished"
                                font: Theme.titleFont
                                color: Theme.primaryColor
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            height: Theme.scaled(1)
                            color: Theme.textSecondaryColor
                            opacity: 0.3
                        }

                        Tr {
                            Layout.fillWidth: true
                            key: "transport.complete.desc"
                            fallback: "If the machine ran until no more water came out, it is empty and ready to power off for storage or transport. If it was stopped early, it may still hold water — run Transport Mode again to finish draining."
                            font: Theme.bodyFont
                            color: Theme.textSecondaryColor
                            wrapMode: Text.WordWrap
                            horizontalAlignment: Text.AlignHCenter
                        }
                    }
                }

                AccessibleButton {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: Theme.scaled(200)
                    Layout.preferredHeight: Theme.scaled(50)
                    primary: true
                    text: TranslationManager.translate("common.button.done", "Done")
                    accessibleName: TranslationManager.translate("common.button.done", "Done")
                    _customFontSize: Theme.scaled(18)
                    _customFontWeight: Font.Bold
                    onClicked: {
                        showComplete = false
                        root.goToIdle()
                    }
                }
            }

            // === PREPARATION VIEW ===
            ColumnLayout {
                visible: !isPurging && !showComplete
                Layout.fillWidth: true
                spacing: Theme.scaled(12)

                // Intro card
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: introContent.implicitHeight + Theme.scaled(24)
                    color: Theme.cardBackgroundColor
                    radius: Theme.cardRadius

                    ColumnLayout {
                        id: introContent
                        anchors.fill: parent
                        anchors.margins: Theme.scaled(12)
                        spacing: Theme.scaled(8)

                        Tr {
                            key: "transport.intro.title"
                            fallback: "Prepare your machine for transport"
                            font: Theme.subtitleFont
                            color: Theme.textColor
                        }

                        Tr {
                            Layout.fillWidth: true
                            key: "transport.intro.desc"
                            fallback: "Transport Mode drains all water from inside the machine so it can be safely powered off, stored, or shipped — for example before a vacation or if the machine may be exposed to freezing temperatures."
                            font: Theme.bodyFont
                            color: Theme.textSecondaryColor
                            wrapMode: Text.WordWrap
                        }
                    }
                }

                // Setup steps
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: stepsContent.implicitHeight + Theme.scaled(24)
                    color: Theme.cardBackgroundColor
                    radius: Theme.cardRadius

                    ColumnLayout {
                        id: stepsContent
                        anchors.fill: parent
                        anchors.margins: Theme.scaled(12)
                        spacing: Theme.scaled(8)

                        Tr {
                            key: "transport.steps.title"
                            fallback: "Steps"
                            font: Theme.subtitleFont
                            color: Theme.textColor
                        }

                        Tr {
                            Layout.fillWidth: true
                            key: "transport.steps.1"
                            fallback: "1. Remove the drip tray cover (keep the tray in place to catch water)."
                            font: Theme.bodyFont
                            color: Theme.textColor
                            wrapMode: Text.WordWrap
                        }

                        Tr {
                            Layout.fillWidth: true
                            key: "transport.steps.2"
                            fallback: "2. Lower the steam wand over the drip tray."
                            font: Theme.bodyFont
                            color: Theme.textColor
                            wrapMode: Text.WordWrap
                        }

                        Tr {
                            Layout.fillWidth: true
                            key: "transport.steps.3"
                            fallback: "3. When you press Start, pull the water tank forward so the machine draws air instead of water."
                            font: Theme.bodyFont
                            color: Theme.textColor
                            wrapMode: Text.WordWrap
                        }

                        Tr {
                            Layout.fillWidth: true
                            key: "transport.steps.4"
                            fallback: "4. The machine pumps until it is empty, then stops on its own. Power it off once it reports it is dry."
                            font: Theme.bodyFont
                            color: Theme.textColor
                            wrapMode: Text.WordWrap
                        }
                    }
                }

                // Not-ready hint (shown until the machine has warmed up)
                Rectangle {
                    Layout.fillWidth: true
                    visible: !machineReady
                    Layout.preferredHeight: notReadyContent.implicitHeight + Theme.scaled(24)
                    color: Qt.rgba(Theme.warningColor.r, Theme.warningColor.g, Theme.warningColor.b, 0.15)
                    radius: Theme.cardRadius
                    border.color: Theme.warningColor
                    border.width: 1

                    ColumnLayout {
                        id: notReadyContent
                        anchors.fill: parent
                        anchors.margins: Theme.scaled(12)
                        spacing: Theme.scaled(4)

                        Tr {
                            Layout.fillWidth: true
                            key: "transport.notready"
                            fallback: "Wake the machine and wait until it has warmed up to ready temperature before starting. On current firmware the drain request is ignored while the machine is still heating."
                            font: Theme.bodyFont
                            color: Theme.warningColor
                            wrapMode: Text.WordWrap
                        }
                    }
                }

                // Start button — gated on the machine being ready
                AccessibleButton {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.topMargin: Theme.scaled(8)
                    Layout.preferredWidth: Theme.scaled(250)
                    Layout.preferredHeight: Theme.scaled(56)
                    primary: true
                    enabled: machineReady
                    text: TranslationManager.translate("transport.button.start", "Start Transport Mode")
                    accessibleName: TranslationManager.translate("transport.button.start", "Start Transport Mode")
                    _customFontSize: Theme.scaled(20)
                    _customFontWeight: Font.Bold
                    onClicked: {
                        userStopped = false
                        showComplete = false
                        DE1Device.startAirPurge()
                    }
                }

                Item { Layout.preferredHeight: Theme.scaled(20) }
            }
        }
    }

    // Bottom bar
    BottomBar {
        visible: !isPurging
        title: transportPage.pageTitle
        onBackClicked: {
            showComplete = false
            root.goBack()
        }
    }
}
