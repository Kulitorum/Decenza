import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decenza

// "Search Loffee Labs Bean Base" — async autocomplete against Visualizer's
// canonical coffee-bag endpoint (keyless) via MainController.beanbase;
// debounce + cache live in C++, so every keystroke may safely call search().
//
// Purely presentational: emits entrySelected(entry) / unlinkRequested() and
// renders the linked indicator; the parent owns applying fields and storing
// the link (live DYE state on the Beans page; the shot's snapshot on the
// review page and in edit mode).
//
// The label is the verbatim Loffee Labs branding, matching Visualizer's UI —
// deliberately not translated.
Item {
    id: root

    // Linked state (parent-driven). When linked, the input shows linkedLabel
    // and typing transitions back to search mode via unlinkRequested().
    property bool linked: false
    property string linkedLabel: ""
    property string linkedUrl: ""

    // Search runs through Visualizer's open canonical autocomplete — no key,
    // no account. Kept as a switch only for future gating needs.
    property bool searchEnabled: true

    property alias textField: searchInput  // For KeyboardAwareContainer registration

    signal entrySelected(var entry)
    signal unlinkRequested()

    // "idle" | "loading" | "error"; errorToken carries the C++ status token.
    property string searchState: "idle"
    property string errorToken: ""
    property var results: []

    implicitHeight: headerRow.height + Theme.scaled(2) + searchInput.height

    // The query string whose results we're waiting for (stale-result guard).
    property string _pendingQuery: ""
    // Suppress the unlink-on-edit signal while we programmatically set text.
    property bool _settingTextProgrammatically: false

    function _setInputText(t) {
        _settingTextProgrammatically = true
        searchInput.text = t
        _settingTextProgrammatically = false
    }

    // Programmatic pre-fill (edit-bag link flow): seed the input with a
    // likely query so one tap on the field searches it. Does NOT search.
    function prefill(q) {
        if (linked) return
        _setInputText(q)
    }

    // Pre-fill AND run the search immediately — the results popup opens on
    // arrival even while the field is unfocused ("Find in Bean Base" on the
    // bag card lands here).
    property bool _openOnResult: false
    function prefillAndSearch(q) {
        if (linked) return
        _setInputText(q)
        const t = q.trim()
        if (t.length < 2) return
        _pendingQuery = t.toLowerCase()
        searchState = "loading"
        _openOnResult = true
        MainController.beanbase.search(t)
    }

    onLinkedChanged: {
        if (linked) {
            resultsPopup.close()
            _setInputText(linkedLabel)
        } else if (!searchInput.activeFocus) {
            _setInputText("")
        }
    }
    onLinkedLabelChanged: if (linked) _setInputText(linkedLabel)
    Component.onCompleted: if (linked) _setInputText(linkedLabel)
    onVisibleChanged: if (!visible) resultsPopup.close()

    // Header: branding label + linked indicator / actions
    RowLayout {
        id: headerRow
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        spacing: Theme.scaled(8)

        Text {
            text: "Search Loffee Labs Bean Base"
            color: Theme.textSecondaryColor
            font.pixelSize: Theme.scaled(12)
        }

        Item { Layout.fillWidth: true }

        // ✓ Linked indicator (text, not glyph-icon — uses themed colors)
        Text {
            id: linkedIndicator
            visible: root.linked
            text: TranslationManager.translate("beaninfo.beanbase.linked", "Linked")
            color: Theme.successColor
            font.pixelSize: Theme.scaled(12)
            font.bold: true
        }

        Text {
            id: unlinkButton
            visible: root.linked
            text: TranslationManager.translate("beaninfo.beanbase.unlink", "Unlink")
            color: Theme.primaryColor
            font.pixelSize: Theme.scaled(12)

            AccessibleMouseArea {
                anchors.fill: parent
                anchors.margins: -Theme.scaled(6)  // Bigger touch target
                accessibleName: TranslationManager.translate("beaninfo.beanbase.unlinkAccessible", "Unlink bean from Bean Base")
                accessibleItem: unlinkButton
                onAccessibleClicked: {
                    root.unlinkRequested()
                    root._setInputText("")
                }
            }
        }

        Text {
            id: openLinkButton
            visible: root.linked && root.linkedUrl.length > 0
            text: TranslationManager.translate("beaninfo.beanbase.openUrl", "View at roaster")
            color: Theme.primaryColor
            font.pixelSize: Theme.scaled(12)

            AccessibleMouseArea {
                anchors.fill: parent
                anchors.margins: -Theme.scaled(6)
                accessibleName: TranslationManager.translate("beaninfo.beanbase.openUrlAccessible", "View bean at roaster website. Opens web browser")
                accessibleItem: openLinkButton
                onAccessibleClicked: Qt.openUrlExternally(root.linkedUrl)
            }
        }
    }

    StyledTextField {
        id: searchInput
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: headerRow.bottom
        anchors.topMargin: Theme.scaled(2)
        height: Theme.scaled(48)
        enabled: root.searchEnabled
        opacity: root.searchEnabled ? 1.0 : 0.6
        placeholder: TranslationManager.translate("beaninfo.beanbase.placeholder", "Search by roaster or bean name")
        accessibleName: "Search Loffee Labs Bean Base"
        inputMethodHints: Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase
        rightPadding: Theme.scaled(44)

        // displayText includes IME preedit — fires per keystroke on Android
        // (see SuggestionField for the full rationale).
        onDisplayTextChanged: {
            if (root._settingTextProgrammatically) return
            if (!activeFocus) return

            // Typing while linked re-enters search mode (spec: the link is
            // never a one-way door).
            if (root.linked) root.unlinkRequested()

            const q = displayText.trim()
            if (q.length < 2) {
                resultsPopup.close()
                root.searchState = "idle"
                return
            }
            root._pendingQuery = q.toLowerCase()
            root.searchState = "loading"
            MainController.beanbase.search(q)
        }

        onActiveFocusChanged: {
            if (!activeFocus) Qt.callLater(function() {
                if (!searchInput.activeFocus) resultsPopup.close()
            })
        }

        Keys.onEscapePressed: { resultsPopup.close(); focus = false }

        // Busy spinner while a request is in flight or debounce-queued
        BusyIndicator {
            anchors.right: parent.right
            anchors.rightMargin: Theme.scaled(8)
            anchors.verticalCenter: parent.verticalCenter
            width: Theme.scaled(24)
            height: Theme.scaled(24)
            running: root.searchState === "loading"
            visible: running
            Accessible.ignored: true
        }
    }

    Connections {
        target: MainController.beanbase

        function onSearchResults(query, entries) {
            // Drop stale responses (C++ aborts superseded requests, but a
            // response can still race the user's next keystroke).
            if (query.toLowerCase() !== root._pendingQuery) return
            root.results = entries
            root.searchState = "idle"
            // Open even when empty: the no-matches message lives in the popup.
            // _openOnResult covers programmatic prefillAndSearch (no focus yet).
            if (searchInput.activeFocus || root._openOnResult) {
                root._openOnResult = false
                resultsPopup.open()
            }
        }

        function onSearchFailed(query, status) {
            if (query.toLowerCase() !== root._pendingQuery) return
            // Superseded = a newer query (possibly from another consumer of
            // the shared client) replaced this one; not an error, just stop
            // the spinner and wait for the newer answer.
            if (status === "superseded") {
                root.searchState = "idle"
                return
            }
            root.results = []
            root.errorToken = status
            root.searchState = "error"
            if (searchInput.activeFocus) resultsPopup.open()
        }
    }

    Popup {
        id: resultsPopup
        x: searchInput.x
        y: searchInput.y + searchInput.height
        width: searchInput.width
        implicitHeight: Math.min(resultsList.contentHeight + Theme.scaled(2), Theme.scaled(280))
        padding: 1
        closePolicy: Popup.CloseOnPressOutside

        background: Rectangle {
            color: Theme.surfaceColor
            border.color: Theme.borderColor
            radius: Theme.scaled(4)
        }

        contentItem: ListView {
            id: resultsList
            clip: true
            model: root.searchState === "error" ? [] : root.results

            delegate: ItemDelegate {
                id: resultDelegate
                width: resultsList.width
                height: Theme.scaled(48)

                property var entry: modelData

                contentItem: RowLayout {
                    spacing: Theme.scaled(8)

                    Text {
                        Layout.fillWidth: true
                        // Visualizer's format: "RoastName (Roaster)"
                        text: resultDelegate.entry.roastName + " (" + resultDelegate.entry.roasterName + ")"
                              + (resultDelegate.entry.soldout ? " — " + TranslationManager.translate("beaninfo.beanbase.soldout", "sold out") : "")
                        color: resultDelegate.entry.soldout ? Theme.textSecondaryColor : Theme.textColor
                        font.pixelSize: Theme.scaled(16)
                        elide: Text.ElideRight
                        verticalAlignment: Text.AlignVCenter
                        leftPadding: Theme.scaled(12)
                    }

                    // Source chip — same "Bean Base" annotation the Change
                    // Beans search shows, so the edit-link search is labelled
                    // consistently.
                    Rectangle {
                        Layout.alignment: Qt.AlignVCenter
                        Layout.rightMargin: Theme.scaled(10)
                        implicitWidth: chipLabel.implicitWidth + Theme.scaled(16)
                        implicitHeight: chipLabel.implicitHeight + Theme.scaled(8)
                        radius: height / 2
                        color: Theme.backgroundColor
                        border.width: 1
                        border.color: Theme.textSecondaryColor

                        Text {
                            id: chipLabel
                            anchors.centerIn: parent
                            text: TranslationManager.translate("changebeans.source.beanbase", "Bean Base")
                            font: Theme.captionFont
                            color: Theme.textSecondaryColor
                        }
                    }
                }

                background: Rectangle {
                    color: resultDelegate.hovered ? Theme.primaryColor : "transparent"
                    opacity: resultDelegate.hovered ? 0.2 : 1
                }

                Accessible.role: Accessible.Button
                Accessible.name: resultDelegate.entry.roastName + ", " + resultDelegate.entry.roasterName
                Accessible.focusable: true
                Accessible.onPressAction: resultDelegate.clicked()

                onClicked: {
                    resultsPopup.close()
                    // Defer focus shift + IME hide (iOS QIOSTapRecognizer race
                    // — see SuggestionField.selectSuggestion for chapter & verse).
                    Qt.callLater(function() {
                        root.forceActiveFocus()
                        Qt.inputMethod.hide()
                    })
                    root.entrySelected(resultDelegate.entry)
                }
            }

            // Empty / error states
            Text {
                anchors.centerIn: parent
                width: parent.width - Theme.scaled(24)
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                visible: resultsList.count === 0
                color: Theme.textSecondaryColor
                font.pixelSize: Theme.scaled(14)
                text: {
                    if (root.searchState === "error") {
                        // invalid/ratelimited/quota are unreachable from the
                        // keyless canonical path — kept as defensive cover
                        // for a future searchBeanBase()-backed mode.
                        if (root.errorToken === "invalid")
                            return TranslationManager.translate("beaninfo.beanbase.errorInvalid", "Invalid API key — check Settings")
                        if (root.errorToken === "ratelimited")
                            return TranslationManager.translate("beaninfo.beanbase.errorRateLimited", "Searching too fast — pause a moment and try again")
                        if (root.errorToken === "quota")
                            return TranslationManager.translate("beaninfo.beanbase.errorQuota", "Daily Bean Base limit reached — search resumes tomorrow")
                        if (root.errorToken === "parse")
                            return TranslationManager.translate("beaninfo.beanbase.errorParse", "Bean search is temporarily unavailable")
                        return TranslationManager.translate("beaninfo.beanbase.errorNetwork", "Could not reach the bean database")
                    }
                    return TranslationManager.translate("beaninfo.beanbase.noMatches", "No matches — your bean may not be in the community database yet")
                }
            }
        }
    }
}
