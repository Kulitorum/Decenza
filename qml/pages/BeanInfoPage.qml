// The bag-card Repeater delegate below reads this file's ids (`flickable`,
// `changeBeansDialog`); Bound makes them statically resolvable. The delegate declares
// its one injected model role required in the same edit -- without that, Bound stops
// role injection and `modelData` goes undefined at RUNTIME, silently.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Templates as T
import QtQuick.Layouts
import Decenza
import "../components/RecipeSearch.js" as RecipeSearch

// Bean bag inventory (bean-bag-inventory change): replaces the old editable
// DYE-fields + presets page. Shows all bags with inInventory = true as cards;
// tapping a card selects it (sets activeBagId), and the Change Beans dialog
// handles Bean Base search and creation. A search field filters both shelves. Finished bags sit behind "Show finished (N)", as
// archived recipes do on the Recipes page. There are no editable bean text fields here —
// bag edits go through the dialog's Edit Bag form.
T.Page {
    id: bagInventoryPage
    // Declarative so it re-evaluates on a language change. This used to be an
    // imperative assignment in onCompleted/onActivated, which ran once and left
    // page titles in the previous language until you navigated away and back.
    readonly property string pageTitle: TranslationManager.translate("beaninfo.title", "Beans")

    objectName: "bagInventoryPage"
    background: ThemedPageBackground {}


    property var inventoryBags: []
    // "No bags yet" is a fact about the DATABASE, and until the async read
    // answers we do not have it. Rendering the empty state from the initial []
    // told every user with bags that they had none, for as long as the read
    // took — the page opened on its own failure message and then corrected
    // itself.
    // "loading" until the read terminates, then "ready" or "failed". A failed
    // read is not an empty one: rendering the empty state for it would claim
    // the user has no bags, and rendering nothing is worse still.
    property string inventoryState: "loading"
    // Finished bags: the count drives the toggle; the list loads while shown or
    // searched, and again after a bag change. finishedState is "idle" (not
    // loaded, or stale), "loading", "ready" or "failed" -- as with the open
    // bags, a failed read must not read as "no matches".
    property int finishedCount: 0
    property var finishedBags: []
    property bool showFinished: false
    property string finishedState: "idle"
    readonly property bool needFinished: showFinished || searchQuery.length > 0
    onNeedFinishedChanged: if (needFinished && finishedState !== "ready") loadFinished()
    function loadFinished() {
        finishedState = "loading"
        MainController.bagStorage.requestFinishedBags()
    }

    // Search + sort, as on Recipes. A search covers every text value a bag holds
    // (RecipeSearch.buildBagHaystack) and the finished bags too. The sort
    // persists; the search resets on entry.
    property string searchQuery: ""
    property string sortField: Settings.network.bagSortField
    property string sortDirection: Settings.network.bagSortDirection
    readonly property var sortFieldLabels: ({
        "dateUsed": TranslationManager.translate("beaninfo.sort.dateUsed", "Last used"),
        "roastDate": TranslationManager.translate("beaninfo.sort.roastDate", "Roast date"),
        "coffee": TranslationManager.translate("beaninfo.sort.coffee", "Coffee"),
        "roaster": TranslationManager.translate("beaninfo.sort.roaster", "Roaster")
    })
    readonly property var sortFieldKeys: ["dateUsed", "roastDate", "coffee", "roaster"]
    readonly property var defaultSortDirections: ({
        "dateUsed": "DESC", "roastDate": "DESC", "coffee": "ASC", "roaster": "ASC"
    })
    readonly property var visibleBags: filterAndSort(inventoryBags, searchQuery, sortField, sortDirection)
    readonly property var visibleFinishedBags: filterAndSort(finishedBags, searchQuery, sortField, sortDirection)
    // With a search on, the count is of the matching finished bags, once known.
    readonly property int shownFinishedCount: searchQuery.length === 0 ? finishedCount
        : (finishedState === "ready" ? visibleFinishedBags.length : 0)

    function _sortKey(bag, field) {
        if (field === "roastDate")
            return String(bag.roastDate || "")   // ISO dates order as text
        if (field === "coffee")
            return String(bag.coffeeName || "").toLowerCase()
        if (field === "roaster")
            return String(bag.roasterName || "").toLowerCase()
        return Number(bag.lastUsedEpoch) || 0   // dateUsed (default)
    }

    function filterAndSort(list, query, field, dir) {
        const tokens = RecipeSearch.tokenize(query)
        const tea = TranslationManager.translate("beaninfo.kind.tea", "Tea")
        const coffee = TranslationManager.translate("beaninfo.kind.coffee", "Coffee")
        const out = tokens.length === 0 ? list : list.filter(function(bag) {
            const hay = RecipeSearch.buildBagHaystack(bag, bag.kind === "tea" ? tea : coffee)
            return RecipeSearch.matches(hay, tokens)
        })
        return RecipeSearch.sortedCopy(out, function(bag) { return _sortKey(bag, field) }, dir)
    }

    T.StackView.onActivated: searchBar.clear()

    Component.onCompleted: {
        MainController.bagStorage.requestInventory()
        MainController.bagStorage.requestFinishedBagCount()
        // The bags as Visualizer holds them now; changes arrive through
        // onBagsChanged below.
        MainController.visualizerSync.refreshBags()
        addBagButton.forceActiveFocus()
    }

    Connections {
        target: MainController.bagStorage
        function onInventoryReady(bags) {
            bagInventoryPage.inventoryBags = bags
            bagInventoryPage.inventoryState = "ready"
        }
        function onInventoryFailed() {
            bagInventoryPage.inventoryState = "failed"
        }
        function onFinishedBagsReady(bags) {
            bagInventoryPage.finishedBags = bags
            bagInventoryPage.finishedState = "ready"
        }
        function onFinishedBagsFailed() {
            bagInventoryPage.finishedState = "failed"
        }
        function onFinishedBagCountReady(count) {
            bagInventoryPage.finishedCount = count
        }
        function onBagsChanged() {
            MainController.bagStorage.requestInventory()
            MainController.bagStorage.requestFinishedBagCount()
            if (bagInventoryPage.needFinished)
                bagInventoryPage.loadFinished()
            else
                bagInventoryPage.finishedState = "idle"
        }
    }

    ChangeBeansDialog {
        id: changeBeansDialog
        context: "inventory"
    }

    // The search field is the page's only text input; this gives it the
    // keyboard handling (and tap-outside dismissal) every text page gets.
    KeyboardAwareContainer {
        anchors.fill: parent
        textFields: [searchBar.field]
        targetFlickable: flickable

        Flickable {
            id: flickable
            anchors.fill: parent
            anchors.topMargin: Theme.pageTopMargin
            anchors.bottomMargin: Theme.bottomBarHeight
            anchors.leftMargin: Theme.standardMargin
            anchors.rightMargin: Theme.standardMargin
            contentHeight: contentColumn.implicitHeight + Theme.scaled(20)
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            ColumnLayout {
                id: contentColumn
                width: flickable.width
                spacing: Theme.spacingMedium

                // Header row: title + Add New Bag
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingMedium

                    Tr {
                        key: "beaninfo.inventory.title"
                        fallback: "Bag Inventory"
                        font: Theme.titleFont
                        color: Theme.textColor
                        Accessible.role: Accessible.Heading
                        Accessible.name: text
                    }

                    Item { Layout.fillWidth: true }

                    // Two creation entry points (add-recipe-wizard-tea): the kind
                    // is stamped at creation, so coffee and tea get their own
                    // buttons and flows — coffee keeps the Bean Base search-first
                    // dialog; tea searches past tea bags only (straight to the
                    // form when there are none). Shown as "Add [Bag of Coffee] [Bag
                    // of Tea]": one "Add" label, then a matched pair with each kind's
                    // icon. Screen readers get "Add a new bag of ..." from the buttons.
                    Tr {
                        key: "beaninfo.inventory.add"
                        fallback: "Add"
                        font: Theme.bodyFont
                        color: Theme.textColor
                        Accessible.ignored: true
                    }

                    AccessibleButton {
                        id: addBagButton
                        Layout.preferredHeight: Theme.scaled(44)
                        icon.source: "qrc:/icons/coffeebeans.svg"
                        text: TranslationManager.translate("beaninfo.inventory.bagOfCoffee", "Bag of Coffee")
                        accessibleName: TranslationManager.translate("beaninfo.inventory.accessible.addBag", "Add a new bag of beans")
                        onClicked: {
                            changeBeansDialog.bagKind = "coffee"
                            changeBeansDialog.open()
                        }
                    }

                    AccessibleButton {
                        id: addTeaButton
                        Layout.preferredHeight: Theme.scaled(44)
                        icon.source: "qrc:/icons/tea.svg"
                        text: TranslationManager.translate("beaninfo.inventory.bagOfTea", "Bag of Tea")
                        accessibleName: TranslationManager.translate("beaninfo.inventory.accessible.addTea", "Add a new bag of tea")
                        onClicked: {
                            var hasTea = false
                            for (let i = 0; i < bagInventoryPage.inventoryBags.length; ++i) {
                                if (String(bagInventoryPage.inventoryBags[i].kind || "") === "tea") {
                                    hasTea = true
                                    break
                                }
                            }
                            changeBeansDialog.openTeaEntry(hasTea)
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    visible: bagInventoryPage.inventoryBags.length > 0 || bagInventoryPage.finishedCount > 0
                    spacing: Theme.spacingSmall

                    SearchField {
                        id: searchBar
                        Layout.fillWidth: true
                        placeholder: TranslationManager.translate("beaninfo.searchPlaceholder", "Search bags...")
                        accessibleName: TranslationManager.translate("beaninfo.accessible.search", "Search bags")
                        onQueryChanged: function(query) { bagInventoryPage.searchQuery = query }
                    }

                    SortControls {
                        keys: bagInventoryPage.sortFieldKeys
                        labels: bagInventoryPage.sortFieldLabels
                        defaultDirections: bagInventoryPage.defaultSortDirections
                        field: bagInventoryPage.sortField
                        direction: bagInventoryPage.sortDirection
                        onSortChanged: function(field, direction) {
                            bagInventoryPage.sortField = field
                            bagInventoryPage.sortDirection = direction
                            Settings.network.bagSortField = field
                            Settings.network.bagSortDirection = direction
                        }
                    }
                }

                Text {
                    Layout.fillWidth: true
                    Layout.topMargin: Theme.spacingMedium
                    visible: bagInventoryPage.searchQuery.length > 0
                             && bagInventoryPage.inventoryState === "ready"
                             && bagInventoryPage.finishedState === "ready"
                             && bagInventoryPage.visibleBags.length === 0
                             && bagInventoryPage.visibleFinishedBags.length === 0
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    text: TranslationManager.translate("beaninfo.noMatches", "No bags match your search")
                    font: Theme.bodyFont
                    color: Theme.textSecondaryColor
                    Accessible.role: Accessible.StaticText
                    Accessible.name: text
                }

                // The read failed — say so rather than claiming there are no bags.
                Tr {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.topMargin: Theme.scaled(40)
                    visible: bagInventoryPage.inventoryState === "failed"
                             && bagInventoryPage.inventoryBags.length === 0
                    key: "beaninfo.inventory.unavailable"
                    fallback: "Couldn't read your bags — the bean database didn't open."
                    font: Theme.bodyFont
                    color: Theme.textSecondaryColor
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    Accessible.role: Accessible.StaticText
                    Accessible.name: text
                }

                // Empty state
                ColumnLayout {
                    visible: bagInventoryPage.inventoryState === "ready"
                             && bagInventoryPage.inventoryBags.length === 0
                    Layout.fillWidth: true
                    Layout.topMargin: Theme.scaled(40)
                    spacing: Theme.spacingSmall

                    Tr {
                        Layout.alignment: Qt.AlignHCenter
                        key: "beaninfo.inventory.empty.title"
                        fallback: "No bags yet"
                        font: Theme.subtitleFont
                        color: Theme.textColor
                        Accessible.role: Accessible.StaticText
                        Accessible.name: text
                    }

                    Tr {
                        Layout.alignment: Qt.AlignHCenter
                        Layout.maximumWidth: flickable.width * 0.8
                        key: "beaninfo.inventory.empty.hint"
                        fallback: "Add your first bag to track beans, freshness and grinder settings per bag."
                        font: Theme.bodyFont
                        color: Theme.textSecondaryColor
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                        Accessible.role: Accessible.StaticText
                        Accessible.name: text
                    }
                }

                // Bag cards
                Flow {
                    Layout.fillWidth: true
                    spacing: Theme.spacingMedium

                    Repeater {
                        model: bagInventoryPage.visibleBags

                        BagCard {
                            required property var modelData

                            bag: modelData
                            width: Theme.cardGridWidth(flickable.width)
                            onEditRequested: function(b) { changeBeansDialog.openForEdit(b) }
                            onLinkRequested: function(b) { changeBeansDialog.openForEditAndLink(b) }
                            onRestockRequested: function(b) { changeBeansDialog.openRestock(b) }
                        }
                    }
                }

                // The finished shelf could not be read: say so rather than hide it.
                Tr {
                    Layout.fillWidth: true
                    visible: bagInventoryPage.finishedState === "failed" && bagInventoryPage.needFinished
                    key: "beaninfo.finished.unavailable"
                    fallback: "Couldn't read your finished bags."
                    font: Theme.bodyFont
                    color: Theme.textSecondaryColor
                    wrapMode: Text.Wrap
                    Accessible.role: Accessible.StaticText
                    Accessible.name: text
                }

                AccessibleButton {
                    visible: bagInventoryPage.shownFinishedCount > 0
                    Layout.preferredHeight: Theme.scaled(36)   // Layout child: raw height is ignored
                    _customFontSize: Theme.captionFont.pixelSize
                    leftPadding: Theme.scaled(10)
                    rightPadding: Theme.scaled(10)
                    text: (bagInventoryPage.showFinished
                           ? TranslationManager.translate("beaninfo.finished.hide", "Hide finished")
                           : TranslationManager.translate("beaninfo.finished.show", "Show finished"))
                          + " (" + bagInventoryPage.shownFinishedCount + ")"
                    accessibleName: text
                    onClicked: bagInventoryPage.showFinished = !bagInventoryPage.showFinished
                }

                Flow {
                    visible: bagInventoryPage.showFinished
                    Layout.fillWidth: true
                    spacing: Theme.spacingMedium

                    Repeater {
                        model: bagInventoryPage.showFinished ? bagInventoryPage.visibleFinishedBags : []

                        BagCard {
                            required property var modelData

                            bag: modelData
                            finishedCard: true
                            width: Theme.cardGridWidth(flickable.width)
                            onEditRequested: function(b) { changeBeansDialog.openForEdit(b) }
                            onRestockRequested: function(b) { changeBeansDialog.openRestock(b) }
                        }
                    }
                }
            }
        }
    }

    BottomBar {
        barColor: "transparent"
        onBackClicked: AppShell.backRequested()
    }
}
