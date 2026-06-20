import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decenza

// Quick coffee:water ratio chooser, opened from the home-screen status bar.
// Offers the three classic styles (Ristretto 1:1, Normale 1:2, Lungo 1:3),
// applies the pick to Settings.brew.lastUsedRatio (and recomputes the yield
// override from the current dose so it takes effect on the next shot), and
// includes a short "about ratios" help panel. Descriptions are original but
// the ratio styles/learning are adapted from La Marzocco Home's article
// "Brew Ratios Around the World" (credited in the footer).
Dialog {
    id: root
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(Theme.scaled(520), parent ? parent.width * 0.95 : Theme.scaled(520))
    height: Math.min(contentCol.implicitHeight + Theme.scaled(40), parent ? parent.height * 0.92 : Theme.scaled(640))
    modal: true
    closePolicy: Dialog.CloseOnEscape | Dialog.CloseOnPressOutside
    padding: 0

    property bool showHelp: false
    readonly property double currentRatio: Settings.brew.lastUsedRatio

    // The three classic styles. `desc` are our own words, informed by the
    // La Marzocco article credited below.
    readonly property var presets: [
        { ratio: Settings.brew.ratioPreset1, key: "ratio.ristretto", name: "Ristretto",
          desc: "Short & concentrated — syrupy and heavy-bodied with intense flavour. Cuts through milk; suits darker roasts." },
        { ratio: Settings.brew.ratioPreset2, key: "ratio.normale", name: "Normale",
          desc: "The modern specialty standard — balanced body and clarity with higher extraction. Flatters lighter roasts and single origins." },
        { ratio: Settings.brew.ratioPreset3, key: "ratio.lungo", name: "Lungo",
          desc: "Long & lighter — brighter clarity and less body, so individual tasting notes show through. Closer to filter coffee." }
    ]

    function applyRatio(r) {
        Settings.brew.lastUsedRatio = r
        // Keep the stop-at-weight target consistent with the new ratio when a
        // dose is known (same math the bean auto-capture uses).
        if (Settings.dye.dyeBeanWeight > 0)
            Settings.brew.brewYieldOverride = Settings.dye.dyeBeanWeight * r
        root.close()
    }

    background: Rectangle {
        color: Theme.surfaceColor
        radius: Theme.cardRadius
        border.width: 1
        border.color: Theme.borderColor
    }

    contentItem: Flickable {
        contentWidth: width
        contentHeight: contentCol.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

        ColumnLayout {
            id: contentCol
            width: parent.width
            spacing: Theme.spacingMedium

            // --- Header ---
            ColumnLayout {
                Layout.fillWidth: true
                Layout.topMargin: Theme.spacingLarge
                Layout.leftMargin: Theme.spacingLarge
                Layout.rightMargin: Theme.spacingLarge
                spacing: Theme.scaled(2)
                Text {
                    text: TranslationManager.translate("ratio.dialog.title", "Brew Ratio")
                    color: Theme.textColor
                    font.pixelSize: Theme.scaled(24); font.bold: true
                }
                Text {
                    text: TranslationManager.translate("ratio.dialog.subtitle", "Coffee : water — tap a style to use it")
                    color: Theme.textSecondaryColor
                    font: Theme.labelFont
                }
            }

            // --- Ratio style cards ---
            Repeater {
                model: root.presets
                delegate: Rectangle {
                    id: card
                    required property var modelData
                    readonly property bool isCurrent: Math.abs(root.currentRatio - modelData.ratio) < 0.05
                    Layout.fillWidth: true
                    Layout.leftMargin: Theme.spacingLarge
                    Layout.rightMargin: Theme.spacingLarge
                    Layout.preferredHeight: cardCol.implicitHeight + Theme.spacingMedium * 2
                    radius: Theme.buttonRadius
                    color: cardMa.pressed ? Qt.darker(Theme.backgroundColor, 1.1) : Theme.backgroundColor
                    border.width: isCurrent ? 2 : 1
                    border.color: isCurrent ? Theme.primaryColor : Theme.borderColor

                    Accessible.role: Accessible.Button
                    Accessible.name: TranslationManager.translate(modelData.key + ".name", modelData.name)
                                     + " 1:" + modelData.ratio.toFixed(1)
                                     + (isCurrent ? ", " + TranslationManager.translate("ratio.current", "current") : "")
                    Accessible.focusable: true
                    Accessible.onPressAction: cardMa.clicked(null)

                    ColumnLayout {
                        id: cardCol
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.leftMargin: Theme.spacingMedium
                        anchors.rightMargin: Theme.spacingMedium
                        spacing: Theme.scaled(2)

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.spacingSmall
                            Text {
                                text: TranslationManager.translate(modelData.key + ".name", modelData.name)
                                color: Theme.textColor
                                font.pixelSize: Theme.scaled(19); font.bold: true
                            }
                            Text {
                                text: "1:" + modelData.ratio.toFixed(1)
                                color: Theme.primaryColor
                                font.pixelSize: Theme.scaled(19); font.bold: true
                            }
                            Item { Layout.fillWidth: true }
                            Text {
                                visible: card.isCurrent
                                text: TranslationManager.translate("ratio.current", "current").toUpperCase()
                                color: Theme.primaryColor
                                font: Theme.captionFont
                            }
                        }
                        Text {
                            Layout.fillWidth: true
                            text: TranslationManager.translate(modelData.key + ".desc", modelData.desc)
                            color: Theme.textSecondaryColor
                            font: Theme.captionFont
                            wrapMode: Text.WordWrap
                        }
                    }

                    MouseArea {
                        id: cardMa
                        anchors.fill: parent
                        onClicked: root.applyRatio(modelData.ratio)
                    }
                }
            }

            // --- Footnote: finer control lives in Espresso Setup ---
            Text {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.spacingLarge
                Layout.rightMargin: Theme.spacingLarge
                text: TranslationManager.translate("ratio.footnote", "For finer control, adjust the dose and target in Espresso Setup.")
                color: Theme.textSecondaryColor
                font: Theme.captionFont
                wrapMode: Text.WordWrap
            }

            // --- "About brew ratios" toggle ---
            Rectangle {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.spacingLarge
                Layout.rightMargin: Theme.spacingLarge
                Layout.preferredHeight: Theme.scaled(40)
                radius: Theme.buttonRadius
                color: helpMa.pressed ? Qt.darker(Theme.backgroundColor, 1.1) : "transparent"
                border.width: 1
                border.color: Theme.borderColor
                Accessible.role: Accessible.Button
                Accessible.name: TranslationManager.translate("ratio.help.button", "About brew ratios")
                Accessible.focusable: true
                Accessible.onPressAction: helpMa.clicked(null)
                Text {
                    anchors.centerIn: parent
                    text: (root.showHelp ? "– " : "+ ") + TranslationManager.translate("ratio.help.button", "About brew ratios")
                    color: Theme.primaryColor
                    font: Theme.labelFont
                }
                MouseArea { id: helpMa; anchors.fill: parent; onClicked: root.showHelp = !root.showHelp }
            }

            // --- Help body (collapsible) ---
            ColumnLayout {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.spacingLarge
                Layout.rightMargin: Theme.spacingLarge
                visible: root.showHelp
                spacing: Theme.spacingSmall
                Text {
                    Layout.fillWidth: true
                    text: TranslationManager.translate("ratio.help.intro",
                        "Brew ratio is the weight of coffee in vs. espresso out. A lower ratio (1:1) is shorter, thicker and more intense; a higher ratio (1:3) is longer, lighter and clearer. Dose stays the same — only the yield changes.")
                    color: Theme.textColor
                    font: Theme.captionFont
                    wrapMode: Text.WordWrap
                }
                Text {
                    Layout.fillWidth: true
                    text: TranslationManager.translate("ratio.help.regional",
                        "Tastes differ by region: 1:1 ristrettos (popularised in Seattle), the traditional ~1:3 Italian shot, and the modern specialty 1:2 normale.")
                    color: Theme.textSecondaryColor
                    font: Theme.captionFont
                    wrapMode: Text.WordWrap
                }
            }

            // --- Attribution (always shown) ---
            ColumnLayout {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.spacingLarge
                Layout.rightMargin: Theme.spacingLarge
                spacing: 0
                Text {
                    Layout.fillWidth: true
                    text: TranslationManager.translate("ratio.attribution",
                        "Ratio styles adapted from “Brew Ratios Around the World,” Ben Blake & Scott Callender — La Marzocco Home (2014).")
                    color: Theme.textSecondaryColor
                    font: Theme.captionFont
                    wrapMode: Text.WordWrap
                }
                Text {
                    id: articleLink
                    text: TranslationManager.translate("ratio.readArticle", "Read the full article")
                    color: Theme.primaryColor
                    font: Theme.captionFont
                    Accessible.role: Accessible.Link
                    Accessible.name: text
                    Accessible.focusable: true
                    Accessible.onPressAction: linkMa.clicked(null)
                    MouseArea {
                        id: linkMa
                        anchors.fill: parent
                        onClicked: Qt.openUrlExternally("https://home.lamarzoccousa.com/brew-ratios-around-world/")
                    }
                }
            }

            // --- Close ---
            Rectangle {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.spacingLarge
                Layout.rightMargin: Theme.spacingLarge
                Layout.bottomMargin: Theme.spacingLarge
                Layout.preferredHeight: Theme.scaled(48)
                radius: Theme.buttonRadius
                color: closeMa.pressed ? Qt.darker(Theme.primaryColor, 1.15) : Theme.primaryColor
                Accessible.role: Accessible.Button
                Accessible.name: TranslationManager.translate("common.button.close", "Close")
                Accessible.focusable: true
                Accessible.onPressAction: closeMa.clicked(null)
                Text {
                    anchors.centerIn: parent
                    text: TranslationManager.translate("common.button.close", "Close")
                    color: Theme.primaryContrastColor
                    font: Theme.bodyFont
                }
                MouseArea { id: closeMa; anchors.fill: parent; onClicked: root.close() }
            }
        }
    }
}
