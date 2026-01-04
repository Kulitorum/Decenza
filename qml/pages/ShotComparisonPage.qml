import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DecenzaDE1
import "../components"

Page {
    id: shotComparisonPage
    objectName: "shotComparisonPage"
    background: Rectangle { color: Theme.backgroundColor }

    property var comparisonModel: MainController.shotComparison

    Component.onCompleted: {
        root.currentPageTitle = "Compare Shots"
    }

    // Scrollable content area (vertical only)
    Flickable {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: bottomBar.top
        anchors.leftMargin: Theme.standardMargin
        anchors.rightMargin: Theme.standardMargin
        anchors.topMargin: Theme.pageTopMargin
        contentWidth: width  // Lock horizontal scroll
        contentHeight: contentColumn.height
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        ColumnLayout {
            id: contentColumn
            width: parent.width
            spacing: Theme.spacingSmall

            // Legend
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingLarge

                Repeater {
                    model: comparisonModel.shotCount

                    RowLayout {
                        spacing: Theme.spacingSmall

                        Rectangle {
                            width: Theme.scaled(16)
                            height: Theme.scaled(16)
                            radius: Theme.scaled(4)
                            color: comparisonModel.getShotColor(index)
                        }

                        Text {
                            text: {
                                var info = comparisonModel.getShotInfo(index)
                                return info.profileName + " - " + info.dateTime
                            }
                            font: Theme.labelFont
                            color: Theme.textColor
                            elide: Text.ElideRight
                            Layout.preferredWidth: Theme.scaled(200)
                        }
                    }
                }
            }

            // Graph
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.scaled(280)
                Layout.topMargin: Theme.spacingSmall
                color: Theme.surfaceColor
                radius: Theme.cardRadius

                ComparisonGraph {
                    anchors.fill: parent
                    anchors.margins: Theme.spacingSmall
                    comparisonModel: shotComparisonPage.comparisonModel
                }
            }

            // Line type legend
            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                spacing: Theme.spacingLarge

                RowLayout {
                    spacing: Theme.spacingSmall
                    Rectangle { width: Theme.scaled(20); height: 2; color: Theme.textSecondaryColor }
                    Tr { key: "comparison.pressure"; fallback: "Pressure"; font: Theme.captionFont; color: Theme.textSecondaryColor }
                }
                RowLayout {
                    spacing: Theme.spacingSmall
                    Rectangle {
                        width: Theme.scaled(20); height: 2; color: Theme.textSecondaryColor
                        Rectangle { anchors.fill: parent; color: "transparent"; border.color: Theme.textSecondaryColor; border.width: 1 }
                    }
                    Tr { key: "comparison.flow"; fallback: "Flow"; font: Theme.captionFont; color: Theme.textSecondaryColor }
                }
                RowLayout {
                    spacing: Theme.spacingSmall
                    Row {
                        spacing: Theme.scaled(3)
                        Repeater {
                            model: 4
                            Rectangle { width: 3; height: 2; color: Theme.textSecondaryColor }
                        }
                    }
                    Tr { key: "comparison.weight"; fallback: "Weight"; font: Theme.captionFont; color: Theme.textSecondaryColor }
                }
            }

            // Shot columns
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: Theme.spacingSmall
                Layout.bottomMargin: Theme.spacingSmall
                spacing: Theme.spacingMedium

                Repeater {
                    model: comparisonModel.shotCount

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: shotColumn.height + Theme.spacingMedium * 2
                        Layout.alignment: Qt.AlignTop
                        color: Theme.surfaceColor
                        radius: Theme.cardRadius

                        ColumnLayout {
                            id: shotColumn
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.margins: Theme.spacingMedium
                            spacing: Theme.spacingSmall

                            // Shot header with color indicator
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Theme.spacingSmall

                                Rectangle {
                                    width: Theme.scaled(12)
                                    height: Theme.scaled(12)
                                    radius: Theme.scaled(3)
                                    color: comparisonModel.getShotColor(index)
                                }

                                Text {
                                    text: TranslationManager.translate("comparison.shot", "Shot") + " " + (index + 1)
                                    font: Theme.subtitleFont
                                    color: comparisonModel.getShotColor(index)
                                }
                            }

                            // Profile
                            Text {
                                text: comparisonModel.getShotInfo(index).profileName || "-"
                                font: Theme.labelFont
                                color: Theme.textColor
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }

                            // Date
                            Text {
                                text: comparisonModel.getShotInfo(index).dateTime || "-"
                                font: Theme.captionFont
                                color: Theme.textSecondaryColor
                            }

                            // Separator
                            Rectangle {
                                Layout.fillWidth: true
                                height: Theme.scaled(1)
                                color: Theme.borderColor
                            }

                            // Metrics grid
                            GridLayout {
                                Layout.fillWidth: true
                                columns: 2
                                columnSpacing: Theme.spacingSmall
                                rowSpacing: Theme.spacingSmall

                                Tr { key: "comparison.duration"; fallback: "Duration"; font: Theme.captionFont; color: Theme.textSecondaryColor }
                                Text { text: (comparisonModel.getShotInfo(index).duration || 0).toFixed(1) + "s"; font: Theme.labelFont; color: Theme.textColor }

                                Tr { key: "comparison.dose"; fallback: "Dose"; font: Theme.captionFont; color: Theme.textSecondaryColor }
                                Text { text: (comparisonModel.getShotInfo(index).doseWeight || 0).toFixed(1) + "g"; font: Theme.labelFont; color: Theme.textColor }

                                Tr { key: "comparison.output"; fallback: "Output"; font: Theme.captionFont; color: Theme.textSecondaryColor }
                                Text { text: (comparisonModel.getShotInfo(index).finalWeight || 0).toFixed(1) + "g"; font: Theme.labelFont; color: Theme.textColor }

                                Tr { key: "comparison.ratio"; fallback: "Ratio"; font: Theme.captionFont; color: Theme.textSecondaryColor }
                                Text { text: comparisonModel.getShotInfo(index).ratio || "-"; font: Theme.labelFont; color: Theme.textColor }

                                Tr { key: "comparison.rating"; fallback: "Rating"; font: Theme.captionFont; color: Theme.textSecondaryColor }
                                Text { text: (comparisonModel.getShotInfo(index).enjoyment || 0) + "%"; font: Theme.labelFont; color: Theme.warningColor }

                                Tr { key: "comparison.bean"; fallback: "Bean"; font: Theme.captionFont; color: Theme.textSecondaryColor }
                                Text {
                                    text: {
                                        var info = comparisonModel.getShotInfo(index)
                                        return (info.beanBrand || "") + (info.beanType ? " " + info.beanType : "") || "-"
                                    }
                                    font: Theme.labelFont
                                    color: Theme.textColor
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }
                            }

                            // Separator
                            Rectangle {
                                Layout.fillWidth: true
                                height: Theme.scaled(1)
                                color: Theme.borderColor
                            }

                            // Notes section
                            Tr {
                                key: "comparison.notes"
                                fallback: "Notes"
                                font: Theme.captionFont
                                color: Theme.textSecondaryColor
                            }

                            Text {
                                text: comparisonModel.getShotInfo(index).notes || "-"
                                font: Theme.labelFont
                                color: Theme.textColor
                                wrapMode: Text.Wrap
                                Layout.fillWidth: true
                            }
                        }
                    }
                }
            }
        }
    }

    // Bottom bar
    BottomBar {
        id: bottomBar
        title: TranslationManager.translate("comparison.title", "Compare Shots")
        rightText: comparisonModel.shotCount + " " + TranslationManager.translate("comparison.shots", "shots")
        onBackClicked: root.goBack()
    }
}
