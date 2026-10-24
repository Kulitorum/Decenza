import QtQuick
import Decenza

// Native adapter for shared controls' tap-to-announce policy.
SharedButtonInteraction {
    accessibilityEnabled: AccessibilityManager.enabled
    lastAnnouncedItem: AccessibilityManager.lastAnnouncedItem
    onAnnouncementRequested: function(text) { AccessibilityManager.announce(text) }
    onLastAnnouncedItemChangeRequested: function(item) {
        AccessibilityManager.lastAnnouncedItem = item
    }
}
