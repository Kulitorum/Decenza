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
        { code: "ar", name: "Arabic", nativeName: "العربية" },
        { code: "bg", name: "Bulgarian", nativeName: "Български" },
        { code: "ca", name: "Catalan", nativeName: "Català" },
        { code: "zh", name: "Chinese", nativeName: "中文" },
        { code: "hr", name: "Croatian", nativeName: "Hrvatski" },
        { code: "cs", name: "Czech", nativeName: "Čeština" },
        { code: "da", name: "Danish", nativeName: "Dansk" },
        { code: "nl", name: "Dutch", nativeName: "Nederlands" },
        { code: "et", name: "Estonian", nativeName: "Eesti" },
        { code: "fi", name: "Finnish", nativeName: "Suomi" },
        { code: "fr", name: "French", nativeName: "Français" },
        { code: "de", name: "German", nativeName: "Deutsch" },
        { code: "el", name: "Greek", nativeName: "Ελληνικά" },
        { code: "he", name: "Hebrew", nativeName: "עברית" },
        { code: "hi", name: "Hindi", nativeName: "हिन्दी" },
        { code: "hu", name: "Hungarian", nativeName: "Magyar" },
        { code: "id", name: "Indonesian", nativeName: "Bahasa Indonesia" },
        { code: "it", name: "Italian", nativeName: "Italiano" },
        { code: "ja", name: "Japanese", nativeName: "日本語" },
        { code: "ko", name: "Korean", nativeName: "한국어" },
        { code: "lv", name: "Latvian", nativeName: "Latviešu" },
        { code: "lt", name: "Lithuanian", nativeName: "Lietuvių" },
        { code: "ms", name: "Malay", nativeName: "Bahasa Melayu" },
        { code: "nb", name: "Norwegian", nativeName: "Norsk bokmål" },
        { code: "fa", name: "Persian", nativeName: "فارسی" },
        { code: "pl", name: "Polish", nativeName: "Polski" },
        { code: "pt", name: "Portuguese", nativeName: "Português" },
        { code: "ro", name: "Romanian", nativeName: "Română" },
        { code: "ru", name: "Russian", nativeName: "Русский" },
        { code: "sr", name: "Serbian", nativeName: "Српски" },
        { code: "sk", name: "Slovak", nativeName: "Slovenčina" },
        { code: "sl", name: "Slovenian", nativeName: "Slovenščina" },
        { code: "es", name: "Spanish", nativeName: "Español" },
        { code: "sv", name: "Swedish", nativeName: "Svenska" },
        { code: "th", name: "Thai", nativeName: "ไทย" },
        { code: "tr", name: "Turkish", nativeName: "Türkçe" },
        { code: "uk", name: "Ukrainian", nativeName: "Українська" },
        { code: "vi", name: "Vietnamese", nativeName: "Tiếng Việt" }
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
                    anchors.margins: Theme.scaled(6)
                    radius: Theme.buttonRadius
                    color: langMouseArea.pressed ? Theme.primaryColor : Theme.surfaceColor
                    border.width: 1
                    border.color: Qt.rgba(1, 1, 1, 0.3)

                    Accessible.role: Accessible.Button
                    Accessible.name: modelData.name + ", " + modelData.nativeName
                    Accessible.description: TranslationManager.translate("addlanguage.accessible.add", "Add") + " " + modelData.name + " " + TranslationManager.translate("language.accessible.language", "language")
                    Accessible.focusable: true
                    Accessible.onPressAction: langMouseArea.clicked(null)

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: Theme.scaled(10)
                        spacing: Theme.scaled(10)

                        Text {
                            text: modelData.code.toUpperCase()
                            font.pixelSize: Theme.scaled(14)
                            font.family: "monospace"
                            font.bold: true
                            color: langMouseArea.pressed ? "white" : Theme.primaryColor
                            Layout.preferredWidth: Theme.scaled(30)
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: Theme.scaled(2)

                            Text {
                                text: modelData.name
                                font: Theme.bodyFont
                                color: langMouseArea.pressed ? "white" : Theme.textColor
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }

                            Text {
                                text: modelData.nativeName
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
                            TranslationManager.addLanguage(modelData.code, modelData.name, modelData.nativeName)
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
