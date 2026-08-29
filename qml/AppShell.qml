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
// NAVIGATION IS SIGNALS, NOT FUNCTIONS. main.qml owns the StackView and keeps
// every line of the navigation logic — the `startNavigation()` re-entrancy guard,
// `returnToPageName`/`returnToShotId`, the operation-page `replace` — because that
// is the most delicate code in the app and moving it here would buy nothing. A page
// states what it wants; the shell decides how. That also means a page can be loaded
// and reasoned about without main.qml existing at all.
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
    // Emitted by a page, handled by main.qml. Named for the request, not the
    // action, because the page is not the thing that performs it.

    signal backRequested()
    signal idleRequested()
    // Distinct from idleRequested: leaving the screensaver restores the page the
    // user was on rather than replacing the stack with idle.
    signal idleFromScreensaverRequested()
    signal profileEditorRequested()
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

    // Destination requests. Named per destination rather than one generic
    // `pageRequested(url, props)`, because a string page name is not checked by anything and the
    // codebase had four different spellings of the same path plus one built by concatenation
    // (`"../../../pages/" + page`). A signal name is checked at build time.
    //
    // These carry INTENT, not policy: main.qml decides push vs replace. See the note on its
    // navigation functions for the rule (machine-driven replaces, user-driven pushes).
    signal espressoRequested()
    signal steamRequested()
    signal hotWaterRequested()
    signal flushRequested()
    signal settingsRequested(string tabId)
    signal recipeEditorRequested()
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

    // "Leave this page, however you can." Operation pages can be arrived at two ways — pushed
    // when the user navigated, replaced when the machine drove a phase change — so back is
    // available in one case and not the other. The page should not have to inspect the stack to
    // find out; it says it wants out and the shell picks.
    signal dismissRequested()

    // ---- Operation-completion handshake --------------------------------------
    // A page that opens a dialog over a finishing operation suspends the
    // "complete" overlay, then releases it when the dialog closes. Without the
    // pair, the overlay suppression latches and every later operation is silent.

    signal completionSuspendRequested()
    signal completionFinishRequested()

    // ---- Shell state ---------------------------------------------------------

    // Why the espresso operation ended: "manual", "weight", "machine", or "".
    // Drives the stop-reason overlay that draws above every page.
    property string stopReason: ""

    // Set by FlushPage's back-arrow / STOP handlers to suppress the 1.5 s "Flush
    // Complete" overlay when the user explicitly chose to leave. Single-shot:
    // cleared on the next Idle/Ready transition regardless of current page.
    property bool userExitedFlush: false

    // Auto-flush countdown, produced by the shell's timer and displayed on SteamPage.
    property real steamAutoFlushCountdown: 0

    // Development toggle: force operation pages into their live view without a
    // machine attached.
    property bool debugLiveView: false

    // A scale dialog wanted to open while the screensaver was up. Held until the
    // user wakes the app, so the dialog is not shown to an empty room.
    property bool scaleDialogDeferred: false

    // A brew was requested while something else still had to be confirmed.
    property bool pendingBrewDialog: false

    // Net milk weighed during this steam session, 0 when none has been. Written by
    // the home screen's and the steam page's auto-capture, read by the steam plan,
    // the milk-weight widget and SteamPage's scaling fallback. Reset on pitcher
    // change and at session end.
    //
    // This was main.qml's, and every consumer reached it through `Window.window`,
    // which qmllint types as QQuickWindow — so `window.sessionMeasuredMilkG` was an
    // unchecked member access and a rename would have been silent. SteamPlanText
    // knew it: it carried a `_warnedMissingMilkProp` console.warn and an
    // `ignoreUnknownSignals` Connections whose only purpose was to survive exactly
    // that. Both are deleted now — a rename here is a build-time failure at every
    // call site instead.
    property real sessionMeasuredMilkG: 0

    // The page currently on top of the stack, published by main.qml on every change. Read by
    // widgets that live OUTSIDE the page stack — the persistent status bar — and so cannot find
    // the page by walking their own parent chain. Null before the first page is shown.
    property Item currentPage: null
}
