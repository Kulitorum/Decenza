pragma Singleton

import QtQuick
import Decenza

// One select/second-tap policy for Home rows and compact layout pickers.
QtObject {
    id: actions

    property var pendingControl: []
    property var pendingProfiles: []
    property var pendingRecipes: []
    property var pendingConnections: []
    readonly property int selectedFavoriteIndex: {
        const filename = ProfileService.currentProfileSummary.filename || ""
        const favorites = ProfileService.favoriteProfiles
        for (let i = 0; i < favorites.length; ++i) {
            if (favorites[i].filename === filename) return i
        }
        return -1
    }

    function trackControl(requestId: string): void {
        if (requestId) pendingControl = pendingControl.concat([requestId])
    }

    function selectPreparationMode(mode: string): bool {
        if (!PreparationService.state.available) {
            failed(TranslationManager.translate("idle.operationFailed",
                "The selection could not be changed. Please try again."))
            return false
        }
        return PreparationService.state.selectMode(mode)
    }

    function sleepMachine() {
        trackControl(MachineControlService.sleepMachine(SteamService.state.controlContextId))
    }
    function wakeMachine() {
        trackControl(MachineControlService.wakeMachine(SteamService.state.controlContextId))
    }
    function requestIdle() {
        trackControl(MachineControlService.requestIdle(SteamService.state.controlContextId))
    }
    function disconnectMachine() {
        trackControl(MachineControlService.disconnectMachine(SteamService.state.controlContextId))
    }
    function generateFakeShot() {
        trackControl(MachineControlService.generateFakeShot(SteamService.state.controlContextId))
    }
    function scanForDevices() {
        trackControl(MachineControlService.scanForDevices())
    }
    function tareScale() {
        pendingConnections = pendingConnections.concat([SettingsConnectionsService.execute("tareScale")])
    }
    function reconnectPortal() {
        pendingConnections = pendingConnections.concat([SettingsConnectionsService.execute("reconnectPortal")])
    }
    function toggleSteamHeater() {
        trackControl(MachineControlService.toggleSteamHeater(SteamService.state.controlContextId))
    }
    function toggleCharging() {
        trackControl(MachineControlService.toggleCharging(SteamService.state.controlContextId))
    }
    function loadPreviousProfile() {
        trackControl(MachineControlService.loadPreviousProfile(SteamService.state.controlContextId))
    }

    function applyTemperatureOverride(valueC: double): void {
        const expectedOverride = {
            hasOverride: Settings.brew.hasTemperatureOverride,
            valueC: Settings.brew.temperatureOverride
        }
        const summary = ProfileService.currentProfileSummary
        const expectedProfile = {
            filename: summary.filename || "",
            title: summary.title || ""
        }
        trackControl(MachineControlService.applyTemperatureOverride(
            SteamService.state.controlContextId, expectedProfile, expectedOverride, valueC))
    }

    function loadProfile(filename: string): void {
        pendingProfiles = pendingProfiles.concat([ProfileService.loadProfile(filename)])
    }

    function tapProfile(filename: string): void {
        if (filename === ProfileService.currentProfileSummary.filename)
            startEspresso()
        else
            loadProfile(filename)
    }

    function tapRecipe(recipeId: double): void {
        if (recipeId === MachineControlService.selectedRecipeId) {
            trackControl(MachineControlService.startRecipe(SteamService.state.controlContextId, recipeId))
        } else {
            pendingRecipes = pendingRecipes.concat([recipeId])
            RecipeService.requestActivateRecipe(recipeId)
        }
    }

    function selectSteamPitcher(index: int): void {
        trackControl(MachineControlService.selectSteamPitcher(index, SteamService.getSteamPitcherPreset(index)))
    }

    function tapSteamPitcher(index: int): void {
        const preset = SteamService.getSteamPitcherPreset(index)
        if (!preset.disabled && index === SteamService.state.selectedSteamPitcherDisplayIndex)
            startSteam()
        else
            selectSteamPitcher(index)
    }

    function tapWaterVessel(index: int): void {
        if (index === RecipeContext.preferences.selectedWaterVessel)
            startHotWater()
        else
            trackControl(MachineControlService.selectWaterVessel(index, (RecipeContext.preferences.waterVesselPresets || [])[index] || {}))
    }

    function tapFlushPreset(index: int): void {
        if (index === RecipeContext.preferences.selectedFlushPreset)
            startFlush()
        else
            trackControl(MachineControlService.selectFlushPreset(index, (RecipeContext.preferences.flushPresets || [])[index] || {}))
    }

    function startEspresso() {
        if (MachineControlService.selectedRecipeId > 0) {
            trackControl(MachineControlService.startRecipe(SteamService.state.controlContextId,
                MachineControlService.selectedRecipeId))
        } else {
            trackControl(MachineControlService.startEspresso(SteamService.state.controlContextId,
                ProfileService.currentProfileSummary.filename || ""))
        }
    }
    function startSteam() {
        trackControl(MachineControlService.startSteam(SteamService.state.controlContextId,
            SteamService.state.selectedSteamPitcher,
            SteamService.getSteamPitcherPreset(SteamService.state.selectedSteamPitcher)))
    }
    function startHotWater() {
        trackControl(MachineControlService.startHotWater(SteamService.state.controlContextId,
            RecipeContext.preferences.selectedWaterVessel,
            (RecipeContext.preferences.waterVesselPresets || [])[RecipeContext.preferences.selectedWaterVessel] || {}))
    }
    function startFlush() {
        trackControl(MachineControlService.startFlush(SteamService.state.controlContextId,
            RecipeContext.preferences.selectedFlushPreset,
            (RecipeContext.preferences.flushPresets || [])[RecipeContext.preferences.selectedFlushPreset] || {}))
    }

    function finish(kind: string, requestId: var, success: bool, message: string): void {
        const ids = actions[kind]
        const index = ids.indexOf(requestId)
        if (index < 0) return
        const remaining = ids.slice()
        remaining.splice(index, 1)
        actions[kind] = remaining
        if (!success) failed(message || TranslationManager.translate(
            "idle.operationFailed", "The selection could not be changed. Please try again."))
    }

    property Connections preparationResults: Connections {
        target: PreparationService
        function onCompleted(requestId, action, success, message) {
            if (!success && PreparationService.state.isPreparationMode(action))
                actions.failed(message || TranslationManager.translate("idle.operationFailed",
                    "The selection could not be changed. Please try again."))
        }
    }
    property Connections controlResults: Connections {
        target: MachineControlService
        function onCompleted(requestId, operation, success, message, result) {
            if (operation === "generate-fake-shot" && success
                    && actions.pendingControl.indexOf(requestId) >= 0 && result.shotId > 0)
                AppShell.postShotReviewRequested(result.shotId, true)
            actions.finish("pendingControl", requestId, success, message)
        }
    }
    property Connections profileResults: Connections {
        target: ProfileService
        function onOperationCompleted(requestId, operation, filename, success, reason, result) {
            actions.finish("pendingProfiles", requestId, success, reason)
        }
    }
    property Connections connectionResults: Connections {
        target: SettingsConnectionsService
        function onOperationCompleted(requestId, operation, success, result, reason) {
            actions.finish("pendingConnections", requestId, success, reason)
        }
    }
    property Connections recipeResults: Connections {
        target: RecipeService
        function onRecipeActivated(recipeId, success) {
            actions.finish("pendingRecipes", recipeId, success, RecipeService.operationError)
        }
    }

    signal failed(string message)
}
