import QtQuick
import Decenza

// Translatable text component
// Usage: Tr { key: "settings.title"; fallback: "Settings" }
Text {
    id: control

    // Required properties
    required property string key
    required property string fallback

    // Computed text - translationVersion dependency forces re-evaluation when translations change
    text: {
        var _ = TranslationManager.translationVersion
        return TranslationManager.translate(key, fallback)
    }

    // Default styling — use individual properties so instances can override font.bold, font.pixelSize, etc.
    font.family: Theme.bodyFont.family
    font.pixelSize: Theme.bodyFont.pixelSize
    color: Theme.textColor

    // Register this string with TranslationManager on creation
    Component.onCompleted: {
        TranslationManager.registerString(key, fallback)
    }
}
