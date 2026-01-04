import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DecenzaDE1
import "../../components"

Item {
    id: accessibilityTab

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.scaled(15)

        // Main accessibility settings card
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.scaled(380)
            color: Theme.surfaceColor
            radius: Theme.cardRadius

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: Theme.scaled(15)
                spacing: Theme.scaled(12)

                Tr {
                    key: "settings.accessibility.title"
                    fallback: "Accessibility"
                    color: Theme.textColor
                    font.pixelSize: Theme.scaled(16)
                    font.bold: true
                }

                Tr {
                    Layout.fillWidth: true
                    key: "settings.accessibility.desc"
                    fallback: "Screen reader support and audio feedback for blind and visually impaired users"
                    color: Theme.textSecondaryColor
                    font.pixelSize: Theme.scaled(12)
                    wrapMode: Text.WordWrap
                }

                // Enable toggle
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(15)

                    Tr {
                        key: "settings.accessibility.enable"
                        fallback: "Enable Accessibility"
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(14)
                    }

                    Item { Layout.fillWidth: true }

                    StyledSwitch {
                        checked: AccessibilityManager.enabled
                        onCheckedChanged: AccessibilityManager.enabled = checked
                    }
                }

                // TTS toggle
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(15)
                    opacity: AccessibilityManager.enabled ? 1.0 : 0.5

                    Tr {
                        key: "settings.accessibility.voiceAnnouncements"
                        fallback: "Voice Announcements"
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(14)
                    }

                    Item { Layout.fillWidth: true }

                    StyledSwitch {
                        checked: AccessibilityManager.ttsEnabled
                        enabled: AccessibilityManager.enabled
                        onCheckedChanged: {
                            if (AccessibilityManager.enabled) {
                                if (checked) {
                                    // Enable first, then announce
                                    AccessibilityManager.ttsEnabled = true
                                    AccessibilityManager.announce("Voice announcements enabled", true)
                                } else {
                                    // Announce first, then disable
                                    AccessibilityManager.announce("Voice announcements disabled", true)
                                    AccessibilityManager.ttsEnabled = false
                                }
                            } else {
                                AccessibilityManager.ttsEnabled = checked
                            }
                        }
                    }
                }

                // Tick sound toggle
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(15)
                    opacity: AccessibilityManager.enabled ? 1.0 : 0.5

                    ColumnLayout {
                        spacing: Theme.scaled(2)
                        Tr {
                            key: "settings.accessibility.frameTick"
                            fallback: "Frame Tick Sound"
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(14)
                        }
                        Tr {
                            key: "settings.accessibility.frameTickDesc"
                            fallback: "Play a tick when extraction frames change"
                            color: Theme.textSecondaryColor
                            font.pixelSize: Theme.scaled(11)
                        }
                    }

                    Item { Layout.fillWidth: true }

                    StyledSwitch {
                        checked: AccessibilityManager.tickEnabled
                        enabled: AccessibilityManager.enabled
                        onCheckedChanged: {
                            AccessibilityManager.tickEnabled = checked
                            if (AccessibilityManager.enabled) {
                                AccessibilityManager.announce(checked ? "Frame tick sound enabled" : "Frame tick sound disabled", true)
                            }
                        }
                    }
                }

                // Tick sound picker and volume
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(15)
                    opacity: (AccessibilityManager.enabled && AccessibilityManager.tickEnabled) ? 1.0 : 0.5

                    Tr {
                        key: "settings.accessibility.tickSound"
                        fallback: "Tick Sound"
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(14)
                    }

                    Item { Layout.fillWidth: true }

                    ValueInput {
                        value: AccessibilityManager.tickSoundIndex
                        from: 1
                        to: 4
                        stepSize: 1
                        suffix: ""
                        displayText: "Sound " + value
                        accessibleName: "Select tick sound, 1 to 4. Current: " + value
                        enabled: AccessibilityManager.enabled && AccessibilityManager.tickEnabled
                        onValueModified: function(newValue) {
                            AccessibilityManager.tickSoundIndex = newValue
                        }
                    }

                    ValueInput {
                        value: AccessibilityManager.tickVolume
                        from: 10
                        to: 100
                        stepSize: 10
                        suffix: "%"
                        accessibleName: "Tick volume. Current: " + value + " percent"
                        enabled: AccessibilityManager.enabled && AccessibilityManager.tickEnabled
                        onValueModified: function(newValue) {
                            AccessibilityManager.tickVolume = newValue
                        }
                    }
                }

                Item { Layout.fillHeight: true }
            }
        }

        // Spacer
        Item { Layout.fillHeight: true }
    }
}
