import QtQuick
import Decenza

// The grind suggestion for one bean and profile on one equipment package, kept
// current (add-genius-grind-widget). The inputs are the triggers: a change to any of
// them, to the shot history or to the package's calibration asks again, so a caller
// only binds them. Used by the Genius Grind widget and popup and the recipe wizard.
QtObject {
    id: root

    // False while the host is hidden or has nothing to ask about.
    property bool active: true
    property double packageId: 0
    property string roaster: ""
    property string coffee: ""
    property string roastDate: ""
    property string profileTitle: ""

    // A new bean, profile or package clears this at once; a history or calibration
    // refresh keeps it until the new one arrives, with pending set.
    property var suggestion: ({})
    property bool pending: false
    property string _requestId: ""

    // The host reads the package's grinder live, so editing it in place asks again.
    readonly property var _grinder: EquipmentService.packages.find(function(pkg) {
        return Number(pkg.id) === root.packageId
    }) ?? ({})
    // JSON, not a join: a name may contain any delimiter.
    readonly property string _identityKey: JSON.stringify([EquipmentService.ready, root.packageId, root.roaster,
                                                           root.coffee, root.roastDate, root.profileTitle,
                                                           root._grinder.grinderBrand ?? "",
                                                           root._grinder.grinderModel ?? "",
                                                           root._grinder.rpmCapable ?? false])
    // historyRevision moves on a saved, rated, edited or deleted shot; catalogRevision
    // when a profile's editor type or shape (its chart row) changes. Compared by
    // value, so the replicas' other notifications pass.
    readonly property string _requestKey: root.active + "|" + root._identityKey + "|"
                                          + HistoryService.historyRevision + "|"
                                          + ProfileService.catalogRevision

    // A caller sets several inputs in a row; callLater with the same method runs it
    // once (qqmldelayedcallqueue.cpp:89-101). The new request is posted, so a reply
    // for the old inputs already queued would land first: drop its id here.
    // pending until request() runs, so the empty gap reads "working", not a reason.
    on_IdentityKeyChanged: {
        root.suggestion = ({})
        root._requestId = ""
        root.pending = true
    }
    on_RequestKeyChanged: Qt.callLater(root.request)
    Component.onCompleted: root.request()

    function request(): void {
        root._requestId = ""
        if (!root.active || !EquipmentService.ready || root.packageId <= 0
                || root.profileTitle.length === 0) {
            root.pending = false
            root.suggestion = ({})
            return
        }
        root.pending = true
        root._requestId = EquipmentService.requestGrindSuggestion(
            root.packageId, root.roaster, root.coffee, root.roastDate, root.profileTitle)
    }

    readonly property Connections _replies: Connections {
        target: EquipmentService
        function onGrindSuggestionReady(suggestion) {
            if (suggestion.requestId !== root._requestId) return
            root._requestId = ""
            root.pending = false
            root.suggestion = suggestion
        }
        function onGrindCalibrationChanged(packageId) {
            if (packageId === root.packageId) Qt.callLater(root.request)
        }
    }
}
