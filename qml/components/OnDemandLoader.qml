import QtQuick
import Decenza

// Builds its component on first use: a dialog or overlay most opens of a page never show
// (#1976). ensure() builds it and returns it, or logs why not and returns null, so a caller
// writes `(x.ensure() as T)?.open()` and a failed build is a log line, not a silent tap.
Loader {
    id: root
    active: false

    function ensure(): QtObject {
        root.active = true
        if (root.status !== Loader.Ready || !root.item) {
            WebDebugLogger.warn("App", "OnDemandLoader", ["could not build", root.objectName || "component",
                root.sourceComponent ? root.sourceComponent.errorString() : "(no component)"].map(String).join(" "))
            return null
        }
        return root.item
    }
}
