pragma ComponentBehavior: Bound

import QtQml
import Decenza
// Local editor state for the Hot Water and Flush pages. Slider changes stay in
// this draft until release; the host validates and applies the complete row in
// one revision-checked operation.
QtObject {
    id: root

    required property string domain

    readonly property bool waterVessel: domain === "waterVessel"
    readonly property var presets: RecipeContext.preferences[
        waterVessel ? "waterVesselPresets" : "flushPresets"] || []
    readonly property int selectedIndex: Number(RecipeContext.preferences[
        waterVessel ? "selectedWaterVessel" : "selectedFlushPreset"] ?? -1)
    readonly property var selectedPreset: presetAt(selectedIndex)

    property int draftIndex: -1
    property double draftRevision: 0
    property string expectedName: ""
    property var values: ({})
    property bool dirty: false
    property bool pending: false
    property bool stale: false
    property string pendingRequestId: ""
    property string pendingOperation: ""
    property string errorMessage: ""

    signal completed(string operation, bool success, string reason)

    Component.onCompleted: begin(selectedIndex)

    property Connections recipeContextConnections: Connections {
        target: RecipeContext

        function onPreferencesChanged() {
            if (!root.dirty && !root.pending)
                root.begin(root.selectedIndex)
        }

        function onPresetEditCompleted(requestId, domain, operation, success,
                                       revision, preferences, reason) {
            if (requestId !== root.pendingRequestId
                || domain !== root.domain
                || operation !== root.pendingOperation)
                return

            const completedOperation = root.pendingOperation
            root.pending = false
            root.pendingRequestId = ""
            root.pendingOperation = ""
            if (success) {
                root.errorMessage = ""
                root.stale = false
            } else {
                root.recordFailure(reason)
            }
            if (success)
                root.begin(root.selectedIndex)
            root.completed(completedOperation, success, reason)
        }
    }

    function presetAt(index) {
        if (index < 0 || index >= root.presets.length)
            return ({})
        return root.presets[index] || ({})
    }

    function nameTaken(name, ignoreIndex) {
        if (!root.waterVessel)
            return false
        const normalized = String(name || "").trim().toLowerCase()
        if (!normalized)
            return false
        for (let i = 0; i < root.presets.length; ++i) {
            if (i !== ignoreIndex
                    && String(root.presets[i].name || "").trim().toLowerCase() === normalized)
                return true
        }
        return false
    }

    function begin(index) {
        const preset = root.presetAt(index)
        root.draftIndex = index >= 0 && index < root.presets.length ? index : -1
        root.draftRevision = Number(RecipeContext.revision)
        root.expectedName = String(preset.name || "")
        root.values = Object.assign({}, preset)
        root.dirty = false
        root.stale = false
        root.errorMessage = ""
    }

    function recordFailure(reason) {
        root.errorMessage = String(reason || "")
        root.stale = root.errorMessage === "stalePreferences"
    }

    // Discard the local draft only after the user explicitly chooses to reload
    // the host's latest preset snapshot.
    function reload() {
        if (root.pending || !RecipeContext.ready)
            return false
        root.begin(root.selectedIndex)
        return true
    }

    function setValue(key, value) {
        const next = Object.assign({}, root.values)
        next[key] = value
        root.values = next
        root.dirty = true
        root.errorMessage = ""
    }

    function submit(operation, fields, expectedRevision) {
        if (root.pending)
            return false
        if (!RecipeContext.ready) {
            root.errorMessage = "unavailable"
            root.completed(operation, false, root.errorMessage)
            return false
        }
        const requestId = RecipeContext.newRequestId()
        root.pendingRequestId = requestId
        root.pendingOperation = operation
        root.pending = true
        root.errorMessage = ""
        RecipeContext.executePresetEdit(requestId, root.domain, operation, fields,
                                        expectedRevision !== undefined
                                                ? expectedRevision : root.draftRevision)
        return true
    }

    function commit() {
        if (root.draftIndex < 0 || root.draftIndex !== root.selectedIndex)
            return false

        let fields
        if (root.waterVessel) {
            fields = {
                index: root.draftIndex,
                expectedName: root.expectedName,
                name: String(root.values.name || ""),
                volume: root.values.volume,
                mode: String(root.values.mode || "weight"),
                flowRate: root.values.flowRate,
                temperature: root.values.temperature
            }
        } else {
            fields = {
                index: root.draftIndex,
                expectedName: root.expectedName,
                name: String(root.values.name || ""),
                flow: root.values.flow,
                seconds: root.values.seconds
            }
        }
        return root.submit("commit", fields)
    }

    function updateName(index, name, capturedName, capturedRevision) {
        const preset = presetAt(index)
        if (index < 0 || !preset.name)
            return false
        const fields = {index: index,
                        expectedName: String(capturedName !== undefined ? capturedName : preset.name),
                        name: String(name)}
        if (waterVessel) {
            fields.volume = preset.volume
            fields.mode = String(preset.mode || "weight")
            fields.flowRate = preset.flowRate !== undefined ? preset.flowRate : 40
            fields.temperature = preset.temperature !== undefined
                    ? preset.temperature : Number(RecipeContext.preferences.waterTemperature)
        } else {
            fields.flow = preset.flow
            fields.seconds = preset.seconds
        }
        return submit("update", fields, capturedRevision)
    }

    function add(name, values, capturedRevision) {
        const fields = {name: String(name)}
        if (waterVessel) {
            fields.volume = values.volume
            fields.mode = String(values.mode || "weight")
            fields.flowRate = values.flowRate
            fields.temperature = values.temperature
        } else {
            fields.flow = values.flow
            fields.seconds = values.seconds
        }
        return submit("add", fields, capturedRevision)
    }

    function remove(index, capturedName, capturedRevision) {
        const preset = presetAt(index)
        return index >= 0 && preset.name
                ? submit("remove", {index: index,
                                     expectedName: String(capturedName !== undefined ? capturedName : preset.name)},
                         capturedRevision) : false
    }

    function move(fromIndex, toIndex) {
        const from = presetAt(fromIndex)
        const to = presetAt(toIndex)
        return fromIndex >= 0 && toIndex >= 0 && from.name && to.name
                ? submit("move", {fromIndex: fromIndex, expectedFromName: String(from.name),
                                  toIndex: toIndex, expectedToName: String(to.name)}, draftRevision) : false
    }
}
