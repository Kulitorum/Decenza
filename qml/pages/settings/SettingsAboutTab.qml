import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DecenzaDE1
import "../../components"

Item {
    id: aboutTab

    RowLayout {
        anchors.fill: parent
        spacing: Theme.spacingMedium

        // Main content area
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Theme.surfaceColor
            radius: Theme.cardRadius

            Flickable {
                anchors.fill: parent
                anchors.margins: Theme.spacingMedium
                contentHeight: contentColumn.height
                clip: true
                boundsBehavior: Flickable.StopAtBounds

                ColumnLayout {
                    id: contentColumn
                    width: parent.width
                    spacing: Theme.spacingLarge

                    // App logo/title area
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingSmall

                        Text {
                            text: "Decenza"
                            font.pixelSize: Theme.scaled(32)
                            font.bold: true
                            color: Theme.primaryColor
                            Layout.alignment: Qt.AlignHCenter
                        }

                        Text {
                            text: "Version " + Qt.application.version
                            font: Theme.bodyFont
                            color: Theme.textSecondaryColor
                            Layout.alignment: Qt.AlignHCenter
                        }
                    }

                    // Divider
                    Rectangle {
                        Layout.fillWidth: true
                        height: 1
                        color: Theme.borderColor
                    }

                    // Story section
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingMedium

                        Text {
                            Layout.fillWidth: true
                            text: "Built by Michael Holm (Kulitorum) during Christmas 2025. Three weeks, lots of coffee, one app."
                            font: Theme.bodyFont
                            color: Theme.textColor
                            wrapMode: Text.Wrap
                            lineHeight: 1.4
                        }
                    }

                    // Divider
                    Rectangle {
                        Layout.fillWidth: true
                        height: 1
                        color: Theme.borderColor
                    }

                    // Support section
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingMedium

                        Text {
                            Layout.fillWidth: true
                            text: "If you find this app useful, donations are welcome but never expected."
                            font: Theme.bodyFont
                            color: Theme.textColor
                            wrapMode: Text.Wrap
                            lineHeight: 1.4
                        }

                        // Donation button
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.topMargin: Theme.spacingSmall
                            height: Theme.scaled(56)
                            radius: Theme.buttonRadius
                            color: "#0070BA"  // PayPal blue

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: Qt.openUrlExternally("https://www.paypal.com/donate?business=paypal@kulitorum.com")
                            }

                            Text {
                                anchors.centerIn: parent
                                text: "Donate via PayPal"
                                font.pixelSize: Theme.scaled(16)
                                font.bold: true
                                color: "white"
                            }
                        }

                        Text {
                            Layout.fillWidth: true
                            Layout.topMargin: Theme.spacingSmall
                            text: "paypal@kulitorum.com"
                            font: Theme.captionFont
                            color: Theme.textSecondaryColor
                            horizontalAlignment: Text.AlignHCenter
                        }

                        Image {
                            Layout.alignment: Qt.AlignHCenter
                            Layout.topMargin: Theme.spacingMedium
                            source: "qrc:/qrcode.png"
                            width: Theme.scaled(150)
                            height: Theme.scaled(150)
                            fillMode: Image.PreserveAspectFit
                            sourceSize.width: 150
                            sourceSize.height: 150
                        }
                    }

                    // Divider
                    Rectangle {
                        Layout.fillWidth: true
                        height: 1
                        color: Theme.borderColor
                    }

                    // Credits section
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingSmall

                        Text {
                            Layout.fillWidth: true
                            text: "Thanks to the Decent community and the de1app developers for inspiration."
                            font: Theme.bodyFont
                            color: Theme.textSecondaryColor
                            wrapMode: Text.Wrap
                            lineHeight: 1.4
                        }
                    }

                    // Bottom spacing
                    Item {
                        Layout.fillWidth: true
                        height: Theme.spacingLarge
                    }
                }
            }
        }
    }
}
