// The results-list delegate reads this file's ids (`searchDialog`, `resultsList`);
// Bound makes them statically resolvable. It declares its injected roles, `modelData`
// and `index`, required -- without that, Bound stops role injection and every search
// result renders blank at RUNTIME, silently.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decenza
import "../../components/SettingsSearchEntries.js" as SearchEntries
import "../../components/SettingsSearchMatcher.mjs" as SearchMatcher

DecenzaDialog {
    id: searchDialog
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(parent.width * 0.85, Theme.scaled(450))
    height: Math.min(parent.height * 0.7, Theme.scaled(500))
    modal: true
    dim: true
    padding: Theme.scaled(16)
    closePolicy: Dialog.CloseOnEscape | Dialog.CloseOnPressOutside

    background: Rectangle {
        color: Theme.surfaceColor
        radius: Theme.cardRadius
        border.color: Theme.borderColor
        border.width: 1
    }

    // aboutToShow, not opened: opened() waits for the enter transition (qquickpopup.cpp:845),
    // so the old list would show through the zoom-in and then jump.
    onAboutToShow: {
        recentEntries = matcher.lookup(SettingsSearchRegistry.recentIds)
        searchField.setTextSilently("")
    }
    onOpened: searchField.field.forceActiveFocus()

    // Rebuilt when the language or an availability condition changes, never per keystroke.
    readonly property var matcher: {
        var translate = TranslationManager.translate.bind(TranslationManager)
        return SearchMatcher.createMatcher(SearchMatcher.buildItems(SearchEntries.entries, translate,
                                                                    SettingsSearchRegistry.isAvailable))
    }
    readonly property int totalCount: matcher.search("").length
    // `displayText`, not `text`: re-evaluates on every IME preedit change on Android, not only
    // after the IME commits the word.
    readonly property var filteredEntries: matcher.search(searchField.field.displayText)
    readonly property bool browsing: filteredEntries.length === totalCount
    // Snapshotted on open, not bound to the registry: noteRecent() runs in a delegate's click
    // handler, and a model reset would clear that delegate's context mid-handler
    // (destroyObjectLater(), qqmldelegatemodel.cpp:2581).
    property var recentEntries: []
    readonly property var shownRecents: browsing ? recentEntries : []

    // `result` is a SettingsSearchMatcher item: {kind, tabId, cardId, externalRoute, title, ...}.
    signal resultSelected(var result)

    contentItem: ColumnLayout {
        spacing: Theme.scaled(12)

        // Search field
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.scaled(8)

            // ThemedIcon: the white-stroked search.svg sits on a light surface in light
            // mode and was invisible there. Same fix as SettingsPage.qml.
            ThemedIcon {
                source: "qrc:/icons/search.svg"
                iconSize: Theme.scaled(20)
                Layout.alignment: Qt.AlignVCenter
                Accessible.ignored: true
            }

            SearchField {
                id: searchField
                Layout.fillWidth: true
                placeholder: TranslationManager.translate("settings.search.placeholder", "Search settings")
                accessibleName: TranslationManager.translate("settings.search.placeholder", "Search settings")
            }
        }

        // With recents shown, the list carries its own headings.
        Text {
            visible: searchDialog.shownRecents.length === 0
            text: searchDialog.browsing
                ? TranslationManager.translate("settings.search.browseAll", "Browse all settings")
                : TranslationManager.translate("settings.search.resultsCount", "%1 results").arg(searchDialog.filteredEntries.length)
            color: Theme.textSecondaryColor
            font.pixelSize: Theme.scaled(11)
        }

        ListView {
            id: resultsList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            // Recents repeat in the full list below them.
            model: searchDialog.shownRecents.concat(searchDialog.filteredEntries)

            delegate: Column {
                id: resultDelegate
                required property var modelData
                required property int index

                width: resultsList.width

                readonly property string badgeLabel: modelData.externalRoute
                    ? TranslationManager.translate("settings.search.externalBadge", "Profiles")
                    : SettingsTabs.tabName(modelData.tabId)
                // Subtitle: the adjustment's card (if any), then the description.
                readonly property string context: [modelData.cardTitle, modelData.description]
                    .filter(function(s) { return s.length > 0 }).join(" · ")

                readonly property string heading: searchDialog.shownRecents.length === 0 ? ""
                    : index === 0 ? TranslationManager.translate("settings.search.recent", "Recent")
                    : index === searchDialog.shownRecents.length
                    ? TranslationManager.translate("settings.search.browseAll", "Browse all settings") : ""

                Text {
                    visible: text.length > 0
                    topPadding: resultDelegate.index > 0 ? Theme.scaled(8) : 0
                    bottomPadding: Theme.scaled(4)
                    text: resultDelegate.heading
                    color: Theme.textSecondaryColor
                    font.pixelSize: Theme.scaled(11)
                    Accessible.role: Accessible.Heading
                    Accessible.name: text
                }

                Rectangle {
                    width: parent.width
                    height: resultContent.implicitHeight + Theme.scaled(16)
                    color: resultMouseArea.containsMouse ? Theme.backgroundColor : "transparent"
                    radius: Theme.scaled(6)

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
                                text: resultDelegate.modelData.title
                                color: Theme.textColor
                                font.pixelSize: Theme.scaled(14)
                                font.bold: true
                                Accessible.ignored: true
                            }

                            Text {
                                Layout.fillWidth: true
                                visible: text.length > 0
                                text: resultDelegate.context
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
                                text: resultDelegate.badgeLabel
                                color: Theme.primaryColor
                                font.pixelSize: Theme.scaled(10)
                                font.bold: true
                                Accessible.ignored: true
                            }
                        }
                    }

                    AccessibleMouseArea {
                        id: resultMouseArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        accessibleName: [resultDelegate.modelData.title, resultDelegate.modelData.cardTitle,
                                         resultDelegate.badgeLabel].filter(function(s) { return s.length > 0 }).join(", ")
                        onAccessibleClicked: {
                            SettingsSearchRegistry.noteRecent(resultDelegate.modelData.id)
                            searchDialog.resultSelected(resultDelegate.modelData)
                            searchDialog.close()
                        }
                    }
                }
            }

            // Empty state
            Text {
                // `parent` here is the ListView's contentItem (qquickflickable.cpp:2462),
                // which is zero-high exactly when the list is empty -- centring on it put
                // this message half above the clipped top edge.
                anchors.horizontalCenter: parent.horizontalCenter
                y: (resultsList.height - height) / 2
                visible: searchDialog.filteredEntries.length === 0
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
