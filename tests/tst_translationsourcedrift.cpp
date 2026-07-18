// What happens when the English text behind a translation key is rewritten.
//
// Every registry write used to be guarded by `if (!m_stringRegistry.contains(key))`, so a key's
// English was captured the first time it was seen and never revisited — a full rescan could not
// refresh it either. Two consequences, both silent:
//
//   * The registry drifted into holding text the app no longer displays. It is what the AI
//     translator is prompted with and what a community upload publishes, so the drift spread
//     outward. `settings.ai.remoteMcp.setupGuidance` was the worked example: rewritten in QML to
//     drop its arrows, still stored with them, in the registry and in all of ar/de/fr.
//   * The translation kept rendering. A translation of a sentence that no longer exists is not
//     merely outdated — reword "Delete" to "Archive" and every other language keeps the old verb,
//     confidently, in a way that reads as intentional.
//
// The fix is deliberately limited to the registry: the translation is REPORTED as now rendering
// superseded text, and otherwise left alone. An earlier version dropped it, and running that
// against the real app disproved the assumption it rested on. A key does not have one English
// string here — 26 keys are used with two different fallbacks in the SAME build, so they
// oscillate rather than drift, and dropping destroyed their translations on every launch. It ate
// 11 German strings on the first real run before being reverted. See tasks.md 7.8a/7.8e.

#include <QtTest>
#include <QRegularExpression>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QStandardPaths>
#include "core/settings.h"
#include "core/translationmanager.h"

namespace {

// Write a language file the way a community download leaves one, so the translations it
// contains arrive through loadTranslations() and are NOT marked as user overrides. Seeding
// via setTranslation() instead would mark every entry an override and quietly test nothing.
void writeDownloadedLanguageFile(const QString& lang, const QMap<QString, QString>& entries)
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                        + QStringLiteral("/translations");
    QDir().mkpath(dir);

    QJsonObject translations;
    for (auto it = entries.constBegin(); it != entries.constEnd(); ++it)
        translations[it.key()] = it.value();

    QJsonObject root;
    root[QStringLiteral("language")] = lang;
    root[QStringLiteral("displayName")] = QStringLiteral("German");
    root[QStringLiteral("translations")] = translations;

    QFile f(dir + QStringLiteral("/") + lang + QStringLiteral(".json"));
    QVERIFY2(f.open(QIODevice::WriteOnly | QIODevice::Truncate), "could not seed language file");
    f.write(QJsonDocument(root).toJson());
}

} // namespace

class TestTranslationSourceDrift : public QObject
{
    Q_OBJECT

private:
    QNetworkAccessManager m_nam;

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
    }

    // Each test starts from an empty translations directory. Without this they share one
    // on-disk registry and leak state into each other — the first version of this file had
    // a later test reporting strings a previous one had registered.
    void init()
    {
        QTest::failOnWarning();

        const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                            + QStringLiteral("/translations");
        QDir(dir).removeRecursively();
    }

    // The baseline the old guard did get right. Kept so the drift fix cannot regress into
    // re-registering — and re-saving — on every single lookup.
    void unchangedSourceIsNotTreatedAsARewrite()
    {
        Settings settings;
        TranslationManager tm(&m_nam, &settings);
        const QString key = QStringLiteral("test.drift.stable");

        tm.registerString(key, QStringLiteral("Brew ratio"));
        tm.setTranslation(key, QStringLiteral("Brühverhältnis"));

        tm.registerString(key, QStringLiteral("Brew ratio"));
        tm.registerString(key, QStringLiteral("  Brew ratio  "));   // whitespace only

        QCOMPARE(tm.translateString(key, QStringLiteral("Brew ratio")),
                 QStringLiteral("Brühverhältnis"));
    }

    // The drift itself: the registry must follow the source text.
    void rewrittenSourceUpdatesTheRegistry()
    {
        Settings settings;
        TranslationManager tm(&m_nam, &settings);
        const QString key = QStringLiteral("test.drift.registry");

        tm.registerString(key, QStringLiteral("Go to Settings -> Connectors"));
        tm.registerString(key, QStringLiteral("Add this as a custom connector"));

        // getAllStrings() is what the String Browser and the AI translator read, so asserting
        // through it checks what those consumers actually receive.
        QString stored;
        const QVariantList all = tm.getAllStrings();
        for (const QVariant& v : all) {
            const QVariantMap row = v.toMap();
            if (row.value(QStringLiteral("key")).toString() == key)
                stored = row.value(QStringLiteral("fallback")).toString();
        }
        QCOMPARE(stored, QStringLiteral("Add this as a custom connector"));
    }

    // A translation is KEPT when its source text is rewritten — the registry moves, the
    // translation does not. Pinned because the opposite was implemented first and looked right
    // until it met a codebase where one key can carry two English strings.
    void rewrittenSourceKeepsTheTranslationAndUpdatesTheRegistry()
    {
        const QString key = QStringLiteral("test.drift.delete");
        writeDownloadedLanguageFile(QStringLiteral("de"), {{key, QStringLiteral("Löschen")}});

        Settings settings;
        TranslationManager tm(&m_nam, &settings);
        tm.setCurrentLanguage(QStringLiteral("de"));

        tm.registerString(key, QStringLiteral("Delete"));
        QCOMPARE(tm.translateString(key, QStringLiteral("Delete")), QStringLiteral("Löschen"));

        // Warns that the German now renders superseded English, and keeps serving it.
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("source text changed")));
        tm.registerString(key, QStringLiteral("Archive"));
        QVERIFY2(tm.hasTranslation(key), "the translation must survive a source rewrite");
    }

    // The oscillating case that killed the drop: one key, two English strings, same build.
    // Flipping between them must not degrade the translation no matter how often it happens.
    void aKeyUsedWithTwoFallbacksDoesNotLoseItsTranslation()
    {
        const QString key = QStringLiteral("beanbase.details.elevation");
        writeDownloadedLanguageFile(QStringLiteral("de"), {{key, QStringLiteral("Höhe")}});

        Settings settings;
        TranslationManager tm(&m_nam, &settings);
        tm.setCurrentLanguage(QStringLiteral("de"));

        // BeanBaseDetailsPopup says "Elevation"; ChangeBeansDialog says "Elevation:".
        for (int round = 0; round < 3; ++round) {
            QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("source text changed")));
            tm.registerString(key, QStringLiteral("Elevation"));
            QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("source text changed")));
            tm.registerString(key, QStringLiteral("Elevation:"));
        }
        QCOMPARE(tm.translateString(key, QStringLiteral("Elevation")), QStringLiteral("Höhe"));
    }

    // A user's own translation survives too. Trivially true now that nothing is dropped, but
    // kept as the guard that would fail first if dropping were ever reintroduced.
    void aUserOverrideSurvivesARewrite()
    {
        Settings settings;
        TranslationManager tm(&m_nam, &settings);
        tm.setCurrentLanguage(QStringLiteral("de"));
        const QString key = QStringLiteral("test.drift.override");

        tm.registerString(key, QStringLiteral("Delete"));
        tm.setTranslation(key, QStringLiteral("Entfernen"));   // marks a user override

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("source text changed")));
        QCOMPARE(tm.translateString(key, QStringLiteral("Archive")), QStringLiteral("Entfernen"));
    }

    // A rewrite landing on wording already translated under another key inherits that
    // translation rather than going blank — the propagate step inside translateString().
    void aRewriteMatchingAnotherKeyInheritsItsTranslation()
    {
        Settings settings;
        TranslationManager tm(&m_nam, &settings);
        tm.setCurrentLanguage(QStringLiteral("de"));

        tm.translateString(QStringLiteral("test.drift.donor"), QStringLiteral("Archive"));
        tm.setTranslation(QStringLiteral("test.drift.donor"), QStringLiteral("Archivieren"));

        // A brand-new key whose English already matches the donor's inherits its translation.
        const QString key = QStringLiteral("test.drift.inherits");
        QCOMPARE(tm.translateString(key, QStringLiteral("Archive")), QStringLiteral("Archivieren"));
    }

    // The scanner reads QML as text and sees escape sequences; the runtime sees the characters
    // they denote. Both write the registry, so if they decode differently each sees the other's
    // value as a rewrite and they oscillate — dropping the translation and rewriting the
    // registry on every scan and every render, forever. Six fallbacks in qml/ use \uXXXX
    // (GraphLegend's superscript two, a degree sign), and the old unescape handled only
    // \", \n and \t. Harmless while the !contains guard meant first-writer-wins; not harmless now.
    void scannerAndRuntimeDecodeLiteralsIdentically()
    {
        // What the scanner reads out of a .qml file, verbatim.
        QCOMPARE(TranslationManager::unescapeQmlLiteral(QStringLiteral("Resist(P/F\\u00B2)")),
                 QStringLiteral("Resist(P/F²)"));
        QCOMPARE(TranslationManager::unescapeQmlLiteral(QStringLiteral("Steam (\\u00B0C)")),
                 QStringLiteral("Steam (°C)"));
        QCOMPARE(TranslationManager::unescapeQmlLiteral(QStringLiteral("Shot excluded \\u2014 too short")),
                 QStringLiteral("Shot excluded — too short"));

        // The escapes that already worked must keep working.
        QCOMPARE(TranslationManager::unescapeQmlLiteral(QStringLiteral("a\\nb\\tc\\\"d\\\"")),
                 QStringLiteral("a\nb\tc\"d\""));

        // A backslash must not be eaten, and must not turn the next character into an escape.
        QCOMPARE(TranslationManager::unescapeQmlLiteral(QStringLiteral("C:\\\\next")),
                 QStringLiteral("C:\\next"));

        // Malformed \u is left exactly as written rather than silently dropped.
        QCOMPARE(TranslationManager::unescapeQmlLiteral(QStringLiteral("50\\uZZZZ")),
                 QStringLiteral("50\\uZZZZ"));
    }

    // The oscillation itself: a decoded literal registered by the scanner must not read as a
    // rewrite when the runtime supplies the same string as real characters.
    void aDecodedLiteralIsNotSeenAsARewrite()
    {
        Settings settings;
        TranslationManager tm(&m_nam, &settings);
        tm.setCurrentLanguage(QStringLiteral("de"));
        const QString key = QStringLiteral("test.drift.escapes");

        // Scanner path: registers what unescapeQmlLiteral() produced from the file.
        tm.registerString(key, TranslationManager::unescapeQmlLiteral(QStringLiteral("Steam (\\u00B0C)")));
        tm.setTranslation(key, QStringLiteral("Dampf (°C)"));

        // Runtime path: QML hands over the real characters.
        QCOMPARE(tm.translateString(key, QStringLiteral("Steam (°C)")), QStringLiteral("Dampf (°C)"));
    }

};

QTEST_MAIN(TestTranslationSourceDrift)
#include "tst_translationsourcedrift.moc"
