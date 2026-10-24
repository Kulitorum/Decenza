pragma Singleton
import QtQuick

QtObject {
    property bool enabled: false
    property QtObject lastAnnouncedItem: null
    function announce(text: string): void {}
}
