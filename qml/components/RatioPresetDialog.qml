pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import Decenza

RatioChoiceDialog {
    id: root
    SettingsSearch.route: "ratioPresets"
    SettingsSearch.title: TranslationManager.translate("settings.search.ratioPresets", "Brew ratio presets")
    SettingsSearch.keywords: ["ratio", "ristretto", "normale", "lungo", "preset"]
    allowPresetEditing: true
    closeOnPick: false
    titleText: TranslationManager.translate("ratio.dialog.title", "Brew Ratio")
    subtitle: TranslationManager.translate("ratio.dialog.subtitle", "Coffee : water — tap a style to use it")
    editSubtitle: TranslationManager.translate("ratio.dialog.subtitleEdit", "Adjust each ratio to suit your beans and machine")
    property bool pickOnly: false
    property string _pendingPreferenceRequestId: ""
    property string _pendingPreferenceName: ""
    property bool _closeAfterPreference: false
    pending: _pendingPreferenceRequestId !== ""
    minimumRatio: Number(RecipeContext.preferences.minRatio || 0.5)
    maximumRatio: Number(RecipeContext.preferences.maxRatio || 100.0)

    function writePreference(name, value, closeAfterSuccess) {
        if (root._pendingPreferenceRequestId || !RecipeContext.ready)
            return false
        const id = RecipeContext.newRequestId()
        root._pendingPreferenceRequestId = id
        root._pendingPreferenceName = name
        root._closeAfterPreference = closeAfterSuccess
        root.errorMessage = ""
        RecipeContext.setPreference(id, name, value, RecipeContext.revision)
        return true
    }

    Connections {
        target: RecipeContext
        function onPreferenceCompleted(requestId, name, success, preferences, reason) {
            if (requestId !== root._pendingPreferenceRequestId
                    || name !== root._pendingPreferenceName)
                return
            const closeAfter = root._closeAfterPreference
            root._pendingPreferenceRequestId = ""
            root._pendingPreferenceName = ""
            root._closeAfterPreference = false
            if (!success) {
                root.errorMessage = TranslationManager.translate(
                    "common.error.saveFailed", "Could not save this setting. Try again.")
                return
            }
            if (closeAfter)
                root.close()
        }
    }

    property bool showHelp: false
    // Each standard is a RANGE in practice (ristretto ~1:1–1.5, normale ~1:2–2.5,
    // lungo ~1:3+); the right point depends on the bean and the machine, so fixed
    // 1:1/1:2/1:3 values need to be adjustable. Off by default; tapping a card
    // still just applies it.


    // Which ratio the "current" badge marks. Defaults to the live session:
    // the stored anchor when ratio-anchored, else derived (target ÷ dose) —
    // ProfileManager.brewByRatio folds both — so the highlighted preset
    // reflects reality, not lastUsedRatio. With no dose recorded a derived
    // ratio is unknowable (brewByRatio is 0): no card highlights, rather than
    // faking one against an 18 g stand-in. A pick-only host overrides this
    // with the value IT is editing (the wizard's field, the dialog's dial) —
    // the badge must mark what the user is changing, not the live brew.
    compareRatio: ProfileManager.brewByRatio > 0
        ? ProfileManager.brewByRatio
        : (ProfileManager.brewByRatioDose > 0
           ? ProfileManager.targetWeight / ProfileManager.brewByRatioDose : 0)


    // The three presets (ratios are user-configurable, defaulting to 1/2/3).
    // `desc` are our own words, informed by the La Marzocco article credited below.
    presets: [
        { idx: 1, ratio: Number(RecipeContext.preferences.ratioPreset1 || 1.0), key: "ratio.ristretto", name: "Ristretto",
          desc: "Short & concentrated — syrupy and heavy-bodied with intense flavour. Cuts through milk; suits darker roasts." },
        { idx: 2, ratio: Number(RecipeContext.preferences.ratioPreset2 || 2.0), key: "ratio.normale", name: "Normale",
          desc: "The modern specialty standard — balanced body and clarity with higher extraction. Flatters lighter roasts and single origins." },
        { idx: 3, ratio: Number(RecipeContext.preferences.ratioPreset3 || 3.0), key: "ratio.lungo", name: "Lungo",
          desc: "Long & lighter — brighter clarity and less body, so individual tasting notes show through. Closer to filter coffee." }
    ]

    onPresetRatioEdited: function(index, value) {
        root.writePreference(index === 1 ? "ratioPreset1" : index === 2 ? "ratioPreset2" : "ratioPreset3", value, false)
    }
    onRatioPicked: function(value) {
        if (root.pickOnly) root.close()
        else root.writePreference("brewRatioAnchor", value, true)
    }

    helpContent: Component {
        ColumnLayout {
            spacing: Theme.spacingSmall
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
                Layout.preferredHeight: Theme.touchTargetMin
                radius: Theme.buttonRadius
                color: helpMa.pressed ? Qt.darker(Theme.backgroundColor, 1.1) : "transparent"
                border.width: 1
                border.color: Theme.borderColor
                Accessible.role: Accessible.Button
                Accessible.name: TranslationManager.translate("ratio.help.button", "About brew ratios")
                Accessible.focusable: true
                Accessible.onPressAction: helpMa.clicked(null)
                        activeFocusOnTab: true
                        Keys.onReturnPressed: helpMa.clicked(null)
                        Keys.onSpacePressed: helpMa.clicked(null)
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
                        activeFocusOnTab: true
                        Keys.onReturnPressed: linkMa.clicked(null)
                        Keys.onSpacePressed: linkMa.clicked(null)
                    MouseArea {
                        id: linkMa
                        anchors.fill: parent
                        onClicked: Qt.openUrlExternally("https://home.lamarzoccousa.com/brew-ratios-around-world/")
                    }
                }
            }

        }
    }
}
