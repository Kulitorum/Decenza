// The QML string scan: what it extracts, and that it never runs on the calling stack.
//
// The scan used to be synchronous and pumped QCoreApplication::processEvents() once per file "to
// keep the UI responsive". That nested pump is what crashed the shipped 2.0.0 iOS build (issue
// #1692, SIGABRT): SettingsLanguageTab kicks the scan off from its Component.onCompleted, QML
// incubates that component synchronously while SettingsPage's TabBar.onCurrentIndexChanged
// handler is still on the stack, and an event delivered inside the pump destroyed the outgoing
// SettingsPage — taking the TabBar with it, which Qt turns into a qFatal().
//
// Three things worth pinning here:
//   * scanAllStrings() must not pump the event queue. The sentinel test asserts that directly —
//     a queued event posted before the call has still not run when it returns — rather than
//     asserting the weaker "scanFinished arrived late", which a rewrite that keeps the parsing
//     inline and defers only the tail would satisfy while reintroducing the crash.
//   * m_scanCompleted and m_scanning must settle BEFORE scanFinished() is emitted, because the
//     parked bulk translator re-enters from inside that emit.
//   * the parsing itself, now lifted into a static function, must still recognise all three
//     patterns — including the two malformed-input cases that earlier bugs turned into garbage
//     registry keys, and the scan ordering that decides which fallback wins for a duplicate key.

#include <QtTest>
#include <QSignalSpy>
#include <QDir>
#include <QNetworkAccessManager>
#include <QRegularExpression>
#include <QStandardPaths>
#include "core/settings.h"
#include "core/translationmanager.h"

namespace {

// The scanner's output as a lookup, for tests that don't care about ordering. Anything asserting
// order or duplicates must use the QList directly — this helper discards exactly that.
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

    // Constructed in init(), BEFORE failOnWarning() is armed — Settings' own constructor may warn,
    // and a warning from it inside a test function would fail that test pointing nowhere near the
    // cause. Every test uses this one rather than building its own.
    std::unique_ptr<Settings> m_settings;

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

        m_settings = std::make_unique<Settings>();
        m_settings->setValue(QStringLiteral("localization/language"), QStringLiteral("en"));

        // Armed last, after everything whose construction may warn.
        QTest::failOnWarning();
    }

    void cleanup()
    {
        m_settings.reset();
    }

    // --- The parser -------------------------------------------------------------------------

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

    // Pattern 2: ActionButton's translationKey/translationFallback pair, matched by proximity.
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

    // The two proximity heuristics are NOT the same and must not be "unified for consistency":
    // pattern 2 looks forward only, pattern 3 looks both ways by absolute distance. Which English
    // string ends up attached to a key is what gets AI-translated and published, so the asymmetry
    // is behaviour, not style.
    void patternTwoLooksForwardOnlyAndPatternThreeLooksBothWays()
    {
        // Pattern 2, fallback BEFORE the key: not matched.
        const QString backwards = QStringLiteral(
            "ActionButton {\n"
            "    translationFallback: \"Cancel\"\n"
            "    translationKey: \"common.button.cancel\"\n"
            "}\n");
        QVERIFY(asMap(TranslationManager::parseTranslatableStrings(backwards))
                    .value(QStringLiteral("common.button.cancel")).isEmpty());

        // Pattern 3, fallback before the key: matched.
        const QString trBackwards = QStringLiteral("Tr { fallback: \"Steaming\"; key: \"m.steaming\" }\n");
        QCOMPARE(asMap(TranslationManager::parseTranslatableStrings(trBackwards))
                     .value(QStringLiteral("m.steaming")), QStringLiteral("Steaming"));

        // Pattern 2 beyond the 200-character window: not matched.
        QString distant = QStringLiteral("ActionButton { translationKey: \"far.key\"\n");
        distant += QStringLiteral("    // %1\n").arg(QString(240, QLatin1Char('x')));
        distant += QStringLiteral("    translationFallback: \"Far\"\n}\n");
        QVERIFY(asMap(TranslationManager::parseTranslatableStrings(distant))
                    .value(QStringLiteral("far.key")).isEmpty());
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

    // Results come back in SCAN order — pattern 1, then 2, then 3 — not in position order. The
    // registry applies them in that sequence and the last write wins, so 26 real keys that are
    // used with two different fallbacks in the same build depend on this not being "tidied up".
    void duplicateKeysComeBackInScanOrderNotPositionOrder()
    {
        const QString qml = QStringLiteral(
            "Tr { key: \"dup.key\"; fallback: \"FromTr\" }\n"
            "Text { text: TranslationManager.translate(\"dup.key\", \"FromTranslateCall\") }\n");

        const QList<TranslationManager::ScannedString> found =
            TranslationManager::parseTranslatableStrings(qml);

        QList<QString> fallbacks;
        for (const TranslationManager::ScannedString& s : found) {
            if (s.key == QStringLiteral("dup.key"))
                fallbacks << s.fallback;
        }

        // The Tr block comes FIRST in the text, but pattern 1 runs before pattern 3.
        QCOMPARE(fallbacks.size(), 2);
        QCOMPARE(fallbacks.at(0), QStringLiteral("FromTranslateCall"));
        QCOMPARE(fallbacks.at(1), QStringLiteral("FromTr"));
    }

    // A key must not be captured across a newline. Without the [^"\n] guard, `fallback: "How to
    // get an API key:"` matched at its own trailing `key:"` and swallowed 249 characters of QML
    // source as a translation key — which was then AI-translated into every language and uploaded
    // to the community server.
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

        // And the block is still scanned — a regex change that dropped it entirely would satisfy
        // the assertions above.
        QCOMPARE(asMap(found).value(QStringLiteral("ai.apiKeyHelp")),
                 QStringLiteral("How to get an API key:"));
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

    // --- The scan ---------------------------------------------------------------------------

    // The contract that keeps #1692 fixed, asserted against the event queue itself: a queued call
    // posted before scanAllStrings() must still be unrun when it returns. A scan that pumps —
    // however briefly, however well-intentioned — runs it, and fails here.
    void scanAllStringsDoesNotPumpTheEventQueue()
    {
        TranslationManager tm(&m_nam, m_settings.get());

        QObject sentinel;
        bool sentinelRan = false;
        QMetaObject::invokeMethod(&sentinel, [&sentinelRan]() { sentinelRan = true; },
                                  Qt::QueuedConnection);

        tm.scanAllStrings();

        QVERIFY2(!sentinelRan,
                 "scanAllStrings() delivered a queued event before returning — it is pumping the "
                 "event loop again, which is what aborted shipped iOS 2.0.0 (#1692)");
        QVERIFY(tm.isScanning());
    }

    // The scan reaches the registry, over the fixture QML, off the calling stack.
    void scanFindsFixtureStringsAndFinishesAsynchronously()
    {
        TranslationManager tm(&m_nam, m_settings.get());

        QSignalSpy finished(&tm, &TranslationManager::scanFinished);
        tm.scanAllStrings();

        QCOMPARE(finished.count(), 0);   // must NOT have completed inline
        QVERIFY(finished.wait(10000));
        QVERIFY(!tm.isScanning());

        // Assert the REGISTRY, not translateString(): the registry is what the scan exists to
        // fill and what AI translation and community upload are handed. translateString() with no
        // translation loaded just echoes the fallback the caller passed, so it would pass this
        // test whether or not the scan ever ran.
        QCOMPARE(tm.m_stringRegistry.value(QStringLiteral("fixture.alpha.title")),
                 QStringLiteral("Alpha"));
        QCOMPARE(tm.m_stringRegistry.value(QStringLiteral("fixture.alpha.save")),
                 QStringLiteral("Save"));
        QCOMPARE(tm.m_stringRegistry.value(QStringLiteral("fixture.alpha.idle")),
                 QStringLiteral("Idle"));
        // Second file: the worker loops rather than stopping at the first.
        QCOMPARE(tm.m_stringRegistry.value(QStringLiteral("fixture.beta.title")),
                 QStringLiteral("Beta"));
        // Last write wins, in scan order.
        QCOMPARE(tm.m_stringRegistry.value(QStringLiteral("fixture.shared")),
                 QStringLiteral("Last"));

        // Progress is posted from the worker; a worker that never reported would leave the
        // Language page's bar at zero for the whole scan and pass every other assertion here.
        QVERIFY(tm.scanTotal() > 0);
        QCOMPARE(tm.scanProgress(), tm.scanTotal());
    }

    // m_scanCompleted and m_scanning must be settled BEFORE scanFinished() is emitted: the parked
    // bulk translator re-enters translateAndUploadAllLanguages() from inside this emit and
    // re-tests m_scanCompleted. Emit first and it re-parks, rescans, and loops forever — with
    // nothing in the suite noticing, because the loop only shows up via bulk translate.
    void flagsAreSettledBeforeScanFinishedIsEmitted()
    {
        TranslationManager tm(&m_nam, m_settings.get());

        bool sawCompleted = false;
        bool sawStoppedScanning = false;
        connect(&tm, &TranslationManager::scanFinished, &tm, [&](int) {
            sawCompleted = tm.m_scanCompleted;
            sawStoppedScanning = !tm.m_scanning;
        });

        QSignalSpy finished(&tm, &TranslationManager::scanFinished);
        tm.scanAllStrings();
        QVERIFY(finished.wait(10000));

        QVERIFY2(sawCompleted, "m_scanCompleted was still false inside scanFinished() — a parked "
                               "bulk translate would re-park and rescan forever");
        QVERIFY2(sawStoppedScanning, "m_scanning was still true inside scanFinished()");
    }

    // Two calls in a row start one scan, not two, and emit one scanFinished. Both failure modes
    // are silent.
    void aSecondScanWhileOneIsRunningIsJoinedNotStarted()
    {
        TranslationManager tm(&m_nam, m_settings.get());

        QSignalSpy finished(&tm, &TranslationManager::scanFinished);
        tm.scanAllStrings();
        tm.scanAllStrings();   // joins the in-flight scan

        QVERIFY(finished.wait(10000));
        QTest::qWait(50);      // any second worker's result would land in this window
        QCOMPARE(finished.count(), 1);

        // And a scan started after the first completed does run again.
        tm.scanAllStrings();
        QVERIFY(tm.isScanning());
        QVERIFY(finished.wait(10000));
        QCOMPARE(finished.count(), 2);
    }

    // The destructor exists so a worker cannot post to a destroyed object. Under the Debug
    // build's automatic ASan this is a direct test of that.
    void destroyingMidScanIsSafe()
    {
        {
            TranslationManager tm(&m_nam, m_settings.get());
            tm.scanAllStrings();
            QVERIFY(tm.isScanning());
        }
        QTest::qWait(50);   // anything that escaped the destructor would land here
    }
};

QTEST_MAIN(TestTranslationScan)
#include "tst_translationscan.moc"
