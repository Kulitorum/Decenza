pragma Singleton
import QtQuick

QtObject {
    property bool enabled: false
    property QtObject lastAnnouncedItem: null
    property string lastAnnouncement: ""
    function announce(text: string): void { lastAnnouncement = text }
}
