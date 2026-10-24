// The contract between a page and the application shell.
//
// Pages used to reach main.qml's root object directly, by the names it happens to
// declare — a bare "root dot goBack", "root dot stopReason". That works only because
// StackView creates each page with main.qml's context, so the id is found at runtime
// by name lookup. It made 120 call sites across 26 page files invisible to qmllint,
// qmlcachegen and the language server: a misspelled member is indistinguishable from
// a correct one until a user hits it, and a member main.qml does not have at all is
// never flagged. ScreensaverPage's RSS ceiling sat broken that way for months,
// comparing against `undefined` on every video transition.
//
// So the shell surface is declared here instead, where it is a real type.
//
// NAVIGATION IS SIGNALS, NOT FUNCTIONS. Each application instance mounts the same
// SharedAppNavigation StackView, destination catalog and re-entrancy guard.
// Native window/device lifecycle and browser history stay in their respective
// roots. A page states what it wants; the shell decides how to enter it, without
// coupling the page to either root or another client's stack.
//
// STATE IS OWNED HERE, NOT MIRRORED. The properties below have exactly one owner.
// main.qml reads and writes them like any other participant, rather than declaring
// its own copy — two owners of one flag is how they drift.
//
// What deliberately did NOT move here: `cleanForSpeech()` lives on
// AccessibilityManager, where a screen-reader text helper belongs and where it can
// be unit-tested. Do not let this file become the place everything global lands —
// that is the flat-`Settings` mistake the project already had to reverse.
pragma Singleton

import QtQuick

QtObject {
    id: shell

    // ---- Navigation ----------------------------------------------------------
    // Emitted by a page, handled by its local shell. Named for the request, not the
    // action, because the page is not the thing that performs it.

    signal backRequested()
    // A page's asynchronous leave guard explicitly declined the browser's
    // pending route change. The browser shell uses this to discard only its
    // own correlated pop request; native navigation ignores it.
    signal browserBackCancelled()
    signal idleFromScreensaverRequested()
    signal profileEditorRequested()
    signal profileDraftEditorRequested(string filename, string editorType)
    signal newProfileDraftRequested(string editorType, string title)
    signal profileSelectorRequested()
    signal profileImportRequested()
    signal visualizerBrowserRequested()
    signal descalingRequested()
    signal transportRequested()
    // sensor indexes the SensorCalibration table; the shell pushes the guided
    // page with it, so one page serves both sensors.
    signal sensorCalibrationRequested(int sensor)
    signal brewSettingsRequested()
    // Three widgets reached this through `Window.window` and a
    // `typeof win.goToScreensaver === "function"` probe — a duck-typed call on QQuickWindow that
    // no tool could check and that silently did nothing if the name ever changed.
    signal screensaverRequested()
    // A quit that asks first: Sleep's long-press with its "Ask before quitting" option on.
    signal quitRequested()
    // The native shell handles application exit; browser shells ignore it.
    signal immediateQuitRequested()

    // Destination requests. Named per destination rather than one generic
    // `pageRequested(url, props)`, because a string page name is not checked by anything and the
    // codebase had four different spellings of the same path plus one built by concatenation
    // (`"../../../pages/" + page`). An emitter's signal name is checked at build time; main.qml's
    // handler names are not (a Connections function matching no signal only warns at runtime,
    // qqmlconnections.cpp:443), so tst_customwidgethtml pairs every signal here with a handler.
    //
    // These carry INTENT, not policy: the shell decides how the page is entered
    // (QML_NAVIGATION.md).
    signal espressoRequested()
    signal steamRequested()
    signal hotWaterRequested()
    signal flushRequested()
    signal settingsRequested(string tabId)
    signal dflowEditorRequested()
    signal recipesRequested()
    // options carries the wizard's own properties: promoteShotId, editRecipeId, prefill.
    signal recipeWizardRequested(string mode, var options)
    signal shotHistoryRequested(var filter)
    signal shotDetailRequested(int shotId, var shotIds)
    signal shotComparisonRequested()
    signal postShotReviewRequested(int shotId, bool autoClose)
    signal profileInfoRequested(string profileFilename, string profileName)
    signal beanInfoRequested()
    signal equipmentRequested()
    signal autoFavoritesRequested()
    signal autoFavoriteInfoRequested(var options)
    signal communityBrowserRequested()
    signal visualizerMultiImportRequested()
    signal flowCalibrationRequested()
    signal aiSettingsRequested()
    signal stringBrowserRequested()
    signal addLanguageRequested()

    // "Leave this page, however you can." A pop, or home when nothing is underneath. The page
    // says it wants out and the shell picks.
    signal dismissRequested()

    // ---- Operation-completion handshake --------------------------------------
    // A page that opens a dialog over a finishing operation suspends the
    // "complete" overlay, then releases it when the dialog closes. Without the
    // pair, the overlay suppression latches and every later operation is silent.

    signal completionSuspendRequested()
    signal completionFinishRequested()

    // ---- Shell state ---------------------------------------------------------

    // Client-local modal ownership. SharedDecenzaDialog registers on opening,
    // updates the two policy flags as its form changes, and unregisters on
    // close/destruction. Order is opening order, so Back consumes only the
    // topmost dialog and never closes unrelated dialogs underneath it. The
    // callback is supplied by that shared base, not discovered on arbitrary QML.
    property var _browserDialogs: []
    property bool browserDialogDraftsDirty: false

    function registerBrowserDialog(dialog: var, dismissible: bool,
                                   dirty: bool, dismiss: var): void {
        var next = _browserDialogs.filter(function(entry) { return entry.dialog !== dialog })
        next.push({dialog: dialog, dismissible: dismissible, dirty: dirty, dismiss: dismiss})
        _browserDialogs = next
        updateBrowserDialogDraftsDirty()
    }

    function updateBrowserDialog(dialog: var, dismissible: bool, dirty: bool): void {
        var next = _browserDialogs.slice()
        for (var i = 0; i < next.length; ++i) {
            if (next[i].dialog === dialog) {
                next[i] = Object.assign({}, next[i], {dismissible: dismissible, dirty: dirty})
                _browserDialogs = next
                updateBrowserDialogDraftsDirty()
                return
            }
        }
    }

    function unregisterBrowserDialog(dialog: var): void {
        var next = _browserDialogs.filter(function(entry) { return entry.dialog !== dialog })
        if (next.length === _browserDialogs.length)
            return
        _browserDialogs = next
        updateBrowserDialogDraftsDirty()
    }

    function updateBrowserDialogDraftsDirty(): void {
        browserDialogDraftsDirty = _browserDialogs.some(function(entry) { return entry.dirty })
    }

    // Returns true whenever a modal owns Back, including a non-dismissible
    // host confirmation. The dialog itself applies its established reject
    // semantics; a refusal/pending operation simply stays on top.
    function consumeBrowserBack(): bool {
        if (_browserDialogs.length === 0)
            return false
        const top = _browserDialogs[_browserDialogs.length - 1]
        if (top.dismissible)
            top.dismiss()
        return true
    }

    // Why the espresso operation ended: "manual", "weight", "machine", or "".
    // Drives the stop-reason overlay that draws above every page.
    property string stopReason: ""

    // Set by FlushPage's back-arrow / STOP handlers to suppress the 1.5 s "Flush
    // Complete" overlay when the user explicitly chose to leave. Single-shot:
    // cleared on the next Idle/Ready transition regardless of current page.
    property bool userExitedFlush: false

    // Development toggle: force operation pages into their live view without a
    // machine attached.
    property bool debugLiveView: false

    // A scale dialog wanted to open while the screensaver was up. Held until the
    // user wakes the app, so the dialog is not shown to an empty room.
    property bool scaleDialogDeferred: false

    // A brew was requested while something else still had to be confirmed.
    property bool pendingBrewDialog: false

    // The page currently on top of the stack, published by main.qml on every change. Read by
    // widgets that live OUTSIDE the page stack — the persistent status bar — and so cannot find
    // the page by walking their own parent chain. Null before the first page is shown.
    property Item currentPage: null
}
