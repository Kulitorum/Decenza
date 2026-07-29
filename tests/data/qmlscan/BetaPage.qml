// Second fixture: proves the worker loops over more than one file rather than stopping at the
// first. Same rules as AlphaPage.qml — text to be parsed, never loaded.
import QtQuick

Item {
    Text { text: TranslationManager.translate("fixture.beta.title", "Beta") }
}
