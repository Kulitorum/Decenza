pragma ComponentBehavior: Bound

import QtQml
import Decenza

// One correlated preference write per control; state is always read from the host.
QtObject {
    id: action
    property bool historyPreferences: false
    // Non-empty selects SettingsPreferencesService; otherwise writes go through
    // HistoryService or RecipeContext as before.
    property string domain: ""
    // Dialog drafts can retain the revision they opened against.
    property var expectedRevision: undefined
    property string pendingRequestId: ""
    property string pendingName: ""
    property var pendingValue: undefined
    readonly property bool busy: pendingRequestId.length > 0
    readonly property bool ready: (historyPreferences ? HistoryService.ready
                              : domain.length > 0 ? SettingsPreferencesService.ready
                              : RecipeContext.ready) && !busy
    signal failed(string message)
    signal completed(bool success)

    // Keep a control on the submitted value while a request is outstanding.
    // Clearing it on completion restores the latest host-backed binding after
    // a refusal instead of leaving the switch in its optimistic state.
    function displayedValue(name: string, hostValue: var): var {
        return busy && pendingName === name ? pendingValue : hostValue
    }

    function submit(name: string, value: var): bool {
        if (!ready) return false
        const revision = expectedRevision !== undefined ? expectedRevision
            : historyPreferences ? HistoryService.preferencesRevision
            : domain.length > 0 ? SettingsPreferencesService.revisions[domain]
            : RecipeContext.revision
        pendingName = name
        pendingValue = value
        if (historyPreferences) {
            pendingRequestId = HistoryService.setPreference(name, value, revision)
        } else if (domain.length > 0) {
            pendingRequestId = SettingsPreferencesService.newRequestId()
            SettingsPreferencesService.setPreference(pendingRequestId, domain, name, value, revision)
        } else {
            pendingRequestId = RecipeContext.newRequestId()
            RecipeContext.setPreference(pendingRequestId, name, value, revision)
        }
        return true
    }

    function complete(requestId: string, success: bool) {
        if (requestId !== pendingRequestId) return
        pendingRequestId = ""
        pendingName = ""
        pendingValue = undefined
        completed(success)
        if (!success)
            failed(TranslationManager.translate("common.settingsChangedRetry",
                "The setting could not be saved. Review the current value and try again."))
    }

    readonly property Connections historyCompletion: Connections {
        target: HistoryService
        function onOperationCompleted(requestId, operation, success, result, reason) {
            if (action.historyPreferences && operation === "preference")
                action.complete(requestId, success)
        }
    }

    readonly property Connections completion: Connections {
        target: RecipeContext
        function onPreferenceCompleted(requestId, name, success, preferences, reason) {
            if (!action.historyPreferences && action.domain.length === 0)
                action.complete(requestId, success)
        }
    }

    readonly property Connections settingsCompletion: Connections {
        target: SettingsPreferencesService
        function onPreferenceCompleted(requestId, completedDomain, name, success, revision, reason) {
            if (!action.historyPreferences && action.domain.length > 0
                    && completedDomain === action.domain)
                action.complete(requestId, success)
        }
    }
}
