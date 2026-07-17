import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decenza

// Quick coffee:water ratio chooser, opened from the ratioQuickSelect layout
// widget (placeable in any zone). Offers three user-configurable presets
// (Ristretto / Normale / Lungo; defaults 1:1 / 1:2 / 1:3, read from
// Settings.brew.ratioPreset1/2/3). Applying a pick arms a RATIO ANCHOR on the
// session (add-yield-ratio-anchor) — identical in effect to editing the ratio
// row in Brew Settings — so the gram target derives from the dose and keeps
// re-deriving when the dose changes. It writes no recipe or bag (persisting
// is the Update button's job in Brew Settings) and records lastUsedRatio only
// as preset memory. Also includes a short "about ratios" help panel.
// Descriptions are original but the ratio styles/learning are adapted from
// La Marzocco Home's article "Brew Ratios Around the World" (credited in the
// footer).
Dialog {
    id: root
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(Theme.scaled(520), parent ? parent.width * 0.95 : Theme.scaled(520))
    height: Math.min(contentCol.implicitHeight + Theme.scaled(40), parent ? parent.height * 0.92 : Theme.scaled(640))
    modal: true
    closePolicy: Dialog.CloseOnEscape | Dialog.CloseOnPressOutside
    padding: 0

    // Pick-only mode (add-yield-ratio-anchor): the three named ratios are the
    // app's smart-ratio vocabulary, and they are just as useful when DESIGNING
    // a drink (the recipe wizard) or dialing a not-yet-committed value (Brew
    // Settings) as when arming the current brew. In pick-only mode a tap emits
    // ratioPicked() and writes NOTHING — the host decides what the pick means.
    // Default false = the idle widget's behaviour: arm the session anchor.
    property bool pickOnly: false
    signal ratioPicked(double ratio)

    property bool showHelp: false
    // Each standard is a RANGE in practice (ristretto ~1:1–1.5, normale ~1:2–2.5,
    // lungo ~1:3+); the right point depends on the bean and the machine, so fixed
    // 1:1/1:2/1:3 values need to be adjustable. Off by default; tapping a card
    // still just applies it.
    property bool editMode: false

    // Clamp + round to one decimal, then write the preset for this card's index.
    // Bounds match SettingsBrew::setRatioPreset1/2/3's qBound(0.5, r, 6.0).
    function setPresetRatio(idx, r) {
        var v = Math.max(0.5, Math.min(6.0, Math.round(r * 10) / 10))
        if (idx === 1) Settings.brew.ratioPreset1 = v
        else if (idx === 2) Settings.brew.ratioPreset2 = v
        else Settings.brew.ratioPreset3 = v
    }
    // Which ratio the "current" badge marks. Defaults to the live session:
    // the stored anchor when ratio-anchored, else derived (target ÷ dose) —
    // ProfileManager.brewByRatio folds both — so the highlighted preset
    // reflects reality, not lastUsedRatio. With no dose recorded a derived
    // ratio is unknowable (brewByRatio is 0): no card highlights, rather than
    // faking one against an 18 g stand-in. A pick-only host overrides this
    // with the value IT is editing (the wizard's field, the dialog's dial) —
    // the badge must mark what the user is changing, not the live brew.
    property double compareRatio: ProfileManager.brewByRatio > 0
        ? ProfileManager.brewByRatio
        : (ProfileManager.brewByRatioDose > 0
           ? ProfileManager.targetWeight / ProfileManager.brewByRatioDose : 0)
    readonly property double currentRatio: compareRatio

    // The three presets (ratios are user-configurable, defaulting to 1/2/3).
    // `desc` are our own words, informed by the La Marzocco article credited below.
    readonly property var presets: [
        { idx: 1, ratio: Settings.brew.ratioPreset1, key: "ratio.ristretto", name: "Ristretto",
          desc: "Short & concentrated — syrupy and heavy-bodied with intense flavour. Cuts through milk; suits darker roasts." },
        { idx: 2, ratio: Settings.brew.ratioPreset2, key: "ratio.normale", name: "Normale",
          desc: "The modern specialty standard — balanced body and clarity with higher extraction. Flatters lighter roasts and single origins." },
        { idx: 3, ratio: Settings.brew.ratioPreset3, key: "ratio.lungo", name: "Lungo",
          desc: "Long & lighter — brighter clarity and less body, so individual tasting notes show through. Closer to filter coffee." }
    ]

    function applyRatio(r) {
        // Pick-only: hand the choice to the host and touch nothing. The
        // wizard writes it into the recipe being designed; Brew Settings
        // writes its own local dial (committed on OK). Neither may arm the
        // session behind the user's back.
        if (root.pickOnly) {
            root.ratioPicked(r)
            root.close()
            return
        }
        // Arm a RATIO ANCHOR on the session (add-yield-ratio-anchor) — the
        // ratio itself is stored, not a flattened dose x ratio snapshot, so
        // the target derives live from whatever dose is (or becomes) known
        // and shows up everywhere immediately: the scale widget
        // (ProfileManager.brewByRatio), Brew Settings, and the machine
        // target (resolved to grams in ProfileManager::targetWeight).
        // lastUsedRatio is preset memory only.
        Settings.brew.lastUsedRatio = r
        Settings.brew.setBrewRatioAnchor(r)
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
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingSmall
                    Text {
                        text: TranslationManager.translate("ratio.dialog.title", "Brew Ratio")
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(24); font.bold: true
                    }
                    Item { Layout.fillWidth: true }
                    // Edit toggle: reveals a per-card stepper to tune each preset's ratio.
                    Rectangle {
                        Layout.preferredHeight: Theme.scaled(34)
                        Layout.preferredWidth: editLabel.implicitWidth + Theme.spacingMedium * 2
                        radius: Theme.buttonRadius
                        color: editMa.pressed ? Qt.darker(Theme.backgroundColor, 1.1)
                             : (root.editMode ? Theme.primaryColor : "transparent")
                        border.width: 1
                        border.color: root.editMode ? Theme.primaryColor : Theme.borderColor
                        Accessible.role: Accessible.Button
                        Accessible.name: root.editMode
                            ? TranslationManager.translate("ratio.edit.done", "Done editing ratios")
                            : TranslationManager.translate("ratio.edit.button", "Edit ratios")
                        Accessible.focusable: true
                        Accessible.onPressAction: editMa.clicked(null)
                        Text {
                            id: editLabel
                            anchors.centerIn: parent
                            text: root.editMode
                                ? TranslationManager.translate("common.button.done", "Done")
                                : TranslationManager.translate("ratio.edit.button", "Edit")
                            color: root.editMode ? Theme.primaryContrastColor : Theme.primaryColor
                            font: Theme.labelFont
                        }
                        MouseArea { id: editMa; anchors.fill: parent; onClicked: root.editMode = !root.editMode }
                    }
                }
                Text {
                    text: root.editMode
                        ? TranslationManager.translate("ratio.dialog.subtitleEdit", "Adjust each ratio to suit your beans and machine")
                        : TranslationManager.translate("ratio.dialog.subtitle", "Coffee : water — tap a style to use it")
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
                    // In edit mode the card is not an apply button — the steppers are the actionable
                    // controls. Gate the accessibility activation the same way cardMa.enabled gates
                    // pointer taps (a directly-emitted clicked() ignores enabled), and don't announce
                    // the card as a button so screen-reader focus lands on the steppers instead.
                    Accessible.focusable: !root.editMode
                    Accessible.onPressAction: if (!root.editMode) cardMa.clicked(null)

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

                        RowLayout {
                            Layout.fillWidth: true
                            Layout.topMargin: Theme.spacingSmall
                            visible: root.editMode
                            spacing: Theme.spacingMedium

                            Rectangle {
                                Layout.preferredWidth: Theme.scaled(46)
                                Layout.preferredHeight: Theme.scaled(46)
                                radius: Theme.buttonRadius
                                color: minusMa.pressed ? Qt.darker(Theme.primaryColor, 1.15) : Theme.primaryColor
                                Accessible.role: Accessible.Button
                                Accessible.name: TranslationManager.translate("ratio.edit.decrease", "Decrease %1 ratio")
                                                 .arg(TranslationManager.translate(modelData.key + ".name", modelData.name))
                                Accessible.focusable: true
                                Accessible.onPressAction: minusMa.clicked(null)
                                Text { anchors.centerIn: parent; text: "—"; color: Theme.primaryContrastColor
                                       font.pixelSize: Theme.scaled(24); font.bold: true }
                                MouseArea { id: minusMa; anchors.fill: parent
                                    onClicked: root.setPresetRatio(modelData.idx, modelData.ratio - 0.1) }
                            }
                            Text {
                                Layout.fillWidth: true
                                horizontalAlignment: Text.AlignHCenter
                                text: "1:" + modelData.ratio.toFixed(1)
                                color: Theme.textColor
                                font.pixelSize: Theme.scaled(20); font.bold: true
                            }
                            Rectangle {
                                Layout.preferredWidth: Theme.scaled(46)
                                Layout.preferredHeight: Theme.scaled(46)
                                radius: Theme.buttonRadius
                                color: plusMa.pressed ? Qt.darker(Theme.primaryColor, 1.15) : Theme.primaryColor
                                Accessible.role: Accessible.Button
                                Accessible.name: TranslationManager.translate("ratio.edit.increase", "Increase %1 ratio")
                                                 .arg(TranslationManager.translate(modelData.key + ".name", modelData.name))
                                Accessible.focusable: true
                                Accessible.onPressAction: plusMa.clicked(null)
                                Text { anchors.centerIn: parent; text: "+"; color: Theme.primaryContrastColor
                                       font.pixelSize: Theme.scaled(24); font.bold: true }
                                MouseArea { id: plusMa; anchors.fill: parent
                                    onClicked: root.setPresetRatio(modelData.idx, modelData.ratio + 0.1) }
                            }
                        }
                    }

                    MouseArea {
                        id: cardMa
                        anchors.fill: parent
                        enabled: !root.editMode
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
                    text: (root.showHelp ? "— " : "+ ") + TranslationManager.translate("ratio.help.button", "About brew ratios")
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
