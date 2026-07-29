// Fixture for tst_translationscan. Never compiled or instantiated — the scanner reads QML as
// TEXT, so this file exists only to be parsed. Keep it small; scale is not coverage.
import QtQuick

Item {
    Text { text: TranslationManager.translate("fixture.alpha.title", "Alpha") }

    ActionButton {
        translationKey: "fixture.alpha.save"
        translationFallback: "Save"
    }

    Tr { key: "fixture.alpha.idle"; fallback: "Idle" }

    // One key, two fallbacks, in one file: pattern 1 sees "First", pattern 3 sees "Last". Scan
    // order is pattern 1 then 3, so "Last" must win — independent of the order the worker
    // happens to walk the files in.
    Text { text: TranslationManager.translate("fixture.shared", "First") }
    Tr { key: "fixture.shared"; fallback: "Last" }
}
