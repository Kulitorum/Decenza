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

    ColumnLayout {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: bottomBar.top
        spacing: 0

        // Header
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: Theme.standardMargin
            spacing: Theme.spacingMedium

            Tr {
                key: "comparison.title"
                fallback: "Compare Shots"
                font: Theme.titleFont
                color: Theme.textColor
                Layout.fillWidth: true
            }

            ActionButton {
                translationKey: "comparison.clear"
                translationFallback: "Clear"
                onClicked: {
                    comparisonModel.clearAll()
                    pageStack.pop()
                }
            }
        }

        // Legend
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.standardMargin
            Layout.rightMargin: Theme.standardMargin
            spacing: Theme.spacingLarge

            Repeater {
                model: comparisonModel.shotCount

                RowLayout {
                    spacing: Theme.spacingSmall

                    Rectangle {
                        width: Theme.scaled(16)
                        height: Theme.scaled(16)
                        radius: 4
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
            Layout.margins: Theme.standardMargin
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
                    spacing: 3
                    Repeater {
                        model: 4
                        Rectangle { width: 3; height: 2; color: Theme.textSecondaryColor }
                    }
                }
                Tr { key: "comparison.weight"; fallback: "Weight"; font: Theme.captionFont; color: Theme.textSecondaryColor }
            }
        }

        // Comparison table
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: Theme.standardMargin
            color: Theme.surfaceColor
            radius: Theme.cardRadius

            ScrollView {
                anchors.fill: parent
                anchors.margins: Theme.spacingMedium
                contentWidth: availableWidth

                GridLayout {
                    columns: comparisonModel.shotCount + 1
                    columnSpacing: Theme.spacingMedium
                    rowSpacing: Theme.spacingSmall
                    width: parent.width

                    // Header row
                    Tr {
                        key: "comparison.metric"
                        fallback: "Metric"
                        font: Theme.subtitleFont
                        color: Theme.textColor
                    }
                    Repeater {
                        model: comparisonModel.shotCount
                        Text {
                            text: TranslationManager.translate("comparison.shot", "Shot") + " " + (index + 1)
                            font: Theme.subtitleFont
                            color: comparisonModel.getShotColor(index)
                        }
                    }

                    // Profile
                    Tr { key: "comparison.profile"; fallback: "Profile"; font: Theme.labelFont; color: Theme.textSecondaryColor }
                    Repeater {
                        model: comparisonModel.shotCount
                        Text {
                            text: comparisonModel.getShotInfo(index).profileName || "-"
                            font: Theme.labelFont
                            color: Theme.textColor
                            elide: Text.ElideRight
                            Layout.preferredWidth: Theme.scaled(120)
                        }
                    }

                    // Date
                    Tr { key: "comparison.date"; fallback: "Date"; font: Theme.labelFont; color: Theme.textSecondaryColor }
                    Repeater {
                        model: comparisonModel.shotCount
                        Text {
                            text: comparisonModel.getShotInfo(index).dateTime || "-"
                            font: Theme.labelFont
                            color: Theme.textColor
                        }
                    }

                    // Duration
                    Tr { key: "comparison.duration"; fallback: "Duration"; font: Theme.labelFont; color: Theme.textSecondaryColor }
                    Repeater {
                        model: comparisonModel.shotCount
                        Text {
                            text: (comparisonModel.getShotInfo(index).duration || 0).toFixed(1) + "s"
                            font: Theme.labelFont
                            color: Theme.textColor
                        }
                    }

                    // Dose
                    Tr { key: "comparison.dose"; fallback: "Dose"; font: Theme.labelFont; color: Theme.textSecondaryColor }
                    Repeater {
                        model: comparisonModel.shotCount
                        Text {
                            text: (comparisonModel.getShotInfo(index).doseWeight || 0).toFixed(1) + "g"
                            font: Theme.labelFont
                            color: Theme.textColor
                        }
                    }

                    // Output
                    Tr { key: "comparison.output"; fallback: "Output"; font: Theme.labelFont; color: Theme.textSecondaryColor }
                    Repeater {
                        model: comparisonModel.shotCount
                        Text {
                            text: (comparisonModel.getShotInfo(index).finalWeight || 0).toFixed(1) + "g"
                            font: Theme.labelFont
                            color: Theme.textColor
                        }
                    }

                    // Ratio
                    Tr { key: "comparison.ratio"; fallback: "Ratio"; font: Theme.labelFont; color: Theme.textSecondaryColor }
                    Repeater {
                        model: comparisonModel.shotCount
                        Text {
                            text: comparisonModel.getShotInfo(index).ratio || "-"
                            font: Theme.labelFont
                            color: Theme.textColor
                        }
                    }

                    // Rating
                    Tr { key: "comparison.rating"; fallback: "Rating"; font: Theme.labelFont; color: Theme.textSecondaryColor }
                    Repeater {
                        model: comparisonModel.shotCount
                        Text {
                            property int rating: Math.round((comparisonModel.getShotInfo(index).enjoyment || 0) / 20)
                            text: {
                                var stars = ""
                                for (var i = 0; i < 5; i++) stars += i < rating ? "\u2605" : "\u2606"
                                return stars
                            }
                            font: Theme.labelFont
                            color: Theme.warningColor
                        }
                    }

                    // Bean
                    Tr { key: "comparison.bean"; fallback: "Bean"; font: Theme.labelFont; color: Theme.textSecondaryColor }
                    Repeater {
                        model: comparisonModel.shotCount
                        Text {
                            text: {
                                var info = comparisonModel.getShotInfo(index)
                                return (info.beanBrand || "") + (info.beanType ? " " + info.beanType : "") || "-"
                            }
                            font: Theme.labelFont
                            color: Theme.textColor
                            elide: Text.ElideRight
                            Layout.preferredWidth: Theme.scaled(120)
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
