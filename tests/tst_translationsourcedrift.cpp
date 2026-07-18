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
// The two translation sources behave differently on purpose, and these tests pin that:
// a DOWNLOADED translation is dropped when its source text is rewritten, while a USER'S OWN
// override survives, matching setTranslation()'s existing contract that overrides outlive updates.
//
// KNOWN LIMIT, deliberately asserted below so nobody mistakes it for a bug: the drop is one-shot.
// A downloaded translation carries no record of the English it was made from, so once the
// registry has moved on, re-downloading the same stale text is indistinguishable from a fresh
// one and it comes back. Closing that needs provenance in the published translation format,
// which is a server-side change. See tasks.md 7.8a.

#include <QtTest>
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

    // A DOWNLOADED translation of the old sentence must stop being served.
    void rewrittenSourceDropsADownloadedTranslation()
    {
        const QString key = QStringLiteral("test.drift.delete");
        writeDownloadedLanguageFile(QStringLiteral("de"), {{key, QStringLiteral("Löschen")}});

        Settings settings;
        TranslationManager tm(&m_nam, &settings);
        tm.setCurrentLanguage(QStringLiteral("de"));

        tm.registerString(key, QStringLiteral("Delete"));
        QCOMPARE(tm.translateString(key, QStringLiteral("Delete")), QStringLiteral("Löschen"));

        // The button now archives rather than deletes. "Löschen" is not stale, it is wrong.
        QCOMPARE(tm.translateString(key, QStringLiteral("Archive")), QStringLiteral("Archive"));
        QVERIFY2(!tm.hasTranslation(key),
                 "a downloaded translation of the previous English must not survive the rewrite");
    }

    // ...but a translation the user wrote themselves is kept. setTranslation() marks those as
    // overrides and already promises they survive updates; discarding someone's own words is
    // worse than showing them against changed source, which the String Browser makes visible.
    void aUserOverrideSurvivesARewrite()
    {
        Settings settings;
        TranslationManager tm(&m_nam, &settings);
        tm.setCurrentLanguage(QStringLiteral("de"));
        const QString key = QStringLiteral("test.drift.override");

        tm.registerString(key, QStringLiteral("Delete"));
        tm.setTranslation(key, QStringLiteral("Entfernen"));   // marks a user override

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

        const QString key = QStringLiteral("test.drift.inherits");
        tm.translateString(key, QStringLiteral("Delete"));

        // Reworded to match the donor's English.
        QCOMPARE(tm.translateString(key, QStringLiteral("Archive")), QStringLiteral("Archivieren"));
    }

    // The known limit, asserted rather than described, so that if provenance is ever added to
    // the published format this test fails and points at the paragraph that explains why.
    void aRedownloadReintroducesTheStaleTranslation()
    {
        const QString key = QStringLiteral("test.drift.redownload");
        writeDownloadedLanguageFile(QStringLiteral("de"), {{key, QStringLiteral("Löschen")}});

        {
            Settings settings;
            TranslationManager tm(&m_nam, &settings);
            tm.setCurrentLanguage(QStringLiteral("de"));
            tm.registerString(key, QStringLiteral("Delete"));
            QCOMPARE(tm.translateString(key, QStringLiteral("Archive")), QStringLiteral("Archive"));
        }

        // The server still publishes the translation of the old English; nothing in the file
        // says which English that was, so the client cannot tell it from a fresh translation.
        writeDownloadedLanguageFile(QStringLiteral("de"), {{key, QStringLiteral("Löschen")}});

        Settings settings;
        TranslationManager tm(&m_nam, &settings);
        tm.setCurrentLanguage(QStringLiteral("de"));
        QCOMPARE(tm.translateString(key, QStringLiteral("Archive")), QStringLiteral("Löschen"));
    }
};

QTEST_MAIN(TestTranslationSourceDrift)
#include "tst_translationsourcedrift.moc"
