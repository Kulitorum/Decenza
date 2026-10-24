import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtTest
import Decenza 1.0
import "DrinkTypeCatalog.js" as DrinkTypeCatalog
import "SharedShotPlanFormat.js" as ShotPlanFormat

TestCase {
    id: testCase

    name: "SharedFormControls"
    when: windowShown
    // The conflict-label test inspects effective Item.visible, which is false
    // when a visual ancestor is hidden (Qt 6.12 qquickitem.cpp:6663–6666, 6762).
    visible: true
    width: 520
    height: 900

    property string parentValue: ""

    FormControlStyle {
        id: controlStyle
        font: Qt.font({ family: Qt.application.font.family, pixelSize: 16, weight: Font.Normal })
    }

    SharedSuggestionField {
        id: suggestionField
        x: 16
        y: 16
        width: 420
        controlStyle: controlStyle
        label: "Grinder"
        accessibleName: "Grinder model"
        text: testCase.parentValue
        suggestions: ["Baratza", "Baratza Encore", "Comandante", "DF64"]
        descriptions: ({ "Baratza Encore": "Conical burr" })
        onTextEdited: value => testCase.parentValue = value
    }

    SharedTextField {
        id: passwordField
        x: 16
        y: 100
        width: 420
        controlStyle: controlStyle
        accessibleName: "Account password"
        echoMode: TextInput.Password
        text: "not-for-accessibility"
    }

    SharedTextField {
        id: nestedFontOverrideField
        x: 16
        y: 170
        width: 420
        controlStyle: controlStyle
        font.bold: true
        font.pixelSize: 22
    }

    RecipeNameEditor {
        id: recipeNameEditor
        visible: true
        x: 16
        y: 245
        width: 420
        controlStyle: controlStyle
        labelText: "Recipe name *"
        placeholderText: "Recipe name"
        conflictText: "<b>Name already exists</b>"
        fieldAccessibleName: "Recipe name"
    }

    RecipeDrinkCardStyle {
        id: recipeCardStyle
        titleFont: Qt.font({ family: Qt.application.font.family, pixelSize: 18 })
        captionFont: Qt.font({ family: Qt.application.font.family, pixelSize: 14 })
        cardPadding: 12
        smallSpacing: 8
        borderColor: "#808080"
        accentColor: "#1769aa"
        warningColor: "#9a4139"
    }

    RecipeDrinkCardStyle { id: defaultRecipeCardStyle }
    FormControlStyle { id: defaultFormControlStyle }
    SharedShotPlanStyle { id: defaultShotPlanStyle }

    Component {
        id: beanThumbnailPlaceholder
        Rectangle {
            objectName: "beanThumbnailTestPlaceholder"
            color: "#808080"
        }
    }

    SharedBeanThumbnail {
        id: beanThumbnailView
        x: 16
        y: 515
        width: 56
        height: 56
        thumbnailStyle: recipeCardStyle
        imageUrl: ""
        placeholderComponent: beanThumbnailPlaceholder
    }

    SharedBackgroundSurface {
        id: backgroundTestSurface
        objectName: "sharedBackgroundTestSurface"
        x: 84
        y: 515
        width: 56
        height: 56
        baseColor: "#24313e"
    }

    EquipmentInventoryStyle {
        id: equipmentInventoryStyle
        titleFont: Qt.font({ family: Qt.application.font.family, pixelSize: 20 })
        subtitleFont: Qt.font({ family: Qt.application.font.family, pixelSize: 18 })
        bodyFont: Qt.font({ family: Qt.application.font.family, pixelSize: 16 })
        pageHorizontalMargin: 16
        minimumColumnWidth: 380
        cardSpacing: 16
    }

    EquipmentCardStyle {
        id: equipmentCardStyle
        bodyFont: Qt.font({ family: Qt.application.font.family, pixelSize: 16 })
        labelFont: Qt.font({ family: Qt.application.font.family, pixelSize: 14 })
        captionFont: Qt.font({ family: Qt.application.font.family, pixelSize: 12 })
        subtitleFont: Qt.font({ family: Qt.application.font.family, pixelSize: 18 })
        textColor: "#202020"
        textSecondaryColor: "#606060"
        warningColor: "#9a4139"
        backgroundColor: "#ffffff"
        borderColor: "#808080"
        selectedBorderColor: "#1769aa"
        cardWidth: 360
        cardRadius: 16
        outerVerticalPadding: 24
        contentPadding: 12
        contentSpacing: 6
    }

    SharedButtonStyle {
        id: equipmentButtonStyle
        bodyFont: Qt.font({ family: Qt.application.font.family, pixelSize: 16 })
        primaryColor: "#1769aa"
        primaryContrastColor: "white"
        cardBackgroundColor: "#ffffff"
        textColor: "#202020"
        textSecondaryColor: "#606060"
        borderColor: "#808080"
    }

    SharedButtonInteraction { id: equipmentButtonInteraction }

    SharedButtonStyle {
        id: scaledButtonStyle
        scaleFactor: 0.4
        bodyFont: Qt.font({ family: Qt.application.font.family, pixelSize: 16 })
    }
    SharedAccessibleButton {
        id: scaledButtonTest
        accessibleName: "Save"
        buttonStyle: scaledButtonStyle
        interaction: equipmentButtonInteraction
        // Keep pointer input inside the runner's native window and clear of
        // the other controls, including the switch at the top right.
        x: 440
        y: 140
        z: 10001
        text: "Save"
    }

    property bool colorEditorVisible: false
    property bool colorCommitPending: false
    ColorEditor {
        id: colorEditorTest
        x: 16
        y: 16
        z: 10000
        width: 420
        height: 160
        visible: testCase.colorEditorVisible
        enabled: !testCase.colorCommitPending
        onColorCommitted: testCase.colorCommitPending = true
    }


    SharedSuggestionFieldChrome {
        id: suggestionFieldChromeTest
        field: suggestionField
        buttonStyle: equipmentButtonStyle
        interaction: equipmentButtonInteraction
        accessibilityMode: false
    }

    SharedDialogStyle {
        id: sharedDialogStyle
        scaleFactor: 1
        bodyFont: Qt.font({ family: Qt.application.font.family, pixelSize: 16 })
        subtitleFont: Qt.font({ family: Qt.application.font.family, pixelSize: 20 })
        surfaceColor: "#ffffff"
        textColor: "#202020"
        textSecondaryColor: "#606060"
        borderColor: "#808080"
        primaryColor: "#1769aa"
    }

    SharedSelectionDialog {
        id: sharedSelectionDialogTest
        dialogStyle: sharedDialogStyle
        buttonInteraction: equipmentButtonInteraction
        title: "Choose grinder"
        options: ["Grinder A", "Grinder B"]
        currentValue: "Grinder A"
        selectedLabel: "Selected"
        noneLabel: "None"
        cancelLabel: "Cancel"
    }

    SharedDecenzaDialog {
        id: browserBackDialogTest
        parent: Overlay.overlay
        dialogStyle: sharedDialogStyle
        closePolicy: Popup.CloseOnEscape
        browserUnloadDirty: true
    }

    SharedSwitchStyle {
        id: sharedSwitchStyle
        bodyFont: Qt.font({ family: Qt.application.font.family, pixelSize: 16 })
        primaryColor: "#1769aa"
        primaryContrastColor: "white"
        insetBackgroundColor: "#dedede"
        textColor: "#202020"
        textSecondaryColor: "#606060"
        fallbackToggleLabel: "Toggle"
    }

    SharedStyledSwitch {
        id: sharedSwitchTest
        // Keep the real pointer target inside the QML test window; the lower
        // fixtures extend beyond the runner's native window on some platforms.
        x: 440
        y: 20
        z: 10
        switchStyle: sharedSwitchStyle
        text: ""
        accessibleName: "Use puck prep"
    }

    SharedKeyboardInteraction { id: equipmentKeyboardInteraction }

    property Item equipmentImeTarget: null
    Connections {
        target: equipmentKeyboardInteraction
        function onCommitRequested() {
            // The probe supplies the platform IME's response to the real
            // dialog commit request; TextInput still processes the event.
            if (testCase.equipmentImeTarget)
                inputMethodProbe.commit(testCase.equipmentImeTarget,
                                        testCase.equipmentImeTarget.preeditText)
        }
    }
    SignalSpy {
        id: equipmentCommitSpy
        target: equipmentKeyboardInteraction
        signalName: "commitRequested"
    }

    EquipmentDialogStyle {
        id: equipmentDialogStyle
        dialogStyle: sharedDialogStyle
        buttonStyle: equipmentButtonStyle
        buttonInteraction: equipmentButtonInteraction
        keyboardInteraction: equipmentKeyboardInteraction
        formControlStyle: controlStyle
        switchStyle: sharedSwitchStyle
        titleFont: Qt.font({ family: Qt.application.font.family, pixelSize: 20 })
        labelFont: Qt.font({ family: Qt.application.font.family, pixelSize: 14 })
        captionFont: Qt.font({ family: Qt.application.font.family, pixelSize: 12 })
    }

    property var fakeEquipmentPackages: []

    EquipmentUiBackend {
        id: fakeEquipmentBackend
        packages: testCase.fakeEquipmentPackages
        activePackageId: 7
        puckPrepRows: [
            { key: "wdt", label: "WDT" },
            { key: "shaker", label: "Shaker" }
        ]
        createSeed: ({ grinderBrand: "Seed brand", grinderModel: "Seed model" })
        property var lastOperation: ({})
        property int refreshCount: 0

        function refresh() { refreshCount += 1 }
        function createPackage(fields, applyToActiveBag, requestId) {
            lastOperation = ({ kind: "create", fields: fields,
                               applyToActiveBag: applyToActiveBag, requestId: requestId })
        }
        function updatePackage(packageId, fields, expectedEditable, requestId) {
            lastOperation = ({ kind: "update", packageId: packageId, fields: fields,
                               expectedEditable: expectedEditable, requestId: requestId })
        }
        function activatePackage(packageId, applyToActiveBag, requestId) {
            lastOperation = ({ kind: "activate", packageId: packageId,
                               applyToActiveBag: applyToActiveBag, requestId: requestId })
        }
        function completeOperation(requestId, success, resultId, reason) {
            operationCompleted(requestId, success, resultId, reason)
        }
    }

    EquipmentDialogText { id: equipmentDialogStrings }

    SharedEquipmentDialog {
        id: sharedEquipmentDialogTest
        objectName: "sharedEquipmentDialogTest"
        backend: fakeEquipmentBackend
        equipmentStyle: equipmentDialogStyle
        strings: equipmentDialogStrings
        applyToActiveBag: true
    }

    property var inventoryTestPackages: []

    EquipmentInventoryView {
        id: inventoryTestView
        x: 160
        y: 515
        width: 480
        height: 170
        inventoryStyle: equipmentInventoryStyle
        buttonStyle: equipmentButtonStyle
        interaction: equipmentButtonInteraction
        title: "Equipment"
        emptyTitle: "No equipment yet"
        emptyHint: "Add a grinder to track its burrs and dial-in."
        emptyStateVisible: inventoryTestPackages.length === 0
        packages: inventoryTestPackages
        cardDelegate: Component {
            Rectangle {
                id: inventoryTestDelegate
                required property var modelData
                objectName: "inventoryTestCard-" + String(modelData.id)
                width: inventoryTestView.cardWidth
                height: 36
                color: "#ffffff"
            }
        }
    }

    EquipmentCardView {
        id: equipmentCardViewTest
        x: 16
        y: 700
        width: 360
        cardStyle: equipmentCardStyle
        buttonStyle: equipmentButtonStyle
        interaction: equipmentButtonInteraction
        pkg: ({ id: 42, name: "Test grinder", grinderBrand: "Niche", grinderModel: "Zero",
                grinderBurrs: "Steel burrs", basketBrand: "IMS", basketModel: "18 g", shotCount: 2 })
        puckPrepLabels: ["WDT"]
        basketTemplate: "Basket: %1"
        puckPrepTemplate: "Prep: %1"
        noticeText: "Referenced package cannot be deleted"
        removeText: "Remove"
        removeAccessibleName: "Remove test grinder"
        editAccessibleName: "Edit test grinder"
        deleteAccessibleName: "Delete test grinder"
        onEditRequested: function(pkg) { }
        onRemoveRequested: function(pkg) { }
        onDeleteRequested: function(pkg) { }
    }

    EquipmentSummaryView {
        id: equipmentSummaryViewTest
        x: 16
        y: 620
        width: 320
        grinderBrand: "Niche"
        grinderModel: "Zero"
        grinderBurrs: "Steel burrs"
        grindSetting: "2.5"
        rpm: 1200
        rpmCapable: true
        basketBrand: "IMS"
        basketModel: "18 g"
        puckPrepLabels: ["WDT", "Level"]
        equipmentState: "retired"
        basketTemplate: "Basket: %1"
        puckPrepTemplate: "Prep: %1"
        lastGrindTemplate: "Grind %1"
        lastRpmTemplate: "%1 rpm"
        retiredText: "Retired now"
        bodyFont: Qt.font({ family: Qt.application.font.family, pixelSize: 16 })
        labelFont: Qt.font({ family: Qt.application.font.family, pixelSize: 14 })
        captionFont: Qt.font({ family: Qt.application.font.family, pixelSize: 12 })
        primaryTextColor: "#202020"
        secondaryTextColor: "#606060"
    }

    Component {
        id: recipePlanComponent
        Item {
            implicitHeight: 58
            Layout.fillWidth: true
            Label {
                anchors.fill: parent
                text: "Shot plan content"
            }
        }
    }

    RecipeDrinkCardView {
        id: recipeCard
        x: 16
        y: 330
        width: 480
        cardStyle: recipeCardStyle
        recipeName: "Literal <b>recipe</b> & title"
        activeLabel: "Active"
        drinkLine: "Espresso · Long profile title that must wrap instead of disappearing"
        beanLine: "Roaster · Coffee"
        planVisible: testCase.showRecipePlan
        planComponent: recipePlanComponent
        staleWarningVisible: testCase.showStaleWarning
        staleWarningText: "Finished bag: choose a replacement"
        staleActionVisible: testCase.showStaleAction
        staleActionText: "Choose beans for this recipe"
        profileMissing: testCase.showMissingProfile
        profileMissingText: "This profile is missing"
        footer: Item {
            objectName: "sharedRecipeCardFooter"
            visible: testCase.showRecipeFooter
            implicitHeight: 48
            Layout.fillWidth: true
        }
    }

    SharedShotPlanStyle {
        id: shotPlanStyle
        bodyFont: Qt.font({ family: Qt.application.font.family, pixelSize: 16 })
        textColor: "#202020"
        pressedColor: "#1769aa"
        warningColor: "#9a4139"
        spacing: 8
        iconSize: 20
        hitInset: 8
    }

    Component {
        id: shotPlanIcon
        Rectangle {
            color: "#1769aa"
            radius: width / 2
        }
    }

    SharedShotPlanView {
        id: shotPlanView
        x: 16
        y: 430
        width: 190
        height: implicitHeight
        planStyle: shotPlanStyle
        plainText: "Accessible plain shot plan summary"
        richText: "A long shared shot plan that wraps across the narrow view while retaining its content"
        maxLines: 2
        iconComponent: shotPlanIcon
    }

    property bool showRecipePlan: false
    property bool showRecipeFooter: false
    property bool showStaleWarning: false
    property bool showStaleAction: false
    property bool showMissingProfile: false

    SharedFormControlsInputMethodProbe {
        id: inputMethodProbe
    }

    SignalSpy {
        id: selectedSpy
        target: suggestionField
        signalName: "suggestionSelected"
    }
    SignalSpy { id: buttonClickedSpy; target: scaledButtonTest; signalName: "clicked" }
    SignalSpy { id: colorCommittedSpy; target: colorEditorTest; signalName: "colorCommitted" }
    SignalSpy {
        id: editedSpy
        target: suggestionField
        signalName: "textEdited"
    }
    SignalSpy {
        id: keyboardShowSpy
        target: suggestionField
        signalName: "keyboardShowRequested"
    }
    SignalSpy {
        id: keyboardHideSpy
        target: suggestionField
        signalName: "keyboardHideRequested"
    }
    SignalSpy {
        id: recipeEditedSpy
        target: recipeNameEditor
        signalName: "textEdited"
    }
    SignalSpy {
        id: recipeKeyboardShowSpy
        target: recipeNameEditor
        signalName: "keyboardShowRequested"
    }
    SignalSpy {
        id: recipeKeyboardHideSpy
        target: recipeNameEditor
        signalName: "keyboardHideRequested"
    }
    SignalSpy {
        id: staleActionSpy
        target: recipeCard
        signalName: "staleActionClicked"
    }
    SignalSpy {
        id: shotPlanClickSpy
        target: shotPlanView
        signalName: "clicked"
    }
    SignalSpy {
        id: backgroundPhotoFailureSpy
        target: backgroundTestSurface
        signalName: "photoLoadFailed"
    }
    SignalSpy {
        id: equipmentEditSpy
        target: equipmentCardViewTest
        signalName: "editRequested"
    }
    SignalSpy {
        id: equipmentRemoveSpy
        target: equipmentCardViewTest
        signalName: "removeRequested"
    }
    SignalSpy {
        id: equipmentDeleteSpy
        target: equipmentCardViewTest
        signalName: "deleteRequested"
    }
    SignalSpy {
        id: selectionSpy
        target: sharedSelectionDialogTest
        signalName: "selected"
    }
    SignalSpy {
        id: selectionDialogOpenedSpy
        target: sharedSelectionDialogTest
        signalName: "opened"
    }
    SignalSpy {
        id: selectionDialogClosedSpy
        target: sharedSelectionDialogTest
        signalName: "closed"
    }
    SignalSpy {
        id: switchToggledSpy
        target: sharedSwitchTest
        signalName: "toggled"
    }
    SignalSpy {
        id: equipmentPackageSavedSpy
        target: sharedEquipmentDialogTest
        signalName: "packageSaved"
    }

    function findTextItem(rootItem, targetText) {
        if (!rootItem)
            return null
        if (rootItem.text !== undefined && rootItem.text === targetText)
            return rootItem
        var descendants = testCase.visualChildren(rootItem)
        for (var i = 0; i < descendants.length; ++i) {
            var found = findTextItem(descendants[i], targetText)
            if (found)
                return found
        }
        return null
    }

    function findVisibleTextItem(rootItem, targetText) {
        if (!rootItem)
            return null
        if (rootItem.visible && rootItem.text !== undefined && rootItem.text === targetText)
            return rootItem
        var descendants = testCase.visualChildren(rootItem)
        for (var i = 0; i < descendants.length; ++i) {
            var found = findVisibleTextItem(descendants[i], targetText)
            if (found)
                return found
        }
        return null
    }

    function visualChildren(item) {
        var result = []
        function append(candidate) {
            if (candidate && candidate !== item && result.indexOf(candidate) < 0)
                result.push(candidate)
        }
        if (item.children !== undefined) {
            for (var i = 0; i < item.children.length; ++i)
                append(item.children[i])
        }
        // Popup roots are QObjects rather than QQuickItems; their visual
        // content is exposed through these properties instead of `children`.
        append(item.contentItem)
        append(item.background)
        append(item.header)
        append(item.footer)
        return result
    }

    function findObjectNamed(rootItem, targetName) {
        if (!rootItem)
            return null
        if (rootItem.objectName === targetName)
            return rootItem
        var descendants = visualChildren(rootItem)
        for (var i = 0; i < descendants.length; ++i) {
            var found = findObjectNamed(descendants[i], targetName)
            if (found)
                return found
        }
        return null
    }

    function findAccessibleItemNamed(rootItem, targetName, role) {
        if (!rootItem)
            return null
        if (rootItem.visible && rootItem.Accessible && rootItem.Accessible.name === targetName
                && (role === undefined || rootItem.Accessible.role === role))
            return rootItem
        var descendants = visualChildren(rootItem)
        for (var i = 0; i < descendants.length; ++i) {
            var found = findAccessibleItemNamed(descendants[i], targetName, role)
            if (found)
                return found
        }
        return null
    }

    function findVisibleAccessibleButton(rootItem, buttonText) {
        var item = findVisibleTextItem(rootItem, buttonText)
        while (item && item !== rootItem) {
            if (item.Accessible && item.Accessible.role === Accessible.Button)
                return item
            item = item.parent
        }
        return null
    }

    function formatShotPlan(input) {
        var completeInput = {
            itemOrder: ["doseYield"], sentence: true, stacked: false, isCleaning: false,
            profileName: "", recipeName: "", temperatureText: "", tempOverridden: false,
            overrideTemp: 0, tempHlBaseline: 0, profileYield: 0, baselineYieldG: 0,
            targetWeight: 0, yieldOverridden: false, yieldAnchorMode: "weight",
            yieldAnchorRatio: 0, yieldTargetOnly: false, actualYield: 0, dose: 0,
            roasterBrand: "", coffeeName: "", roastDate: "", grindSize: "",
            grindRpm: 0, rpmCapable: false, beverageText: "coffee"
        }
        for (var key in input)
            completeInput[key] = input[key]
        return ShotPlanFormat.format(completeInput, function(key, fallback) { return fallback }, {
            plainSeparator: "  ·  ",
            richSeparator: " <font size=\"+1\"><b>·</b></font> ",
            highlightColor: "#d09000",
            escapeHtml: function(value) {
                return String(value).replace(/&/g, "&amp;").replace(/</g, "&lt;")
            }
        })
    }

    function init() {
        equipmentImeTarget = null
        equipmentCommitSpy.clear()
        colorEditorVisible = false
        colorCommitPending = false
        colorEditorTest._rgbMode = false
        colorEditorTest.setColor("#4682E6")
        colorCommittedSpy.clear()
        buttonClickedSpy.clear()
        if (sharedEquipmentDialogTest.saving) {
            var pendingId = sharedEquipmentDialogTest._pendingRequestId
            if (pendingId && pendingId.length > 0)
                fakeEquipmentBackend.completeOperation(pendingId, false, -1, "testReset")
        }
        sharedEquipmentDialogTest.close()
        if (sharedSelectionDialogTest.visible
                || selectionDialogOpenedSpy.count > selectionDialogClosedSpy.count) {
            selectionDialogClosedSpy.clear()
            sharedSelectionDialogTest.close()
            if (selectionDialogClosedSpy.count === 0)
                selectionDialogClosedSpy.wait(3000)
        }
        selectionDialogOpenedSpy.clear()
        selectionDialogClosedSpy.clear()
        fakeEquipmentPackages = []
        fakeEquipmentBackend.loading = false
        fakeEquipmentBackend.lastOperation = ({})
        fakeEquipmentBackend.refreshCount = 0
        equipmentPackageSavedSpy.clear()
        selectionSpy.clear()
        parentValue = ""
        inventoryTestPackages = []
        inventoryTestView.width = 480
        equipmentCardViewTest.pkg = ({
            id: 42, name: "Test grinder", grinderBrand: "Niche", grinderModel: "Zero",
            grinderBurrs: "Steel burrs", basketBrand: "IMS", basketModel: "18 g", shotCount: 2
        })
        equipmentCardViewTest.noticeVisible = false
        suggestionField.isActivelyTyping = false
        suggestionField.justSelected = false
        suggestionField.closeSuggestions()
        suggestionField.textField.focus = false
        suggestionField.accessibilityMode = false
        recipeNameEditor.accessibilityMode = false
        recipeNameEditor.nameConflict = false
        recipeNameEditor.text = ""
        selectedSpy.clear()
        editedSpy.clear()
        keyboardShowSpy.clear()
        keyboardHideSpy.clear()
        recipeEditedSpy.clear()
        recipeKeyboardShowSpy.clear()
        recipeKeyboardHideSpy.clear()
        showRecipePlan = false
        showRecipeFooter = false
        showStaleWarning = false
        showStaleAction = false
        showMissingProfile = false
        recipeCard.width = 480
        staleActionSpy.clear()
        backgroundTestSurface.patternEnabled = false
        backgroundTestSurface.patternSource = ""
        backgroundTestSurface.patternInkColor = "white"
        backgroundTestSurface.patternTileSize = 0
        backgroundTestSurface.patternOpacity = 0
        backgroundTestSurface.photoEnabled = false
        backgroundTestSurface.photoSource = ""
        backgroundTestSurface.photoDecodeWidth = 56
        backgroundTestSurface.photoDecodeHeight = 56
        backgroundPhotoFailureSpy.clear()
    }

    function typeQuery(query) {
        suggestionField.textField.forceActiveFocus()
        suggestionField.textField.selectAll()
        for (var i = 0; i < query.length; ++i)
            keyClick(query.charAt(i))
        tryVerify(function() { return suggestionField.popupVisible })
    }

    function test_filteredSuggestionsFollowTypedDisplayText() {
        typeQuery("bar")

        compare(JSON.stringify(suggestionField.getFilteredSuggestions()),
                JSON.stringify(["Baratza", "Baratza Encore"]))
        compare(suggestionField.popupVisible, true)
    }

    function test_imePreeditFiltersBeforeCommit() {
        suggestionField.textField.forceActiveFocus()
        inputMethodProbe.sendPreedit(suggestionField.textField, "Com")

        compare(suggestionField.textField.text, "")
        compare(suggestionField.textField.displayText, "Com")
        compare(JSON.stringify(suggestionField.getFilteredSuggestions()),
                JSON.stringify(["Comandante"]))
        tryVerify(function() { return suggestionField.popupVisible })

        inputMethodProbe.commit(suggestionField.textField, "Com")

        tryCompare(suggestionField.textField, "text", "Com")
        tryCompare(testCase, "parentValue", "Com")
        tryCompare(suggestionField, "text", "Com")
    }

    function test_selectionUpdatesExternallyBoundValue() {
        suggestionField.selectSuggestion("DF64")

        tryCompare(testCase, "parentValue", "DF64")
        tryCompare(suggestionField, "text", "DF64")
        tryCompare(suggestionField.textField, "text", "DF64")
        compare(selectedSpy.count, 1)
        compare(selectedSpy.signalArguments[0][0], "DF64")

        parentValue = "External update after selection"
        tryCompare(suggestionField, "text", "External update after selection")
        tryCompare(suggestionField.textField, "text", "External update after selection")
    }

    function test_enterSelectsSingleMatch() {
        typeQuery("Com")
        keyClick(Qt.Key_Return)

        tryCompare(selectedSpy, "count", 1)
        compare(selectedSpy.signalArguments[0][0], "Comandante")
        tryCompare(testCase, "parentValue", "Comandante")
    }

    function test_enterSelectsFirstNonExactMatch() {
        typeQuery("Bar")
        keyClick(Qt.Key_Return)

        tryCompare(selectedSpy, "count", 1)
        compare(selectedSpy.signalArguments[0][0], "Baratza")
        tryCompare(testCase, "parentValue", "Baratza")
    }

    function test_enterKeepsExactTextWhenSeveralMatchesExist() {
        typeQuery("Baratza")
        keyClick(Qt.Key_Return)

        compare(selectedSpy.count, 0)
        tryVerify(function() { return editedSpy.count > 0 })
        var edits = editedSpy.signalArguments
        compare(edits[edits.length - 1][0], "Baratza")
        tryCompare(testCase, "parentValue", "Baratza")
    }

    function test_arrowNavigationChoosesHighlightedMatch() {
        typeQuery("B")
        keyClick(Qt.Key_Down)
        keyClick(Qt.Key_Down)
        keyClick(Qt.Key_Return)

        tryCompare(selectedSpy, "count", 1)
        compare(selectedSpy.signalArguments[0][0], "Baratza Encore")
    }

    function test_escapeClosesSuggestionsWithoutSelecting() {
        typeQuery("B")
        keyClick(Qt.Key_Escape)

        tryVerify(function() { return !suggestionField.popupVisible })
        compare(selectedSpy.count, 0)
        compare(suggestionField.textField.text, "B")
    }

    function test_accessibleFieldNameAndPasswordDescription() {
        compare(suggestionField.textField.Accessible.role, Accessible.EditableText)
        compare(suggestionField.textField.Accessible.name, "Grinder model")
        compare(passwordField.Accessible.name, "Account password")
        compare(passwordField.Accessible.description, "")
    }

    function test_accessibleButtonRendersMarkupAsLiteralText() {
        var previousText = scaledButtonTest.text
        try {
            scaledButtonTest.text = "<b>Save</b>"
            var label = findObjectNamed(scaledButtonTest, "sharedAccessibleButtonLabel")
            verify(label !== null)
            compare(label.text, "<b>Save</b>")
            compare(label.textFormat, Text.PlainText)
        } finally {
            scaledButtonTest.text = previousText
        }
    }

    function test_suggestionActionsStayAtTextFieldRightEdge() {
        parentValue = "Niche"
        tryVerify(function() { return suggestionField.textField.text === "Niche" }, 3000)

        var clearArea = findAccessibleItemNamed(suggestionField, "Clear text")
        var openArea = findAccessibleItemNamed(suggestionField, "Open suggestions")
        verify(clearArea !== null)
        verify(openArea !== null)
        verify(waitForPolish(clearArea.parent.parent))

        var clearPosition = clearArea.mapToItem(suggestionField.textField, 0, 0)
        var openPosition = openArea.mapToItem(suggestionField.textField, 0, 0)
        verify(clearPosition.x > suggestionField.textField.width * 0.5)
        verify(openPosition.x > suggestionField.textField.width * 0.5)
        verify(clearPosition.x < openPosition.x)
        verify(openPosition.x + openArea.width <= suggestionField.textField.width + 0.01)
        verify(Math.abs(openPosition.y + openArea.height / 2
                        - suggestionField.textField.height / 2) < 1)
    }

    function test_accessibilityExplorationSuppressesKeyboardUntilActivation() {
        suggestionField.accessibilityMode = true
        suggestionField.textField.forceActiveFocus()
        tryCompare(keyboardHideSpy, "count", 1)
        compare(keyboardShowSpy.count, 0)

        suggestionField.textField.Accessible.pressAction()

        tryCompare(keyboardShowSpy, "count", 1)
    }

    function test_recipeNameEditorSharesValueAccessibilityAndConflictText() {
        recipeNameEditor.text = "Morning tea"
        compare(recipeNameEditor.inputField.text, "Morning tea")
        recipeNameEditor.inputField.text = "Evening tea"
        compare(recipeNameEditor.text, "Evening tea")
        compare(recipeNameEditor.inputField.Accessible.name, "Recipe name")

        recipeNameEditor.nameConflict = true
        var conflict = findChild(recipeNameEditor, "recipeNameConflict")
        verify(conflict !== null)
        compare(conflict.visible, true)
        compare(conflict.text, "<b>Name already exists</b>")
        compare(conflict.textFormat, Text.PlainText)
    }

    function test_recipeNameEditorForwardsTypingAndFocusHelpers() {
        recipeNameEditor.focusInput()
        tryVerify(function() { return recipeNameEditor.inputField.activeFocus })
        recipeNameEditor.inputField.text = "abc"
        recipeNameEditor.selectAll()
        compare(recipeNameEditor.inputField.selectedText, "abc")

        for (var i = 0; i < "xyz".length; ++i)
            keyClick("xyz".charAt(i))
        tryCompare(recipeEditedSpy, "count", 3)
        compare(recipeNameEditor.text, "xyz")
    }

    function test_recipeNameEditorForwardsAccessibilityKeyboardPolicy() {
        recipeNameEditor.accessibilityMode = true
        recipeNameEditor.focusInput()
        tryCompare(recipeKeyboardHideSpy, "count", 1)
        compare(recipeKeyboardShowSpy.count, 0)

        recipeNameEditor.inputField.Accessible.pressAction()
        tryCompare(recipeKeyboardShowSpy, "count", 1)
    }

    function test_nestedFontOverrideSurvivesHostStyleUpdate() {
        controlStyle.font = Qt.font({ family: Qt.application.font.family, pixelSize: 16,
                                      weight: Font.Normal })
        compare(nestedFontOverrideField.font.bold, true)

        controlStyle.font = Qt.font({ family: Qt.application.font.family, pixelSize: 18,
                                      weight: Font.Normal, italic: true })

        compare(nestedFontOverrideField.font.italic, true)
        compare(nestedFontOverrideField.font.pixelSize, 22)
        compare(nestedFontOverrideField.font.bold, true)
    }

    function test_sharedRecipeCardTreatsHostTextAsLiteral() {
        var title = findTextItem(recipeCard, "Literal <b>recipe</b> & title")
        verify(title !== null)
        compare(title.textFormat, Text.PlainText)
        compare(title.text, "Literal <b>recipe</b> & title")
    }

    function test_recipeCardStyleDefaultsUseValidFontMaps() {
        compare(defaultRecipeCardStyle.titleFont.pointSize, 14)
        compare(defaultRecipeCardStyle.captionFont.pointSize, 12)
        verify(typeof defaultFormControlStyle.font.pixelSize === "number")
        verify(typeof defaultShotPlanStyle.bodyFont.pixelSize === "number")
    }

    function test_sharedRecipeCardWrapsLongContentAtNarrowWidth() {
        var title = findTextItem(recipeCard, "Literal <b>recipe</b> & title")
        verify(title !== null)
        tryVerify(function() { return title.width > 0 && title.height > 0 })
        var wideHeight = title.height

        recipeCard.width = 205
        tryVerify(function() { return title.height > wideHeight })
        verify(recipeCard.implicitHeight > 0)

        var drink = findTextItem(recipeCard,
                                 "Espresso · Long profile title that must wrap instead of disappearing")
        verify(drink !== null)
        compare(drink.wrapMode, Text.WordWrap)
    }

    function test_sharedRecipeCardPlanAndFooterContributeContentHeight() {
        showRecipePlan = false
        showRecipeFooter = false
        tryVerify(function() { return recipeCard.implicitHeight > 0 })
        var baseHeight = recipeCard.implicitHeight

        showRecipePlan = true
        tryVerify(function() { return recipeCard.implicitHeight > baseHeight })
        var planHeight = recipeCard.implicitHeight

        showRecipeFooter = true
        tryVerify(function() { return recipeCard.implicitHeight > planHeight })
        var footer = findChild(recipeCard, "sharedRecipeCardFooter")
        verify(footer !== null)
        compare(footer.visible, true)
    }

    function test_sharedRecipeCardActiveStaleAndMissingStates() {
        recipeCard.active = false
        compare(recipeCard.border.color, recipeCardStyle.borderColor)
        compare(recipeCard.border.width, 1)
        var activeBadge = findTextItem(recipeCard, "Active")
        verify(activeBadge !== null)
        compare(activeBadge.visible, false)

        recipeCard.active = true
        compare(recipeCard.border.color, recipeCardStyle.accentColor)
        compare(recipeCard.border.width, 2)
        tryVerify(function() { return activeBadge.visible })

        showStaleWarning = true
        var staleWarning = findTextItem(recipeCard, "Finished bag: choose a replacement")
        verify(staleWarning !== null)
        compare(staleWarning.Accessible.role, Accessible.StaticText)
        compare(staleWarning.Accessible.name, "Finished bag: choose a replacement")
        tryVerify(function() { return staleWarning.visible })

        showStaleWarning = false
        showStaleAction = true
        var staleAction = findTextItem(recipeCard, "Choose beans for this recipe")
        verify(staleAction !== null)
        compare(staleAction.Accessible.role, Accessible.Button)
        compare(staleAction.Accessible.name, "Choose beans for this recipe")
        compare(staleAction.activeFocusOnTab, true)
        staleAction.Accessible.pressAction()
        tryCompare(staleActionSpy, "count", 1)

        showStaleAction = false
        showMissingProfile = true
        var missingProfile = findTextItem(recipeCard, "This profile is missing")
        verify(missingProfile !== null)
        compare(missingProfile.Accessible.role, Accessible.StaticText)
        tryVerify(function() { return missingProfile.visible })
    }

    function test_sharedRecipeCardStaleActionSupportsKeyboardActivation() {
        showStaleAction = true
        var staleAction = findTextItem(recipeCard, "Choose beans for this recipe")
        verify(staleAction !== null)
        tryVerify(function() { return staleAction.activeFocusOnTab })

        staleAction.forceActiveFocus()
        keyClick(Qt.Key_Space)
        tryCompare(staleActionSpy, "count", 1)

        staleActionSpy.clear()
        staleAction.forceActiveFocus()
        keyClick(Qt.Key_Return)
        tryCompare(staleActionSpy, "count", 1)
    }

    function test_sharedDrinkTypeCatalogLabelsIconsAndUnknownFallback() {
        var identityTranslator = function(key, fallback) { return fallback }
        compare(DrinkTypeCatalog.shortLabel("latte", identityTranslator), "Latte")
        compare(DrinkTypeCatalog.longLabel("latte", identityTranslator), "Latte / Cappuccino")
        compare(DrinkTypeCatalog.shortLabel("latte", identityTranslator)
                === DrinkTypeCatalog.longLabel("latte", identityTranslator), false)
        compare(DrinkTypeCatalog.icon("long_black"), "qrc:/icons/water.svg")
        compare(DrinkTypeCatalog.icon("latte"), "qrc:/icons/steam.svg")
        compare(DrinkTypeCatalog.shortLabel("future_drink", identityTranslator), "future_drink")
        compare(DrinkTypeCatalog.longLabel("future_drink", identityTranslator), "future_drink")
        compare(DrinkTypeCatalog.icon("future_drink"), "qrc:/icons/espresso.svg")
    }

    function test_sharedShotPlanRatioAnchorAndYieldBaselineFallback() {
        var ratioWithoutDose = formatShotPlan({
            itemOrder: ["doseYield"], sentence: false, dose: 0,
            yieldAnchorMode: "ratio", yieldAnchorRatio: 2, targetWeight: 0
        })
        compare(ratioWithoutDose.text, "1:2.0")

        var ratioWithDose = formatShotPlan({
            itemOrder: ["doseYield"], sentence: false, dose: 18,
            yieldAnchorMode: "ratio", yieldAnchorRatio: 2, targetWeight: 36,
            profileYield: 36, baselineYieldG: 0, yieldOverridden: false, actualYield: 0
        })
        compare(ratioWithDose.text, "18.0g in  ·  36.0g (1:2.0)")

        var profileFallback = formatShotPlan({
            itemOrder: ["doseYield"], sentence: false, dose: 0,
            yieldAnchorMode: "weight", yieldAnchorRatio: 0, targetWeight: 40,
            profileYield: 36, baselineYieldG: 0, yieldOverridden: true, actualYield: 0
        })
        compare(profileFallback.text, "36.0 → 40.0g")

        var noTarget = formatShotPlan({
            itemOrder: ["doseYield"], sentence: false, dose: 0,
            yieldAnchorMode: "weight", targetWeight: 0, profileYield: 36,
            baselineYieldG: 0, yieldOverridden: false, actualYield: 0
        })
        compare(noTarget.text, "")
    }

    function test_sharedShotPlanUsesHostResolvedMultiframeTemperature() {
        var rendered = formatShotPlan({
            itemOrder: ["profile", "temperature"], sentence: false,
            profileName: "Profile", temperatureText: "81 · 91°C",
            tempOverridden: false, overrideTemp: 91, tempHlBaseline: 91
        })
        compare(rendered.text, "Profile  ·  81 · 91°C")
    }

    function test_sharedShotPlanEscapesMarkupAndKeepsPercentAndDollarTextLiteral() {
        var rendered = formatShotPlan({
            itemOrder: ["profile"], sentence: true, profileName: "Ramp %2 $& <b>",
            recipeName: "", temperatureText: "", targetWeight: 0,
            beverageText: "coffee"
        })
        verify(rendered.text.indexOf("$&") >= 0)
        verify(rendered.text.indexOf("%\u200B2") >= 0)
        verify(rendered.rich.indexOf("$&amp;") >= 0)
        verify(rendered.rich.indexOf("%\u200B2") >= 0)
        verify(rendered.rich.indexOf("&lt;b>") >= 0)
    }

    function test_sharedShotPlanStackingChangesRichLayoutNotAccessibleText() {
        var rendered = formatShotPlan({
            itemOrder: ["doseYield", "profile", "roaster", "coffee"], sentence: true, stacked: true,
            dose: 18, profileName: "Morning", recipeName: "", temperatureText: "",
            targetWeight: 36, profileYield: 36, baselineYieldG: 0,
            yieldOverridden: false, yieldAnchorMode: "weight", yieldAnchorRatio: 0,
            yieldTargetOnly: false, actualYield: 0, coffeeName: "House blend",
            roasterBrand: "Roaster", roastDate: "", grindSize: "", grindRpm: 0,
            rpmCapable: false, beverageText: "Espresso"
        })
        verify(rendered.rich.indexOf("<br>") >= 0)
        compare(rendered.text.indexOf("<br>"), -1)
        verify(rendered.text.indexOf("18.0g in") >= 0)
        verify(rendered.text.indexOf("Roaster  ·  House blend") >= 0)
    }

    function test_sharedShotPlanViewWrapsAndPassiveModeDoesNotCaptureClicks() {
        var text = findVisibleTextItem(shotPlanView,
            "A long shared shot plan that wraps across the narrow view while retaining its content")
        verify(text !== null)
        tryVerify(function() { return text.width > 0 && text.height > shotPlanStyle.bodyFont.pixelSize })
        compare(text.wrapMode, Text.Wrap)
        verify(text.lineCount <= 2)
        compare(shotPlanView.interactive, false)
        compare(shotPlanView.Accessible.role, Accessible.StaticText)
        compare(shotPlanView.Accessible.name, "Accessible plain shot plan summary")

        shotPlanClickSpy.clear()
        mouseClick(shotPlanView, shotPlanView.width / 2, shotPlanView.height / 2)
        compare(shotPlanClickSpy.count, 0)
    }

    function test_sharedBeanThumbnailUsesHostUrlAndShowsPlaceholderUntilReady() {
        var photo = findChild(beanThumbnailView, "sharedBeanThumbnailPhoto")
        var placeholder = findChild(beanThumbnailView, "sharedBeanThumbnailPlaceholder")
        verify(photo !== null)
        verify(placeholder !== null)
        compare(photo.source.toString(), "")
        compare(photo.fillMode, Image.PreserveAspectCrop)
        compare(photo.anchors.margins, 1)
        compare(photo.sourceSize.width, recipeCardStyle.scaled(88))
        compare(photo.sourceSize.height, recipeCardStyle.scaled(88))
        verify(photo.status !== Image.Ready)
        verify(placeholder.active)
        verify(findChild(placeholder, "beanThumbnailTestPlaceholder") !== null)

        // A tiny in-memory PNG exercises the real image load path without a
        // filesystem fixture or network request.
        beanThumbnailView.imageUrl = "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAIAAACQd1PeAAAADElEQVR4nGP4z8AAAAMBAQDJ/pLvAAAAAElFTkSuQmCC"
        tryVerify(function() { return photo.status === Image.Ready }, 3000)
        verify(!placeholder.active)

        beanThumbnailView.imageUrl = ""
        tryVerify(function() { return photo.status !== Image.Ready }, 3000)
        verify(placeholder.active)
    }

    function test_sharedBackgroundPatternUsesExplicitTileInputsAndYieldsToPhoto() {
        var base = findChild(backgroundTestSurface, "sharedBackgroundBase")
        var pattern = findChild(backgroundTestSurface, "sharedBackgroundPattern")
        var photo = findChild(backgroundTestSurface, "sharedBackgroundPhoto")
        verify(base !== null)
        verify(pattern !== null)
        verify(photo !== null)

        compare(base.color, Qt.color("#24313e"))
        verify(base.visible)
        backgroundTestSurface.patternEnabled = true
        backgroundTestSurface.patternSource = "qrc:/backgrounds/grain.svg"
        backgroundTestSurface.patternInkColor = "#ffffff"
        backgroundTestSurface.patternTileSize = 32
        backgroundTestSurface.patternOpacity = 0.18
        tryVerify(function() { return pattern.status === Image.Ready }, 3000)
        verify(pattern.visible)
        compare(pattern.fillMode, Image.Tile)
        compare(pattern.sourceSize.width, 32)
        compare(pattern.sourceSize.height, 32)
        compare(pattern.opacity, 0.18)

        // The renderer owns mutual suppression so an image being loaded or failing
        // never leaves a texture painted over the photo path.
        backgroundTestSurface.photoEnabled = true
        backgroundTestSurface.photoSource = "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAIAAACQd1PeAAAADElEQVR4nGP4z8AAAAMBAQDJ/pLvAAAAAElFTkSuQmCC"
        compare(pattern.visible, false)
        compare(backgroundTestSurface.photoStatus, photo.status)
        verify(base.visible || photo.status !== Image.Ready)
        tryVerify(function() { return photo.status === Image.Ready }, 3000)
        compare(backgroundTestSurface.photoStatus, Image.Ready)
        verify(photo.visible)
        verify(!base.visible)
        compare(pattern.visible, false)

        backgroundTestSurface.photoEnabled = false
        backgroundTestSurface.photoSource = ""
        tryVerify(function() { return pattern.visible }, 3000)
        tryVerify(function() { return backgroundTestSurface.photoStatus !== Image.Ready }, 3000)
        verify(base.visible)
        verify(!photo.visible)
    }

    function test_sharedBackgroundKeepsBaseForPhotoLoadFailureAndClearing() {
        var base = findChild(backgroundTestSurface, "sharedBackgroundBase")
        var pattern = findChild(backgroundTestSurface, "sharedBackgroundPattern")
        var photo = findChild(backgroundTestSurface, "sharedBackgroundPhoto")
        verify(base !== null)
        verify(pattern !== null)
        verify(photo !== null)

        backgroundTestSurface.patternEnabled = true
        backgroundTestSurface.patternSource = "qrc:/backgrounds/dots.svg"
        backgroundTestSurface.patternInkColor = "#ffffff"
        backgroundTestSurface.patternTileSize = 12
        backgroundTestSurface.patternOpacity = 0.18
        tryVerify(function() { return pattern.status === Image.Ready }, 3000)
        verify(pattern.visible)

        // Qt emits the expected QQuickImageBase::requestFinished() qmlWarning
        // for an unreadable source; the renderer's fallback and signal are under test.
        ignoreWarning(/QML QQuickImage: Cannot open: qrc:\/backgrounds\/does-not-exist\.png/)
        backgroundTestSurface.photoEnabled = true
        backgroundTestSurface.photoSource = "qrc:/backgrounds/does-not-exist.png"
        compare(pattern.visible, false)
        verify(base.visible)
        tryVerify(function() { return photo.status === Image.Error }, 3000)
        compare(backgroundTestSurface.photoStatus, Image.Error)
        tryCompare(backgroundPhotoFailureSpy, "count", 1)
        compare(backgroundPhotoFailureSpy.signalArguments[0][0].toString(),
                "qrc:/backgrounds/does-not-exist.png")
        verify(base.visible)
        verify(!photo.visible)

        backgroundTestSurface.photoEnabled = false
        backgroundTestSurface.photoSource = ""
        tryVerify(function() { return pattern.visible }, 3000)
        tryVerify(function() { return backgroundTestSurface.photoStatus !== Image.Error }, 3000)
        verify(base.visible)
    }

    function test_sharedHeaderKeepsTitleAndActionsVisibleAtNarrowWidths() {
        inventoryTestView.width = 300
        verify(waitForPolish(suggestionField.hostWindow))
        var heading = findChild(inventoryTestView, "sharedSectionHeading")
        var actions = findChild(inventoryTestView, "sharedSectionActions")
        verify(heading !== null)
        verify(actions !== null)
        tryVerify(function() { return actions.y >= heading.y + heading.height }, 3000)
        verify(heading.width > 0)
        verify(!heading.truncated)
        verify(actions.x >= 0)
        verify(actions.x + actions.width <= heading.parent.width + 0.01)
        verify(actions.y + actions.height <= heading.parent.height + 0.01)

        inventoryTestView.width = 800
        verify(waitForPolish(suggestionField.hostWindow))
        tryVerify(function() { return actions.x >= heading.x + heading.width }, 3000)
        verify(!heading.truncated)
        verify(actions.x + actions.width <= heading.parent.width + 0.01)
        verify(Math.abs(heading.y + heading.height / 2 - actions.y - actions.height / 2) < 0.01)
    }

    function test_sharedEquipmentInventoryResponsiveGridAndModelStates() {
        inventoryTestPackages = []
        inventoryTestView.width = 344
        tryCompare(inventoryTestView, "columnCount", 1)
        verify(Math.abs(inventoryTestView.viewWidth - 312) < 0.01)
        verify(Math.abs(inventoryTestView.cardWidth - 312) < 0.01)
        var emptyState = findChild(inventoryTestView, "sharedEquipmentEmptyState")
        verify(emptyState !== null)
        verify(emptyState.visible)

        inventoryTestPackages = [{ id: "one" }, { id: "two" }]
        tryVerify(function() {
            return findChild(inventoryTestView, "inventoryTestCard-one") !== null
                && findChild(inventoryTestView, "inventoryTestCard-two") !== null
        }, 3000)
        verify(!emptyState.visible)
        var narrowFirst = findChild(inventoryTestView, "inventoryTestCard-one")
        var narrowSecond = findChild(inventoryTestView, "inventoryTestCard-two")
        tryVerify(function() { return narrowSecond.y > narrowFirst.y }, 3000)

        inventoryTestView.width = 800
        tryCompare(inventoryTestView, "columnCount", 2)
        verify(Math.abs(inventoryTestView.viewWidth - 768) < 0.01)
        verify(Math.abs(inventoryTestView.cardWidth - 376) < 0.01)
        tryVerify(function() { return Math.abs(narrowFirst.y - narrowSecond.y) < 0.01 }, 3000)
        verify(Math.abs(narrowFirst.width - 376) < 0.01)
        verify(Math.abs(narrowSecond.width - 376) < 0.01)
    }

    function test_sharedEquipmentCardShowsSummaryAndPreservesNoticeSpacePolicy() {
        var summary = findChild(equipmentCardViewTest, "sharedEquipmentCardSummary")
        var notice = findChild(equipmentCardViewTest, "sharedEquipmentCardNotice")
        var actions = findChild(equipmentCardViewTest, "sharedEquipmentCardActions")
        verify(summary !== null)
        verify(notice !== null)
        verify(actions !== null)
        compare(equipmentCardViewTest.summaryItem, summary)
        compare(equipmentCardViewTest.accessibleSummary,
                "Test grinder, Steel burrs, Basket: IMS 18 g, Prep: WDT")
        verify(summary.width > 0)
        verify(summary.height > 0)
        verify(actions.width > 0)

        verify(waitForPolish(suggestionField.hostWindow))
        var hiddenHeight = equipmentCardViewTest.implicitHeight
        verify(!notice.visible)
        equipmentCardViewTest.noticeVisible = true
        tryVerify(function() { return notice.visible }, 3000)
        tryVerify(function() { return equipmentCardViewTest.implicitHeight > hiddenHeight }, 3000)
        equipmentCardViewTest.noticeVisible = false
        tryVerify(function() { return !notice.visible }, 3000)
        tryVerify(function() { return Math.abs(equipmentCardViewTest.implicitHeight - hiddenHeight) < 0.01 }, 3000)
    }

    function test_sharedEquipmentCardKeepsNativeActionsAndReferencePolicy() {
        var removeButton = findChild(equipmentCardViewTest, "sharedEquipmentRemove")
        var editButton = findChild(equipmentCardViewTest, "sharedEquipmentEdit")
        var deleteButton = findChild(equipmentCardViewTest, "sharedEquipmentDelete")
        verify(removeButton !== null)
        verify(editButton !== null)
        verify(deleteButton !== null)
        verify(removeButton.visible)
        verify(editButton.visible)
        verify(!deleteButton.visible)
        compare(removeButton.Accessible.name, "Remove test grinder")

        var editArea = findChild(editButton, "sharedIconButtonAccessibleArea")
        verify(editArea !== null)
        editArea.Accessible.pressAction()
        compare(equipmentEditSpy.count, 1)
        compare(equipmentEditSpy.signalArguments[0][0].id, 42)

        equipmentCardViewTest.pkg = ({ id: 43, name: "Unused grinder", shotCount: 0 })
        tryVerify(function() { return !removeButton.visible && deleteButton.visible }, 3000)
        var deleteArea = findChild(deleteButton, "sharedIconButtonAccessibleArea")
        verify(deleteArea !== null)
        deleteArea.Accessible.pressAction()
        compare(equipmentDeleteSpy.count, 1)
        compare(equipmentDeleteSpy.signalArguments[0][0].id, 43)

        equipmentCardViewTest.pkg = ({ id: 44, name: "Referenced grinder", shotCount: 1 })
        tryVerify(function() { return removeButton.visible && !deleteButton.visible }, 3000)
        removeButton.Accessible.pressAction()
        compare(equipmentRemoveSpy.count, 1)
        compare(equipmentRemoveSpy.signalArguments[0][0].id, 44)

        equipmentCardViewTest.actionsEnabled = false
        verify(!editButton.enabled)
        verify(!removeButton.enabled)
        equipmentCardViewTest.actionsEnabled = true
        verify(editButton.enabled)
    }

    function test_sharedSelectionDialogPreservesBrowseAccessibilityActions() {
        selectionDialogOpenedSpy.clear()
        sharedSelectionDialogTest.open()
        if (selectionDialogOpenedSpy.count === 0)
            selectionDialogOpenedSpy.wait(3000)
        tryVerify(function() { return sharedSelectionDialogTest.visible }, 3000)

        var selectedOption = findObjectNamed(sharedSelectionDialogTest, "sharedSelectionOption-1")
        verify(selectedOption !== null)
        compare(selectedOption.Accessible.name, "Grinder B")
        selectionDialogClosedSpy.clear()
        selectedOption.Accessible.pressAction()
        compare(selectionSpy.count, 1)
        compare(selectionSpy.signalArguments[0][0], 1)
        compare(selectionSpy.signalArguments[0][1], "Grinder B")
        if (selectionDialogClosedSpy.count === 0)
            selectionDialogClosedSpy.wait(3000)
        verify(!sharedSelectionDialogTest.visible)

        selectionDialogOpenedSpy.clear()
        sharedSelectionDialogTest.open()
        if (selectionDialogOpenedSpy.count === 0)
            selectionDialogOpenedSpy.wait(3000)
        tryVerify(function() { return sharedSelectionDialogTest.visible }, 3000)
        var cancelOption = findObjectNamed(sharedSelectionDialogTest, "sharedSelectionCancel")
        verify(cancelOption !== null)
        compare(cancelOption.Accessible.name, "Cancel")
        selectionDialogClosedSpy.clear()
        cancelOption.Accessible.pressAction()
        if (selectionDialogClosedSpy.count === 0)
            selectionDialogClosedSpy.wait(3000)
        verify(!sharedSelectionDialogTest.visible)
        compare(selectionSpy.count, 1)
    }

    function test_dialogCloseReleasesBrowserBackBeforeNavigation() {
        browserBackDialogTest.open()
        tryCompare(browserBackDialogTest, "visible", true)
        compare(AppShell._browserDialogs.length, 1)

        // Discard closes the dialog immediately before requesting page Back.
        // The popup's exit animation can continue, but this route now belongs
        // to the page/browser history rather than the closing modal.
        browserBackDialogTest.close()
        compare(AppShell._browserDialogs.length, 0)
        compare(AppShell.consumeBrowserBack(), false)
    }

    function test_scaledButtonBackgroundContainsItsLabel() {
        verify(waitForPolish(suggestionField.hostWindow))
        var label = findTextItem(scaledButtonTest.contentItem, "Save")
        verify(label !== null)
        var position = label.mapToItem(scaledButtonTest.background, 0, 0)
        verify(position.y >= 0, "Button label starts outside its background")
        verify(position.y + label.height <= scaledButtonTest.background.height,
               "Button label extends below its background at a small scale")
    }

    function test_buttonReleaseOutsideCancelsActivation() {
        verify(waitForPolish(suggestionField.hostWindow))
        mousePress(scaledButtonTest, scaledButtonTest.width / 2, scaledButtonTest.height / 2)
        verify(scaledButtonTest.isPressed)
        mouseMove(scaledButtonTest, -30, scaledButtonTest.height / 2)
        mouseRelease(scaledButtonTest, -30, scaledButtonTest.height / 2)
        verify(!scaledButtonTest.isPressed)
        compare(buttonClickedSpy.count, 0)
        mouseClick(scaledButtonTest, scaledButtonTest.width / 2, scaledButtonTest.height / 2)
        compare(buttonClickedSpy.count, 1)
    }

    function test_colorDragPreviewsUntilFinalCommit_data() {
        return [{ tag: "inside", releaseOutside: false },
                { tag: "outside", releaseOutside: true }]
    }

    function snapshotColor(value) {
        return Qt.rgba(value.r, value.g, value.b, value.a)
    }

    function test_colorDragPreviewsUntilFinalCommit(data) {
        colorEditorVisible = true
        verify(waitForPolish(suggestionField.hostWindow))
        const channel = findObjectNamed(colorEditorTest, "colorEditorChannel-0")
        verify(channel !== null)
        verify(channel.width > 0 && channel.height > 0)
        mousePress(channel, channel.width * 0.2, channel.height / 2)
        const firstColor = snapshotColor(colorEditorTest.color)
        mouseMove(channel, channel.width * 0.7, channel.height / 2)
        verify(!Qt.colorEqual(snapshotColor(colorEditorTest.color), firstColor))
        verify(colorEditorTest.enabled)
        compare(colorCommittedSpy.count, 0)
        const releaseX = data.releaseOutside ? channel.width + 30 : channel.width * 0.7
        mouseMove(channel, releaseX, channel.height / 2)
        const finalColor = snapshotColor(colorEditorTest.color)
        mouseRelease(channel, releaseX, channel.height / 2)
        compare(colorCommittedSpy.count, 1)
        verify(Qt.colorEqual(snapshotColor(colorCommittedSpy.signalArguments[0][0]), finalColor))
        verify(colorCommitPending)
    }

    function test_colorAccessibilityStepCommits_data() {
        return [{ tag: "rgb", rgbMode: true }, { tag: "hsl", rgbMode: false }]
    }

    function test_colorAccessibilityStepCommits(data) {
        colorEditorVisible = true
        colorEditorTest._rgbMode = data.rgbMode
        colorEditorTest.setColor(data.rgbMode ? "#4682E6" : Qt.hsla(0.5, 0.5, 0.5, 1))
        verify(waitForPolish(suggestionField.hostWindow))
        const channel = findObjectNamed(colorEditorTest, "colorEditorChannel-0")
        verify(channel !== null)
        const before = snapshotColor(colorEditorTest.color)
        channel.Accessible.increaseAction()
        verify(!Qt.colorEqual(snapshotColor(colorEditorTest.color), before))
        compare(colorCommittedSpy.count, 1)
        verify(Qt.colorEqual(colorCommittedSpy.signalArguments[0][0], colorEditorTest.color))
        verify(colorCommitPending)

        // Acknowledging that commit re-enables the next accessibility step.
        colorCommitPending = false
        channel.Accessible.decreaseAction()
        verify(Qt.colorEqual(snapshotColor(colorEditorTest.color), before))
        compare(colorCommittedSpy.count, 2)
        verify(Qt.colorEqual(snapshotColor(colorCommittedSpy.signalArguments[1][0]), before))
        verify(colorCommitPending)
    }

    function test_colorAccessibilityAtChannelLimitDoesNotCommit() {
        colorEditorVisible = true
        colorEditorTest._rgbMode = true
        colorEditorTest.setColor("#ff0000")
        verify(waitForPolish(suggestionField.hostWindow))
        const channel = findObjectNamed(colorEditorTest, "colorEditorChannel-0")
        verify(channel !== null)
        channel.Accessible.increaseAction()
        compare(colorCommittedSpy.count, 0)
        verify(!colorCommitPending)
    }

    function test_colorGrabCancellationCommitsLastPreview() {
        colorEditorVisible = true
        verify(waitForPolish(suggestionField.hostWindow))
        const channel = findObjectNamed(colorEditorTest, "colorEditorChannel-0")
        verify(channel !== null)
        mousePress(channel, channel.width * 0.2, channel.height / 2)
        mouseMove(channel, channel.width * 0.7, channel.height / 2)
        const preview = snapshotColor(colorEditorTest.color)
        compare(colorCommittedSpy.count, 0)
        colorCommitPending = true
        tryCompare(colorCommittedSpy, "count", 1)
        verify(Qt.colorEqual(snapshotColor(colorCommittedSpy.signalArguments[0][0]), preview))
        mouseRelease(channel, channel.width * 0.7, channel.height / 2)
        compare(colorCommittedSpy.count, 1)
    }

    function test_sharedSwitchWrapsLongLabelsWithoutHidingTheToggle() {
        var oldText = sharedSwitchTest.text
        var oldWidth = sharedSwitchTest.width
        try {
            sharedSwitchTest.width = 180
            sharedSwitchTest.text = "Bottom paper filter with a longer translated label"
            verify(waitForPolish(suggestionField.hostWindow))
            var label = sharedSwitchTest.contentItem
            verify(label.lineCount > 1)
            verify(!label.truncated)
            verify(label.height >= label.contentHeight)
            verify(sharedSwitchTest.height >= label.height)
            verify(sharedSwitchTest.indicator.x >= 0)
            verify(sharedSwitchTest.indicator.x + sharedSwitchTest.indicator.width <= sharedSwitchTest.width)
        } finally {
            sharedSwitchTest.text = oldText
            sharedSwitchTest.width = oldWidth
        }
    }

    function test_sharedSwitchRetainsAccessibleToggleAndNativeClickSignal() {
        compare(sharedSwitchTest.Accessible.role, Accessible.CheckBox)
        compare(sharedSwitchTest.Accessible.name, "Use puck prep")
        compare(sharedSwitchTest.Accessible.checked, false)
        mouseClick(sharedSwitchTest, sharedSwitchTest.width / 2, sharedSwitchTest.height / 2)
        tryCompare(sharedSwitchTest, "checked", true)
        compare(switchToggledSpy.count, 1)
        compare(sharedSwitchTest.Accessible.checked, true)
    }

    function test_sharedEquipmentSummaryFormatsNativeCardFields() {
        compare(equipmentSummaryViewTest.title, "Niche Zero")
        compare(equipmentSummaryViewTest.basketLine, "Basket: IMS 18 g")
        compare(equipmentSummaryViewTest.puckPrepLine, "Prep: WDT · Level")
        compare(equipmentSummaryViewTest.lastDialLine, "Grind 2.5 · 1200 rpm")
        compare(equipmentSummaryViewTest.lineageText, "Retired now")
        compare(equipmentSummaryViewTest.accessibleSummary,
                "Niche Zero, Steel burrs, Grind 2.5 · 1200 rpm, Basket: IMS 18 g, "
                + "Prep: WDT · Level, Retired now")

        var title = findTextItem(equipmentSummaryViewTest, "Niche Zero")
        verify(title !== null)
        compare(title.textFormat, Text.StyledText)
        verify(title.width > 0)

        equipmentSummaryViewTest.rpmCapable = false
        tryCompare(equipmentSummaryViewTest, "lastDialLine", "Grind 2.5")
        equipmentSummaryViewTest.grindSetting = ""
        tryCompare(equipmentSummaryViewTest, "lastDialLine", "")
        tryCompare(equipmentSummaryViewTest, "accessibleSummary",
                   "Niche Zero, Steel burrs, Basket: IMS 18 g, Prep: WDT · Level, Retired now")
    }

    function test_sharedEquipmentEditorCapturesFullEditSnapshotAndCancelDoesNotWrite() {
        fakeEquipmentPackages = [{
            id: 7, name: "Espresso kit", grinderBrand: "Niche", grinderModel: "Zero",
            grinderBurrs: "Steel", basketBrand: "IMS", basketModel: "18 g",
            puckPrepCanonical: "shaker,wdt", puckPrep_wdt: true, puckPrep_shaker: true
        }]
        sharedEquipmentDialogTest.openForEdit(fakeEquipmentPackages[0])
        tryVerify(function() { return sharedEquipmentDialogTest.visible }, 3000)

        compare(sharedEquipmentDialogTest.fName, "Espresso kit")
        compare(sharedEquipmentDialogTest.fBrand, "Niche")
        compare(sharedEquipmentDialogTest.fModel, "Zero")
        compare(sharedEquipmentDialogTest.fBurrs, "Steel")
        compare(sharedEquipmentDialogTest.fBasketBrand, "IMS")
        compare(sharedEquipmentDialogTest.fBasketModel, "18 g")
        compare(sharedEquipmentDialogTest.fPuck.wdt, true)
        compare(sharedEquipmentDialogTest.fPuck.shaker, true)
        compare(JSON.stringify(sharedEquipmentDialogTest.expectedEditable), JSON.stringify({
            name: "Espresso kit", grinderBrand: "Niche", grinderModel: "Zero",
            grinderBurrs: "Steel", basketBrand: "IMS", basketModel: "18 g",
            puckPrepCanonical: "shaker,wdt"
        }))

        sharedEquipmentDialogTest.fModel = "Unsaved draft"
        var cancelButton = findVisibleAccessibleButton(sharedEquipmentDialogTest, "Cancel")
        verify(cancelButton !== null)
        cancelButton.Accessible.pressAction()
        tryVerify(function() { return !sharedEquipmentDialogTest.visible }, 3000)
        compare(fakeEquipmentBackend.lastOperation.kind || "", "")
        compare(equipmentPackageSavedSpy.count, 0)
    }

    function test_sharedEquipmentEditorCommitsFirstComposingGearValue_data() {
        return [
            { tag: "grinder-brand", label: equipmentDialogStrings.brandLabel, field: "grinderBrand", draft: "fBrand" },
            { tag: "grinder-model", label: equipmentDialogStrings.modelLabel, field: "grinderModel", draft: "fModel" },
            { tag: "basket-brand", label: equipmentDialogStrings.basketBrandLabel, field: "basketBrand", draft: "fBasketBrand" },
            { tag: "basket-model", label: equipmentDialogStrings.basketModelLabel, field: "basketModel", draft: "fBasketModel" }
        ]
    }

    function composeEquipmentText(label, preedit) {
        const field = findAccessibleItemNamed(sharedEquipmentDialogTest, label, Accessible.EditableText)
        verify(field !== null)
        field.forceActiveFocus()
        tryCompare(field, "activeFocus", true)
        field.cursorPosition = field.text.length
        equipmentImeTarget = field
        inputMethodProbe.sendPreedit(field, preedit)
        compare(field.preeditText, preedit)
        return field
    }

    function test_sharedEquipmentEditorCommitsFirstComposingGearValue(data) {
        sharedEquipmentDialogTest.openForCreate()
        tryCompare(sharedEquipmentDialogTest, "visible", true)
        sharedEquipmentDialogTest.fBrand = ""
        sharedEquipmentDialogTest.fModel = ""
        sharedEquipmentDialogTest.fBasketBrand = ""
        sharedEquipmentDialogTest.fBasketModel = ""
        const saveButton = findVisibleAccessibleButton(sharedEquipmentDialogTest, equipmentDialogStrings.save)
        verify(saveButton !== null)
        verify(!saveButton.enabled)

        const field = composeEquipmentText(data.label, "コマンダンテ")
        compare(field.text, "")
        compare(sharedEquipmentDialogTest[data.draft], "")
        verify(!sharedEquipmentDialogTest.canSave)
        tryCompare(saveButton, "enabled", true)
        saveButton.Accessible.pressAction()

        compare(equipmentCommitSpy.count, 1)
        compare(field.preeditText, "")
        compare(sharedEquipmentDialogTest[data.draft], "コマンダンテ")
        compare(fakeEquipmentBackend.lastOperation.kind, "create")
        compare(fakeEquipmentBackend.lastOperation.fields[data.field], "コマンダンテ")
        verify(sharedEquipmentDialogTest.saving)
        sharedEquipmentDialogTest.save()
        compare(equipmentCommitSpy.count, 1)
    }

    function test_sharedEquipmentEditorRevalidatesCommittedComposition_data() {
        return [
            { tag: "resolves-equipment-duplicate", label: equipmentDialogStrings.modelLabel,
              initialModel: "Zero", preedit: " II", draft: "fModel", result: "Zero II",
              initiallyValid: false, valid: true },
            { tag: "creates-equipment-duplicate", label: equipmentDialogStrings.modelLabel,
              initialModel: "", preedit: "Zero", draft: "fModel", result: "Zero",
              initiallyValid: true, valid: false },
            { tag: "creates-name-duplicate", label: equipmentDialogStrings.nameLabel,
              initialModel: "Other", preedit: "Travel", draft: "fName", result: "Travel",
              initiallyValid: true, valid: false }
        ]
    }

    function test_sharedEquipmentEditorRevalidatesCommittedComposition(data) {
        fakeEquipmentPackages = [{ id: 7, name: "Travel", grinderBrand: "Acme", grinderModel: "Zero" }]
        sharedEquipmentDialogTest.openForCreate()
        tryCompare(sharedEquipmentDialogTest, "visible", true)
        sharedEquipmentDialogTest.fBrand = "Acme"
        sharedEquipmentDialogTest.fModel = data.initialModel
        compare(sharedEquipmentDialogTest.canSave, data.initiallyValid)
        composeEquipmentText(data.label, data.preedit)
        const saveButton = findVisibleAccessibleButton(sharedEquipmentDialogTest, equipmentDialogStrings.save)
        verify(saveButton !== null)
        tryCompare(saveButton, "enabled", true)
        saveButton.Accessible.pressAction()

        compare(equipmentCommitSpy.count, 1)
        compare(sharedEquipmentDialogTest[data.draft], data.result)
        compare(sharedEquipmentDialogTest.canSave, data.valid)
        compare(sharedEquipmentDialogTest.saving, data.valid)
        compare(fakeEquipmentBackend.lastOperation.kind || "", data.valid ? "create" : "")
        verify(sharedEquipmentDialogTest.visible)
        if (data.valid)
            compare(fakeEquipmentBackend.lastOperation.fields.grinderModel, data.result)
    }

    function test_sharedEquipmentEditorForwardsCreateDraftAndActiveBagChoice() {
        sharedEquipmentDialogTest.applyToActiveBag = false
        sharedEquipmentDialogTest.openForCreate()
        tryVerify(function() { return sharedEquipmentDialogTest.visible }, 3000)
        compare(sharedEquipmentDialogTest.fBrand, "Seed brand")
        compare(sharedEquipmentDialogTest.fModel, "Seed model")

        sharedEquipmentDialogTest.fName = "Travel setup"
        sharedEquipmentDialogTest.fBrand = "Comandante"
        sharedEquipmentDialogTest.fModel = "C40"
        sharedEquipmentDialogTest.fBasketBrand = "IMS"
        sharedEquipmentDialogTest.fBasketModel = "18 g"
        sharedEquipmentDialogTest.setPuck("wdt", true)
        sharedEquipmentDialogTest.save()

        compare(fakeEquipmentBackend.lastOperation.kind, "create")
        compare(fakeEquipmentBackend.lastOperation.applyToActiveBag, false)
        compare(fakeEquipmentBackend.lastOperation.fields.name, "Travel setup")
        compare(fakeEquipmentBackend.lastOperation.fields.grinderBrand, "Comandante")
        compare(fakeEquipmentBackend.lastOperation.fields.grinderModel, "C40")
        compare(fakeEquipmentBackend.lastOperation.fields.basketBrand, "IMS")
        compare(fakeEquipmentBackend.lastOperation.fields.basketModel, "18 g")
        compare(fakeEquipmentBackend.lastOperation.fields.puckPrep_wdt, true)
        verify(fakeEquipmentBackend.lastOperation.requestId.length > 0)
        verify(sharedEquipmentDialogTest.visible)
        verify(sharedEquipmentDialogTest.saving)

        fakeEquipmentBackend.completeOperation(fakeEquipmentBackend.lastOperation.requestId,
                                               true, 19, "")
        tryVerify(function() { return !sharedEquipmentDialogTest.visible }, 3000)
        verify(!sharedEquipmentDialogTest.saving)
    }

    function test_sharedEquipmentEditorIgnoresOtherRequestAndClosesOnMatchingSuccess() {
        sharedEquipmentDialogTest.applyToActiveBag = false
        sharedEquipmentDialogTest.openForCreate()
        sharedEquipmentDialogTest.fBrand = "Niche"
        sharedEquipmentDialogTest.fModel = "Zero"
        sharedEquipmentDialogTest.save()
        var requestId = fakeEquipmentBackend.lastOperation.requestId
        verify(requestId.length > 0)

        fakeEquipmentBackend.completeOperation("another-request", true, 11, "")
        verify(sharedEquipmentDialogTest.visible)
        verify(sharedEquipmentDialogTest.saving)
        compare(equipmentPackageSavedSpy.count, 0)

        fakeEquipmentBackend.completeOperation(requestId, true, 11, "")
        tryVerify(function() { return !sharedEquipmentDialogTest.visible }, 3000)
        verify(!sharedEquipmentDialogTest.saving)
        compare(equipmentPackageSavedSpy.count, 1)
        compare(equipmentPackageSavedSpy.signalArguments[0][0], 11)
    }

    function test_sharedEquipmentEditorConflictKeepsDraftAndShowsRecoveryText() {
        var pkg = {
            id: 7, name: "Espresso kit", grinderBrand: "Niche", grinderModel: "Zero",
            grinderBurrs: "Steel", basketBrand: "IMS", basketModel: "18 g",
            puckPrepCanonical: "wdt"
        }
        fakeEquipmentPackages = [pkg]
        sharedEquipmentDialogTest.openForEdit(pkg)
        sharedEquipmentDialogTest.fModel = "C40"
        sharedEquipmentDialogTest.fBasketModel = "20 g"
        var draftBeforeRefresh = {
            model: sharedEquipmentDialogTest.fModel,
            basketModel: sharedEquipmentDialogTest.fBasketModel
        }
        var snapshotBeforeRefresh = JSON.stringify(sharedEquipmentDialogTest.expectedEditable)
        var refreshCountBefore = fakeEquipmentBackend.refreshCount

        fakeEquipmentPackages = [{
            id: 7, name: "Concurrent inventory update", grinderBrand: "Niche",
            grinderModel: "Updated elsewhere", grinderBurrs: "Steel",
            basketBrand: "IMS", basketModel: "Updated basket",
            puckPrepCanonical: "wdt"
        }]
        fakeEquipmentBackend.packagesInvalidated()
        compare(fakeEquipmentBackend.refreshCount, refreshCountBefore + 1)
        compare(sharedEquipmentDialogTest.fModel, draftBeforeRefresh.model)
        compare(sharedEquipmentDialogTest.fBasketModel, draftBeforeRefresh.basketModel)
        compare(JSON.stringify(sharedEquipmentDialogTest.expectedEditable), snapshotBeforeRefresh)

        sharedEquipmentDialogTest.save()
        var requestId = fakeEquipmentBackend.lastOperation.requestId
        compare(fakeEquipmentBackend.lastOperation.kind, "update")
        compare(fakeEquipmentBackend.lastOperation.expectedEditable.grinderModel, "Zero")
        compare(fakeEquipmentBackend.lastOperation.expectedEditable.basketModel, "18 g")

        fakeEquipmentBackend.completeOperation(requestId, false, 7, "conflict")
        tryVerify(function() { return !sharedEquipmentDialogTest.saving }, 3000)
        verify(sharedEquipmentDialogTest.visible)
        compare(sharedEquipmentDialogTest.fModel, "C40")
        compare(sharedEquipmentDialogTest.fBasketModel, "20 g")
        compare(sharedEquipmentDialogTest.saveError, equipmentDialogStrings.equipmentChanged)
        verify(findVisibleTextItem(sharedEquipmentDialogTest, equipmentDialogStrings.equipmentChanged) !== null)
        compare(equipmentPackageSavedSpy.count, 0)
    }

}
