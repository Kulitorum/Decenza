import QtQuick
import Decenza

// Translatable text component
// Usage: Tr { key: "settings.title"; fallback: "Settings" }
Text {
    id: control

    // Required properties
    required property string key
    required property string fallback

    // Reads translationVersion, then calls translate(). Both establish the same dependency on
    // translationsChanged, so the explicit read is now redundant — `translate` is a property
    // holding a callable, and reading it is what makes any binding re-evaluate on a language
    // change (see translationmanager.h).
    //
    // Kept anyway, and not as an oversight: for a long time this read was the ONLY thing
    // making Tr reactive while 3,248 bare translate() call sites silently froze. Leaving it
    // documents the dependency for a reader who wonders why this component worked when those
    // did not. It costs one property read per language change.
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
