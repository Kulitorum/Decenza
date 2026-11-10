import QtQuick
import Decenza

// Surfaces request-correlated host translation refusals to the user.
//
// This exists because of a live test that went exactly wrong enough to be useful. With a corrupt
// language file the app correctly refused to save an edit — protecting 4021 strings that the
// previous code would have destroyed — and then said nothing at all. The tester edited a string,
// watched it not save, and had to ask whether the feature was broken. A guard that is
// indistinguishable from a dead button is only half a fix.
//
// The messages already exist and are already translated: every refusal path sets m_lastError with
// a tr() string explaining what was not done and why. They were simply never read. `lastError` had
// no QML consumer anywhere in the app, so all of it was written and thrown away.
//
// Self-subscribing on purpose. Wiring each call site individually is how the gap appeared in the
// first place — a new refusal path would silently not surface. Mount this once per page that can
// trigger one and every present and future refusal is covered.
//
// A StatusToast so it looks and sits like every other toast; every message here is a refusal.
StatusToast {
    id: root

    error: true
    durationMs: 6000

    property string lastShownError: ""

    Connections {
        target: LanguageService
        enabled: root.enabled
        function onStateChanged() {
            const code = LanguageService.errorMessage
            const messages = {
                invalidRequest: TranslationManager.translate("language.error.invalidRequest", "This language request was invalid."),
                invalidLanguage: TranslationManager.translate("language.error.invalidLanguage", "This language is unavailable or cannot be changed."),
                invalidLanguageOrBusy: TranslationManager.translate("language.error.invalidLanguageOrBusy", "This language cannot be downloaded right now."),
                invalidArguments: TranslationManager.translate("language.error.invalidRequest", "This language request was invalid."),
                invalidRange: TranslationManager.translate("language.error.invalidRequest", "This language request was invalid."),
                unsupportedOperation: TranslationManager.translate("language.error.unsupported", "This language action is unavailable."),
                unavailable: TranslationManager.translate("language.error.unavailable", "The host language service is unavailable."),
                disconnected: TranslationManager.translate("language.error.unavailable", "The host language service is unavailable."),
                staleContext: TranslationManager.translate("language.error.staleContext", "The host language or translations changed. Reload before changing them."),
                writeFailed: TranslationManager.translate("language.error.writeFailed", "The language change could not be saved."),
                writeFailedOrUnavailable: TranslationManager.translate("language.error.writeFailed", "The language change could not be saved."),
                downloadFailed: TranslationManager.translate("language.error.download", "The community language download failed."),
                submissionFailed: TranslationManager.translate("language.error.submit", "The translation could not be submitted."),
                translationFailed: TranslationManager.translate("language.error.autoTranslate", "AI translation failed. Check the selected provider settings."),
                unavailableOrBusy: TranslationManager.translate("language.error.autoTranslateUnavailable", "AI translation is unavailable or already running."),
                notRunning: TranslationManager.translate("language.error.autoTranslateNotRunning", "AI translation is not running."),
                pageTooLarge: TranslationManager.translate("language.error.pageTooLarge", "This page of strings is too large to transfer."),
                busy: TranslationManager.translate("language.error.busy", "Another language operation is still running.")
            }
            const message = messages[code] || (code ? TranslationManager.translate("language.error.generic", "The language operation failed.") : "")
            if (!message || message.length === 0) {
                root.lastShownError = ""
                return
            }
            if (code === root.lastShownError)
                return
            root.lastShownError = code
            AccessibilityManager.announce(message, true)
            root.show(message)
        }
    }
}
