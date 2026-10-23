import QtQml
import Decenza

// One QML adapter for the native EquipmentService replica on both hosts.
// Keep suggestion normalization here so the page and editor stay host-neutral.
EquipmentUiBackend {
    id: root

    // Dialog-local counters are intentionally simple. Prefix at the service
    // boundary so two simultaneously instantiated pages cannot complete one
    // another's pending editor operation.
    readonly property string serviceRequestPrefix: EquipmentService.newRequestId() + ":"
    readonly property var options: EquipmentService.options

    packages: EquipmentService.packages
    activePackageId: EquipmentService.activePackageId
    puckPrepRows: EquipmentService.puckPrepFlags.map(function(flag) {
        return { key: flag.key, label: TranslationManager.translate(flag.labelKey, flag.label) }
    })
    createSeed: EquipmentService.createSeed
    loading: EquipmentService.loading
    errorMessage: EquipmentService.errorMessage
    inventoryUsable: EquipmentService.ready
    actionsEnabled: EquipmentService.ready && !EquipmentService.loading && !EquipmentService.busy
    statusVisible: loading || errorMessage.length > 0 || EquipmentService.operationError.length > 0
    statusText: errorMessage || (loading
        ? TranslationManager.translate("common.loading", "Loading…")
        : EquipmentService.operationError
            ? TranslationManager.translate("equipment.dialog.saveFailed",
                                           "Couldn't save this equipment — please try again.")
            : "")

    function refresh() { EquipmentService.requestInventory() }
    function grinderBrandSuggestions() {
        const combined = (options.grinderBrands || []).concat(options.grinderHistoryBrands || [])
        return combined.filter((value, index) => value && combined.indexOf(value) === index)
    }
    function grinderModelSuggestions(brand) { return (options.grinderModels || {})[brand] || [] }
    function burrsEntry(brand, model) {
        return ((options.grinderBurrs || {})[brand] || {})[model] || ({})
    }
    function burrsSuggestions(brand, model) { return burrsEntry(brand, model).suggestions || [] }
    function burrsSwappable(brand, model) { return burrsEntry(brand, model).swappable === true }
    function basketBrandSuggestions() { return options.basketBrands || [] }
    function basketModelSuggestions(brand) { return (options.basketModels || {})[brand] || [] }
    function basketModelDescriptions(brand) { return (options.basketSummaries || {})[brand] || ({}) }
    function serviceRequestId(requestId) {
        return serviceRequestPrefix + String(requestId || "")
    }
    function createPackage(fields, applyToActiveBag, requestId) {
        EquipmentService.createPackage(fields, applyToActiveBag, serviceRequestId(requestId))
    }
    function updatePackage(packageId, fields, expectedEditable, requestId) {
        EquipmentService.updatePackage(packageId, fields, expectedEditable, serviceRequestId(requestId))
    }
    function activatePackage(packageId, applyToActiveBag, requestId) {
        EquipmentService.activatePackage(packageId, applyToActiveBag, serviceRequestId(requestId))
    }

    property Connections serviceConnections: Connections {
        target: EquipmentService
        function onInventoryReady(packages) { root.inventoryChanged() }
        function onOperationCompleted(requestId, success, resultId, reason) {
            const prefix = root.serviceRequestPrefix
            if (!requestId || !requestId.startsWith(prefix))
                return
            root.operationCompleted(requestId.slice(prefix.length), success, resultId, reason)
        }
    }
}
