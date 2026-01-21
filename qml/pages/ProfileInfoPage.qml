import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DecenzaDE1
import "../components"

Page {
    id: profileInfoPage
    objectName: "profileInfoPage"
    background: Rectangle { color: Theme.backgroundColor }

    // Profile to display
    property string profileFilename: ""
    property string profileName: ""

    // Loaded profile data
    property var profileData: null

    Component.onCompleted: {
        root.currentPageTitle = TranslationManager.translate("profileinfo.title", "Profile Info")
        loadProfile()
    }

    function loadProfile() {
        if (profileFilename) {
            profileData = MainController.getProfileByFilename(profileFilename)
            if (profileData && profileData.steps) {
                profileGraph.frames = profileData.steps
            } else {
                profileGraph.frames = []
            }
        }
    }

    ScrollView {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: bottomBar.top
        anchors.topMargin: Theme.pageTopMargin
        anchors.leftMargin: Theme.standardMargin
        anchors.rightMargin: Theme.standardMargin
        contentWidth: availableWidth

        ColumnLayout {
            width: parent.width
            spacing: Theme.spacingMedium

            // Header: Title and Author
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.scaled(4)

                Text {
                    text: profileData ? profileData.title : (profileName || "Profile")
                    font: Theme.titleFont
                    color: Theme.textColor
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }

                Text {
                    visible: profileData && profileData.author && profileData.author.length > 0
                    text: profileData ? TranslationManager.translate("profileinfo.by", "by") + " " + profileData.author : ""
                    font: Theme.labelFont
                    color: Theme.textSecondaryColor
                }
            }

            // Profile Graph Card
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.scaled(220)
                color: Theme.surfaceColor
                radius: Theme.cardRadius

                ProfileGraph {
                    id: profileGraph
                    anchors.fill: parent
                    anchors.margins: Theme.scaled(8)
                    frames: []
                    selectedFrameIndex: -1
                }

                // No data message
                Text {
                    anchors.centerIn: parent
                    visible: !profileData || !profileData.steps || profileData.steps.length === 0
                    text: TranslationManager.translate("profileinfo.noData", "No profile data")
                    font: Theme.bodyFont
                    color: Theme.textSecondaryColor
                }
            }

            // Settings Section
            Rectangle {
                Layout.fillWidth: true
                color: Theme.surfaceColor
                radius: Theme.cardRadius
                implicitHeight: settingsColumn.height + Theme.scaled(24)

                ColumnLayout {
                    id: settingsColumn
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: Theme.scaled(12)
                    spacing: Theme.scaled(8)

                    // Section header
                    Text {
                        text: TranslationManager.translate("profileinfo.settings", "Settings")
                        font.family: Theme.titleFont.family
                        font.pixelSize: Theme.scaled(16)
                        font.bold: true
                        color: Theme.textColor
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        height: 1
                        color: Theme.borderColor
                    }

                    // Stop-at type
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(8)

                        Text {
                            text: TranslationManager.translate("profileinfo.stopAt", "Stop at:")
                            font: Theme.bodyFont
                            color: Theme.textSecondaryColor
                        }
                        Text {
                            text: {
                                if (!profileData) return "-"
                                var type = profileData.stop_at_type === "volume" ?
                                    TranslationManager.translate("profileinfo.volume", "Volume") :
                                    TranslationManager.translate("profileinfo.weight", "Weight")
                                var value = profileData.stop_at_type === "volume" ?
                                    (profileData.target_volume || 0).toFixed(0) + " ml" :
                                    (profileData.target_weight || 0).toFixed(0) + " g"
                                return type + ", " + value
                            }
                            font: Theme.bodyFont
                            color: Theme.textColor
                        }
                    }

                    // Temperature
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(8)

                        Text {
                            text: TranslationManager.translate("profileinfo.temperature", "Temperature:")
                            font: Theme.bodyFont
                            color: Theme.textSecondaryColor
                        }
                        Text {
                            text: profileData ? (profileData.espresso_temperature || 0).toFixed(1) + " °C" : "-"
                            font: Theme.bodyFont
                            color: Theme.textColor
                        }
                    }

                    // Frame count
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(8)

                        Text {
                            text: TranslationManager.translate("profileinfo.frames", "Frames:")
                            font: Theme.bodyFont
                            color: Theme.textSecondaryColor
                        }
                        Text {
                            text: profileData && profileData.steps ? profileData.steps.length.toString() : "0"
                            font: Theme.bodyFont
                            color: Theme.textColor
                        }
                    }
                }
            }

            // Profile Notes Section
            Rectangle {
                Layout.fillWidth: true
                color: Theme.surfaceColor
                radius: Theme.cardRadius
                implicitHeight: notesColumn.height + Theme.scaled(24)

                ColumnLayout {
                    id: notesColumn
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: Theme.scaled(12)
                    spacing: Theme.scaled(8)

                    // Section header
                    Text {
                        text: TranslationManager.translate("profileinfo.notes", "Profile Notes")
                        font.family: Theme.titleFont.family
                        font.pixelSize: Theme.scaled(16)
                        font.bold: true
                        color: Theme.textColor
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        height: 1
                        color: Theme.borderColor
                    }

                    // Notes content
                    Text {
                        Layout.fillWidth: true
                        text: {
                            if (profileData && profileData.profile_notes && profileData.profile_notes.length > 0) {
                                return profileData.profile_notes
                            }
                            return TranslationManager.translate("profileinfo.noNotes", "No notes available for this profile.")
                        }
                        font: Theme.bodyFont
                        color: profileData && profileData.profile_notes && profileData.profile_notes.length > 0 ?
                               Theme.textColor : Theme.textSecondaryColor
                        wrapMode: Text.Wrap
                        textFormat: Text.PlainText
                    }
                }
            }

            // Spacer at bottom for padding
            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.scaled(20)
            }
        }
    }

    // Bottom bar with back button
    BottomBar {
        id: bottomBar
        title: TranslationManager.translate("profileinfo.title", "Profile Info")
        onBackClicked: pageStack.pop()
    }
}
