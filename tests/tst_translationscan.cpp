// The QML string scan: what it extracts, and that it never runs inline.
//
// The scan used to be synchronous and pumped QCoreApplication::processEvents() once per file
// "to keep the UI responsive". That nested event loop is what crashed the shipped 2.0.0 iOS
// build (issue #1692, SIGABRT): SettingsLanguageTab kicks the scan off from its
// Component.onCompleted, QML incubates that component synchronously while SettingsPage's
// TabBar.onCurrentIndexChanged handler is still on the stack, and a DeferredDelete already
// queued for the outgoing SettingsPage was delivered inside the nested loop — destroying the
// TabBar mid-handler, which Qt turns into a qFatal().
//
// So there are two things worth pinning here:
//   * scanAllStrings() must RETURN before it has done any of the work. A future "just make it
//     synchronous again, it's simpler" is the regression, and it is invisible in the UI.
//   * the parsing itself, now that it has been lifted out into a static function, must still
//     recognise all three patterns — including the two malformed-input cases that earlier bugs
//     turned into garbage registry keys.

#include <QtTest>
#include <QSignalSpy>
#include <QDir>
#include <QNetworkAccessManager>
#include <QRegularExpression>
#include <QStandardPaths>
#include "core/settings.h"
#include "core/translationmanager.h"

namespace {

// The scanner's output as a lookup, for tests that don't care about ordering.
QMap<QString, QString> asMap(const QList<TranslationManager::ScannedString>& found)
{
    QMap<QString, QString> out;
    for (const TranslationManager::ScannedString& s : found)
        out[s.key] = s.fallback;
    return out;
}

} // namespace

class TestTranslationScan : public QObject
{
    Q_OBJECT

private:
    QNetworkAccessManager m_nam;

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
    }

    void init()
    {
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                            + QStringLiteral("/translations");
        QDir(dir).removeRecursively();

        Settings settings;
        settings.setValue(QStringLiteral("localization/language"), QStringLiteral("en"));

        // Armed last: Settings' own constructor may warn, and arming first would fail every
        // test in this file with a message pointing nowhere near the cause.
        QTest::failOnWarning();
    }

    // Pattern 1: translate("key", "fallback").
    void directCallsAreExtracted()
    {
        const QString qml = QStringLiteral(
            "Text { text: TranslationManager.translate(\"settings.title\", \"Settings\") }\n"
            "Text { text: TranslationManager.translate( \"units.grams\" , \"g\" ) }\n");

        const QMap<QString, QString> found = asMap(TranslationManager::parseTranslatableStrings(qml));

        QCOMPARE(found.value(QStringLiteral("settings.title")), QStringLiteral("Settings"));
        QCOMPARE(found.value(QStringLiteral("units.grams")), QStringLiteral("g"));
    }

    // Pattern 2: ActionButton's translationKey/translationFallback property pair, matched by
    // proximity — the fallback has to FOLLOW the key, and within 200 characters.
    void propertyPairsAreMatchedByProximity()
    {
        const QString qml = QStringLiteral(
            "ActionButton {\n"
            "    translationKey: \"common.button.ok\"\n"
            "    translationFallback: \"OK\"\n"
            "}\n");

        const QMap<QString, QString> found = asMap(TranslationManager::parseTranslatableStrings(qml));
        QCOMPARE(found.value(QStringLiteral("common.button.ok")), QStringLiteral("OK"));
    }

    // Pattern 3: the Tr component. Its fallback may sit on either side of the key.
    void trComponentPropertiesAreExtractedInEitherOrder()
    {
        const QString qml = QStringLiteral(
            "Tr { key: \"machineStatus.idle\"; fallback: \"Idle\" }\n"
            "Tr { fallback: \"Steaming\"; key: \"machineStatus.steaming\" }\n");

        const QMap<QString, QString> found = asMap(TranslationManager::parseTranslatableStrings(qml));

        QCOMPARE(found.value(QStringLiteral("machineStatus.idle")), QStringLiteral("Idle"));
        QCOMPARE(found.value(QStringLiteral("machineStatus.steaming")), QStringLiteral("Steaming"));
    }

    // A key must not be captured across a newline. Without the [^"\n] guard, `fallback: "How to
    // get an API key:"` matched at its own trailing `key:"` and swallowed 249 characters of QML
    // source as a translation key — which was then AI-translated into every language and
    // uploaded to the community server.
    void aFallbackEndingInKeyColonDoesNotBecomeAKey()
    {
        const QString qml = QStringLiteral(
            "Tr {\n"
            "    key: \"ai.apiKeyHelp\"\n"
            "    fallback: \"How to get an API key:\"\n"
            "}\n"
            "Text {\n"
            "    color: Theme.textColor\n"
            "    font: Theme.subtitleFont\n"
            "}\n");

        const QList<TranslationManager::ScannedString> found =
            TranslationManager::parseTranslatableStrings(qml);

        for (const TranslationManager::ScannedString& s : found) {
            QVERIFY2(!s.key.contains(QLatin1Char('\n')),
                     qPrintable(QStringLiteral("scanner captured QML source as a key: %1").arg(s.key)));
            QVERIFY2(!s.key.contains(QStringLiteral("Theme.")),
                     qPrintable(QStringLiteral("scanner captured QML source as a key: %1").arg(s.key)));
        }
    }

    // The scanner reads QML as TEXT while the runtime sees the characters the escapes denote.
    // Both write the registry, so they have to agree.
    void escapesAreDecodedTheWayTheRuntimeSeesThem()
    {
        const QString qml = QStringLiteral(
            "Text { text: TranslationManager.translate(\"chart.mlPerSec\", \"ml\\u00B7s\\u207B\\u00B9\") }\n");

        const QMap<QString, QString> found = asMap(TranslationManager::parseTranslatableStrings(qml));
        QCOMPARE(found.value(QStringLiteral("chart.mlPerSec")), QStringLiteral("ml·s⁻¹"));
    }

    // The contract that keeps #1692 fixed: the call returns with the work not yet done, and the
    // result arrives through the event loop.
    void scanAllStringsIsAsynchronous()
    {
        Settings settings;
        TranslationManager tm(&m_nam, &settings);

        // The QML module resource belongs to the app target, not to this test binary, so the
        // scan legitimately finds no files here. That is the one warning expected.
        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression(QStringLiteral("string scan found NO QML files")));

        QSignalSpy finished(&tm, &TranslationManager::scanFinished);
        tm.scanAllStrings();

        QCOMPARE(finished.count(), 0);   // must NOT have completed inline
        QVERIFY(tm.isScanning());

        QVERIFY(finished.wait(5000));
        QVERIFY(!tm.isScanning());
    }
};

QTEST_MAIN(TestTranslationScan)
#include "tst_translationscan.moc"
