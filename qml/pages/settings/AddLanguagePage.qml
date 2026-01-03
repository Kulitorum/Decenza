import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DecenzaDE1
import "../../components"

Page {
    id: addLanguagePage
    objectName: "addLanguagePage"
    background: Rectangle { color: Theme.backgroundColor }

    Component.onCompleted: root.currentPageTitle = "Add Language"
    StackView.onActivated: root.currentPageTitle = "Add Language"

    // ISO 639-1 common languages (sorted by English name)
    property var isoLanguages: [
        { code: "ar", name: "Arabic", native: "العربية" },
        { code: "bg", name: "Bulgarian", native: "Български" },
        { code: "ca", name: "Catalan", native: "Català" },
        { code: "zh", name: "Chinese", native: "中文" },
        { code: "hr", name: "Croatian", native: "Hrvatski" },
        { code: "cs", name: "Czech", native: "Čeština" },
        { code: "da", name: "Danish", native: "Dansk" },
        { code: "nl", name: "Dutch", native: "Nederlands" },
        { code: "et", name: "Estonian", native: "Eesti" },
        { code: "fi", name: "Finnish", native: "Suomi" },
        { code: "fr", name: "French", native: "Français" },
        { code: "de", name: "German", native: "Deutsch" },
        { code: "el", name: "Greek", native: "Ελληνικά" },
        { code: "he", name: "Hebrew", native: "עברית" },
        { code: "hi", name: "Hindi", native: "हिन्दी" },
        { code: "hu", name: "Hungarian", native: "Magyar" },
        { code: "id", name: "Indonesian", native: "Bahasa Indonesia" },
        { code: "it", name: "Italian", native: "Italiano" },
        { code: "ja", name: "Japanese", native: "日本語" },
        { code: "ko", name: "Korean", native: "한국어" },
        { code: "lv", name: "Latvian", native: "Latviešu" },
        { code: "lt", name: "Lithuanian", native: "Lietuvių" },
        { code: "ms", name: "Malay", native: "Bahasa Melayu" },
        { code: "nb", name: "Norwegian", native: "Norsk bokmål" },
        { code: "fa", name: "Persian", native: "فارسی" },
        { code: "pl", name: "Polish", native: "Polski" },
        { code: "pt", name: "Portuguese", native: "Português" },
        { code: "ro", name: "Romanian", native: "Română" },
        { code: "ru", name: "Russian", native: "Русский" },
        { code: "sr", name: "Serbian", native: "Српски" },
        { code: "sk", name: "Slovak", native: "Slovenčina" },
        { code: "sl", name: "Slovenian", native: "Slovenščina" },
        { code: "es", name: "Spanish", native: "Español" },
        { code: "sv", name: "Swedish", native: "Svenska" },
        { code: "th", name: "Thai", native: "ไทย" },
        { code: "tr", name: "Turkish", native: "Türkçe" },
        { code: "uk", name: "Ukrainian", native: "Українська" },
        { code: "vi", name: "Vietnamese", native: "Tiếng Việt" }
    ]

    function getAvailableLanguages() {
        var available = []
        var existing = TranslationManager.availableLanguages
        for (var i = 0; i < isoLanguages.length; i++) {
            var lang = isoLanguages[i]
            if (existing.indexOf(lang.code) === -1) {
                available.push(lang)
            }
        }
        return available
    }

    ColumnLayout {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: bottomBar.top
        anchors.margins: Theme.spacingSmall
        spacing: Theme.spacingSmall

        // Header
        Text {
            Layout.fillWidth: true
            text: "Tap a language to add it"
            font: Theme.labelFont
            color: Theme.textSecondaryColor
            horizontalAlignment: Text.AlignHCenter
        }

        GridView {
            id: languageGrid
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.topMargin: Theme.spacingMedium
            clip: true
            cellWidth: width / 2
            cellHeight: 72
            model: getAvailableLanguages()

            delegate: Item {
                width: languageGrid.cellWidth
                height: languageGrid.cellHeight

                Rectangle {
                    anchors.fill: parent
                    anchors.margins: 6
                    radius: Theme.buttonRadius
                    color: langMouseArea.pressed ? Theme.primaryColor : Theme.surfaceColor
                    border.width: 1
                    border.color: Qt.rgba(1, 1, 1, 0.3)

                    Accessible.role: Accessible.Button
                    Accessible.name: modelData.name + ", " + modelData.native
                    Accessible.description: TranslationManager.translate("addlanguage.accessible.add", "Add") + " " + modelData.name + " " + TranslationManager.translate("language.accessible.language", "language")
                    Accessible.onPressAction: langMouseArea.clicked(null)

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 10

                        Text {
                            text: modelData.code.toUpperCase()
                            font.pixelSize: 14
                            font.family: "monospace"
                            font.bold: true
                            color: langMouseArea.pressed ? "white" : Theme.primaryColor
                            Layout.preferredWidth: 30
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2

                            Text {
                                text: modelData.name
                                font: Theme.bodyFont
                                color: langMouseArea.pressed ? "white" : Theme.textColor
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }

                            Text {
                                text: modelData.native
                                font: Theme.labelFont
                                color: langMouseArea.pressed ? Qt.rgba(1,1,1,0.8) : Theme.textSecondaryColor
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                        }
                    }

                    MouseArea {
                        id: langMouseArea
                        anchors.fill: parent
                        onClicked: {
                            TranslationManager.addLanguage(modelData.code, modelData.name, modelData.native)
                            addLanguagePage.StackView.view.pop()
                        }
                    }
                }
            }
        }
    }

    // Bottom bar
    BottomBar {
        id: bottomBar
        title: "Add Language"
        onBackClicked: addLanguagePage.StackView.view.pop()
    }
}
