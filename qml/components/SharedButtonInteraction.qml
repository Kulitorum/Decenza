import QtQuick

// Typed boundary for the host's tap-to-announce accessibility policy. The
// shared controls own gesture handling; a host bridges these requests to its
// accessibility service and keeps lastAnnouncedItem in sync across controls.
QtObject {
    property bool accessibilityEnabled: false
    property QtObject lastAnnouncedItem: null

    signal announcementRequested(string text)
    signal lastAnnouncedItemChangeRequested(QtObject item)

    function announce(text: string): void {
        announcementRequested(text)
    }

    function remember(item: QtObject): void {
        lastAnnouncedItemChangeRequested(item)
    }

    function clearIf(item: QtObject): void {
        if (lastAnnouncedItem === item)
            lastAnnouncedItemChangeRequested(null)
    }
}
