import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DecenzaDE1

Dialog {
    id: root
    parent: Overlay.overlay
    x: (parent.width - width) / 2
    y: (parent.height - height) / 2 - Theme.scaled(80)  // Lift up to clear pills/finger
    width: Math.min(parent.width * 0.9, Theme.scaled(600))
    height: Math.min(parent.height * 0.5, Theme.scaled(400))
    modal: true
    padding: 0

    // Profile to preview
    property string profileFilename: ""
    property string profileName: ""

    // Loaded profile data
    property var profileData: null

    onAboutToShow: {
        if (profileFilename) {
            profileData = MainController.getProfileByFilename(profileFilename)
            if (profileData && profileData.steps) {
                profileGraph.frames = profileData.steps
            } else {
                profileGraph.frames = []
            }
        }
    }

    onAboutToHide: {
        profileData = null
        profileGraph.frames = []
    }

    background: Rectangle {
        color: Theme.surfaceColor
        radius: Theme.cardRadius
        border.width: 1
        border.color: Theme.borderColor
    }

    contentItem: ColumnLayout {
        spacing: 0

        // Header with profile name, info button, and close button
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.scaled(50)

            Text {
                anchors.left: parent.left
                anchors.leftMargin: Theme.scaled(16)
                anchors.verticalCenter: parent.verticalCenter
                anchors.right: moreInfoButton.left
                anchors.rightMargin: Theme.scaled(8)
                text: root.profileName || (root.profileData ? root.profileData.title : "")
                font: Theme.titleFont
                color: Theme.textColor
                elide: Text.ElideRight
            }

            // "More Info" button - navigates to ProfileInfoPage
            Rectangle {
                id: moreInfoButton
                anchors.right: closeButton.left
                anchors.rightMargin: Theme.scaled(4)
                anchors.verticalCenter: parent.verticalCenter
                width: moreInfoText.implicitWidth + Theme.scaled(16)
                height: Theme.scaled(32)
                radius: Theme.scaled(6)
                color: moreInfoMouseArea.pressed ? Qt.darker(Theme.primaryColor, 1.2) :
                       moreInfoMouseArea.containsMouse ? Theme.primaryColor : Qt.lighter(Theme.primaryColor, 1.1)

                Text {
                    id: moreInfoText
                    anchors.centerIn: parent
                    text: TranslationManager.translate("profilepreview.moreInfo", "More Info")
                    font.pixelSize: Theme.scaled(13)
                    font.bold: true
                    color: "white"
                }

                MouseArea {
                    id: moreInfoMouseArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        root.close()
                        pageStack.push(Qt.resolvedUrl("../pages/ProfileInfoPage.qml"), {
                            profileFilename: root.profileFilename,
                            profileName: root.profileName
                        })
                    }
                }

                Accessible.role: Accessible.Button
                Accessible.name: TranslationManager.translate("profilepreview.moreInfo", "More Info")
            }

            // Close button
            Rectangle {
                id: closeButton
                anchors.right: parent.right
                anchors.rightMargin: Theme.scaled(8)
                anchors.verticalCenter: parent.verticalCenter
                width: Theme.scaled(36)
                height: Theme.scaled(36)
                radius: width / 2
                color: closeMouseArea.pressed ? Qt.darker(Theme.surfaceColor, 1.2) : "transparent"
                Accessible.role: Accessible.Button
                Accessible.name: "Close preview"
                Accessible.focusable: true

                Text {
                    anchors.centerIn: parent
                    text: "✕"
                    font.pixelSize: Theme.scaled(20)
                    color: Theme.textSecondaryColor
                }

                MouseArea {
                    id: closeMouseArea
                    anchors.fill: parent
                    Accessible.ignored: true
                    onClicked: root.close()
                }
            }

            Rectangle {
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                height: 1
                color: Theme.borderColor
            }
        }

        // Profile graph
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: Theme.scaled(8)

            ProfileGraph {
                id: profileGraph
                anchors.fill: parent
                frames: []
                selectedFrameIndex: -1  // No selection in preview mode
                targetWeight: root.profileData ? (root.profileData.target_weight || 0) : 0
                targetVolume: root.profileData ? (root.profileData.target_volume || 0) : 0
            }

            // No data message
            Text {
                anchors.centerIn: parent
                visible: !root.profileData || !root.profileData.steps || root.profileData.steps.length === 0
                text: TranslationManager.translate("profilePreview.noProfileData", "No profile data")
                font: Theme.bodyFont
                color: Theme.textSecondaryColor
            }
        }
    }

    // Close on background click
    Overlay.modal: Rectangle {
        color: Qt.rgba(0, 0, 0, 0.5)

        MouseArea {
            anchors.fill: parent
            onClicked: root.close()
        }
    }
}
