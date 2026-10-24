import QtQml

// Typed service boundary for the shared equipment picker/editor. Host adapters
// supply registry suggestions and async persistence; the editor itself owns the
// draft, duplicate checks, and compare-and-set snapshot.
QtObject {
    property var packages: []
    property int activePackageId: -1
    property var puckPrepRows: []
    property var createSeed: ({})
    property bool loading: false
    property string errorMessage: ""
    // Page state follows a usable inventory; adapters may further restrict
    // actions while a remote session or mutation is pending.
    property bool inventoryUsable: false
    property bool actionsEnabled: inventoryUsable && !loading
    property bool emptyStateVisible: inventoryUsable && !loading && packages.length === 0
    property bool statusVisible: loading || errorMessage.length > 0
    property string statusText: errorMessage

    signal inventoryChanged()
    signal packagesInvalidated()
    signal operationCompleted(string requestId, bool success, var resultId, string reason)

    function refresh() {}
    function grinderBrandSuggestions() { return [] }
    function grinderModelSuggestions(brand) { return [] }
    function burrsSuggestions(brand, model) { return [] }
    function burrsSwappable(brand, model) { return false }
    function basketBrandSuggestions() { return [] }
    function basketModelSuggestions(brand) { return [] }
    function basketModelDescriptions(brand) { return ({}) }
    function createPackage(fields, applyToActiveBag, requestId) {}
    function updatePackage(packageId, fields, expectedEditable, requestId) {}
    function activatePackage(packageId, applyToActiveBag, requestId) {}
}
