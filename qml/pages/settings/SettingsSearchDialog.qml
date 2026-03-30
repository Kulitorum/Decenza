import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decenza
import "../../components"
import "../../components/SettingsSearchIndex.js" as SearchIndex

Dialog {
    id: searchDialog
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(parent.width * 0.85, Theme.scaled(450))
    height: Math.min(parent.height * 0.7, Theme.scaled(500))
    modal: true
    dim: true
    padding: Theme.scaled(16)
    closePolicy: Dialog.CloseOnEscape | Dialog.CloseOnPressOutside

    signal resultSelected(int tabIndex, string cardId)

    background: Rectangle {
        color: Theme.surfaceColor
        radius: Theme.cardRadius
        border.color: Theme.borderColor
        border.width: 1
    }

    onOpened: {
        searchField.text = ""
        searchField.forceActiveFocus()
    }

    // Levenshtein distance for fuzzy matching
    function editDistance(a, b) {
        if (a.length === 0) return b.length
        if (b.length === 0) return a.length
        var matrix = []
        for (var i = 0; i <= b.length; i++) matrix[i] = [i]
        for (var j = 0; j <= a.length; j++) matrix[0][j] = j
        for (i = 1; i <= b.length; i++) {
            for (j = 1; j <= a.length; j++) {
                var cost = a[j - 1] === b[i - 1] ? 0 : 1
                matrix[i][j] = Math.min(
                    matrix[i - 1][j] + 1,
                    matrix[i][j - 1] + 1,
                    matrix[i - 1][j - 1] + cost
                )
            }
        }
        return matrix[b.length][a.length]
    }

    // Check if queryWord fuzzy-matches any word in targetWords
    function fuzzyWordMatch(queryWord, targetWords) {
        var maxDist = queryWord.length <= 3 ? 0 : (queryWord.length <= 5 ? 1 : 2)
        for (var i = 0; i < targetWords.length; i++) {
            var tw = targetWords[i]
            // Exact substring still works
            if (tw.indexOf(queryWord) !== -1) return true
            // Fuzzy: compare against words of similar length
            if (maxDist > 0 && Math.abs(tw.length - queryWord.length) <= maxDist) {
                if (editDistance(queryWord, tw) <= maxDist) return true
            }
            // Fuzzy: check if query is a fuzzy prefix of a longer word
            if (maxDist > 0 && tw.length > queryWord.length) {
                var prefix = tw.substring(0, queryWord.length + maxDist)
                if (editDistance(queryWord, prefix) <= maxDist) return true
            }
        }
        return false
    }

    // Rebuild entries when language changes
    property int _langVersion: TranslationManager.translationVersion

    // Filter results based on search text (with fuzzy matching)
    property var allEntries: {
        var v = _langVersion  // Force re-evaluation on language change
        return SearchIndex.getSearchEntries(TranslationManager.translate.bind(TranslationManager))
    }
    property var filteredEntries: {
        var query = searchField.text.trim().toLowerCase()
        if (query.length === 0) return allEntries

        var results = []
        var queryWords = query.split(/\s+/)

        for (var i = 0; i < allEntries.length; i++) {
            var entry = allEntries[i]
            var searchText = (entry.title + " " + entry.description + " " + entry.keywords.join(" ")).toLowerCase()

            // Fast path: exact substring match for all query words
            var allExact = true
            for (var w = 0; w < queryWords.length; w++) {
                if (searchText.indexOf(queryWords[w]) === -1) {
                    allExact = false
                    break
                }
            }
            if (allExact) { results.push(entry); continue }

            // Slow path: fuzzy word matching
            var targetWords = searchText.split(/\s+/)
            var allFuzzy = true
            for (w = 0; w < queryWords.length; w++) {
                if (!fuzzyWordMatch(queryWords[w], targetWords)) {
                    allFuzzy = false
                    break
                }
            }
            if (allFuzzy) results.push(entry)
        }
        return results
    }

    contentItem: ColumnLayout {
        spacing: Theme.scaled(12)

        // Search field
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.scaled(8)

            Image {
                source: "qrc:/icons/search.svg"
                sourceSize.width: Theme.scaled(20)
                sourceSize.height: Theme.scaled(20)
                Layout.alignment: Qt.AlignVCenter
                visible: source != ""
                Accessible.ignored: true
            }

            StyledTextField {
                id: searchField
                Layout.fillWidth: true
                placeholder: TranslationManager.translate("settings.search.placeholder", "Search settings...")
                accessibleName: TranslationManager.translate("settings.search.placeholder", "Search settings")
            }
        }

        // Results count
        Text {
            text: filteredEntries.length === allEntries.length
                ? TranslationManager.translate("settings.search.browseAll", "Browse all settings")
                : TranslationManager.translate("settings.search.resultsCount", "%1 results").arg(filteredEntries.length)
            color: Theme.textSecondaryColor
            font.pixelSize: Theme.scaled(11)
        }

        // Results list
        ListView {
            id: resultsList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: filteredEntries

            delegate: Rectangle {
                id: resultDelegate
                width: resultsList.width
                height: resultContent.implicitHeight + Theme.scaled(16)
                color: resultMouseArea.containsMouse ? Theme.backgroundColor : "transparent"
                radius: Theme.scaled(6)

                Accessible.role: Accessible.Button
                Accessible.name: modelData.title + ", " + SearchIndex.getTabName(modelData.tabIndex, TranslationManager.translate.bind(TranslationManager)) + " tab"
                Accessible.focusable: true
                Accessible.onPressAction: resultMouseArea.clicked(null)

                RowLayout {
                    id: resultContent
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.margins: Theme.scaled(8)
                    spacing: Theme.scaled(8)

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(2)

                        Text {
                            text: modelData.title
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(14)
                            font.bold: true
                            Accessible.ignored: true
                        }

                        Text {
                            Layout.fillWidth: true
                            text: modelData.description
                            color: Theme.textSecondaryColor
                            font.pixelSize: Theme.scaled(11)
                            wrapMode: Text.WordWrap
                            Accessible.ignored: true
                        }
                    }

                    // Tab badge
                    Rectangle {
                        Layout.alignment: Qt.AlignVCenter
                        implicitWidth: tabBadgeText.implicitWidth + Theme.scaled(12)
                        implicitHeight: Theme.scaled(20)
                        radius: Theme.scaled(10)
                        color: Qt.rgba(Theme.primaryColor.r, Theme.primaryColor.g, Theme.primaryColor.b, 0.15)

                        Text {
                            id: tabBadgeText
                            anchors.centerIn: parent
                            text: SearchIndex.getTabName(modelData.tabIndex, TranslationManager.translate.bind(TranslationManager))
                            color: Theme.primaryColor
                            font.pixelSize: Theme.scaled(10)
                            font.bold: true
                            Accessible.ignored: true
                        }
                    }
                }

                MouseArea {
                    id: resultMouseArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        searchDialog.resultSelected(modelData.tabIndex, modelData.cardId || "")
                        searchDialog.close()
                    }
                }
            }

            // Empty state
            Text {
                anchors.centerIn: parent
                visible: filteredEntries.length === 0
                text: TranslationManager.translate("settings.search.noResults", "No settings found")
                color: Theme.textSecondaryColor
                font.pixelSize: Theme.scaled(14)
            }
        }

        // Close button
        AccessibleButton {
            Layout.alignment: Qt.AlignRight
            text: TranslationManager.translate("common.button.close", "Close")
            accessibleName: TranslationManager.translate("settings.search.close", "Close search")
            onClicked: searchDialog.close()
        }
    }
}
