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
#include <QSignalSpy>
#include <QRegularExpression>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QStandardPaths>
#include "core/settings.h"
#include "core/settings_ai.h"   // ai() returns SettingsAI*, forward-declared in settings.h
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
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                            + QStringLiteral("/translations");
        QDir(dir).removeRecursively();

        // The QSettings store is process-scoped and shared by every test in this binary, so
        // wiping only the translations directory left half the state behind. The provider tests
        // persist an aiProvider and three API keys; they are order-independent today only
        // because each one writes every value it reads, which is a property of how they happen
        // to be written rather than of the fixture. Clear the AI settings too.
        Settings settings;
        settings.ai()->setAiProvider(QString());
        settings.ai()->setOpenaiApiKey(QString());
        settings.ai()->setAnthropicApiKey(QString());
        settings.ai()->setGeminiApiKey(QString());

        // Also reset the persisted language. Without this, construction loads whatever the
        // PREVIOUS test left behind, which made "the constructor loads de" true by declaration
        // order rather than by anything the test did — and made the same test behave differently
        // when run alone. Now construction always starts at "en" and a test that wants another
        // language asks for it explicitly.
        settings.setValue(QStringLiteral("localization/language"), QStringLiteral("en"));

        // Armed last: Settings' own constructor may warn, and arming first would fail every
        // test in this file with a message pointing nowhere near the cause.
        QTest::failOnWarning();
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

    // The Update button must MERGE, not replace.
    //
    // It used to write the downloaded file straight over the local one. The server's copy is
    // whatever someone last submitted, and nothing uploads automatically, so a machine that has
    // AI-translated its gaps normally holds far MORE than the server does. One click replaced
    // 3429 German strings with the server's 1515 and discarded the difference — twice, on a
    // real machine, the second time destroying an AI pass that had just been paid for.
    //
    // The automatic launch check always merged. That the manual button did the opposite, for
    // the same job, is what made this a trap rather than a preference.
    void aDownloadMergesAndDoesNotDiscardLocalTranslations()
    {
        const QString localOnly = QStringLiteral("test.merge.localOnly");
        const QString shared    = QStringLiteral("test.merge.shared");

        // Local state: two translated strings, one of which the server has never heard of.
        writeDownloadedLanguageFile(QStringLiteral("de"), {
            {localOnly, QStringLiteral("Nur lokal")},
            {shared,    QStringLiteral("Alt")},
        });

        Settings settings;
        TranslationManager tm(&m_nam, &settings);
        tm.setCurrentLanguage(QStringLiteral("de"));
        QCOMPARE(tm.translateString(localOnly, QStringLiteral("Local only")), QStringLiteral("Nur lokal"));

        // What a download carries: an update for the shared key, and nothing for the local one.
        QJsonObject incoming;
        incoming[shared] = QStringLiteral("Neu");
        QVERIFY2(tm.mergeLanguageUpdate(incoming), "a healthy merge must not refuse");

        QCOMPARE(tm.translateString(shared, QStringLiteral("Shared")), QStringLiteral("Neu"));
        QVERIFY2(tm.hasTranslation(localOnly),
                 "a translation the server does not carry must survive a download");
        QCOMPARE(tm.translateString(localOnly, QStringLiteral("Local only")), QStringLiteral("Nur lokal"));
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

    // ---- The spend rule ----------------------------------------------------------------
    //
    // "A configured API key is not permission to spend on it." This is the one function in
    // TranslationManager with money attached, and until this test it was enforced by nothing:
    // re-adding a single `if (!openaiApiKey().isEmpty()) providers << "openai";` left the whole
    // suite green while billing a user on an account they never selected.
    //
    // That is not hypothetical. The old code hard-ordered "Claude first, then OpenAI", which
    // hid a retired Anthropic model for months — every Anthropic request 404'd and the batch
    // quietly finished on OpenAI.
    void onlyTheSelectedProviderIsEverUsed()
    {
        Settings settings;
        TranslationManager tm(&m_nam, &settings);

        // Every provider holds a key. A greedy implementation returns all four here.
        settings.ai()->setOpenaiApiKey(QStringLiteral("sk-openai"));
        settings.ai()->setAnthropicApiKey(QStringLiteral("sk-anthropic"));
        settings.ai()->setGeminiApiKey(QStringLiteral("sk-gemini"));

        settings.ai()->setAiProvider(QStringLiteral("anthropic"));
        QCOMPARE(tm.getConfiguredProviders(), QStringList{QStringLiteral("anthropic")});

        settings.ai()->setAiProvider(QStringLiteral("openai"));
        QCOMPARE(tm.getConfiguredProviders(), QStringList{QStringLiteral("openai")});

        settings.ai()->setAiProvider(QStringLiteral("gemini"));
        QCOMPARE(tm.getConfiguredProviders(), QStringList{QStringLiteral("gemini")});
    }

    // The half that actually bills someone: the selected provider has NO key, but another does.
    // Falling back here is the precise failure the rule exists to prevent, so an empty list —
    // "cannot run, say so" — is the required answer, not a convenience substitution.
    void anUnconfiguredSelectionDoesNotBorrowAnotherProvidersKey()
    {
        Settings settings;
        TranslationManager tm(&m_nam, &settings);

        settings.ai()->setOpenaiApiKey(QStringLiteral("sk-openai"));
        settings.ai()->setAnthropicApiKey(QString());
        settings.ai()->setAiProvider(QStringLiteral("anthropic"));

        QVERIFY2(tm.getConfiguredProviders().isEmpty(),
                 "selected provider has no key: the run must stop, not fall back to OpenAI");
    }

    // ---- The download must never replace a local file it could not read --------------------
    //
    // NOTE, since this paragraph used to say the opposite and the claim was wrong: it asserted
    // that mergeLanguageUpdate "always merged correctly" and only the file branch needed
    // guarding. Both halves needed it. mergeLanguageUpdate merged into a map that was empty by
    // FAILURE whenever the local file would not load, and that is the branch the UI actually
    // reaches. See aFailedLoadBlocksTheInMemoryMergeToo below.
    //
    // This test still earns its place: it is the only coverage of QSaveFile atomicity and of
    // the not-current-language branch, which is reachable by switching language mid-download.
    //
    // The specific hazard is that "no local file" and "local file I cannot read" both used to
    // yield an empty base, so a locked or corrupt file was silently overwritten by the server's
    // copy — the original destructive replace, reached from inside the branch written to stop it.
    void acorruptLocalFileIsNotOverwrittenByTheServerCopy()
    {
        Settings settings;
        TranslationManager tm(&m_nam, &settings);

        const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                            + QStringLiteral("/translations");
        QDir().mkpath(dir);
        const QString path = dir + QStringLiteral("/zz.json");

        // A local file that exists but is not valid JSON.
        {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("{ this is not json");
        }
        const auto readBack = [&]() -> QByteArray {
            QFile f(path);
            return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray("<unreadable>");
        };
        const QByteArray before = readBack();

        // The abort path warns by design, so failOnWarning() must not fire the test.
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("Language merge ABORTED")));

        QJsonObject incoming{{QStringLiteral("translations"),
                              QJsonObject{{QStringLiteral("a.key"), QStringLiteral("server value")}}}};
        QVERIFY2(!tm.mergeDownloadedLanguageFile(QStringLiteral("zz"), incoming),
                 "an unreadable local file must make the merge fail, not succeed silently");

        const QByteArray after = readBack();
        QCOMPARE(after, before);   // untouched, not replaced
    }

    // The same protection, on the branch the UI ACTUALLY reaches.
    //
    // This exists because the first attempt at this fix was theatre. It guarded
    // mergeDownloadedLanguageFile and tested it — but both Update buttons set currentLanguage
    // to the language they are about to download, so `langCode == m_currentLanguage` always
    // holds and that helper never runs. The test passed against code no user could reach while
    // the destructive replace shipped on the path they took.
    //
    // The route: loadTranslations() on a corrupt file left m_translations empty, which is
    // indistinguishable from "nothing translated yet"; mergeLanguageUpdate() then folded the
    // server's copy into that empty map and saved. The user sees 0% translated, presses Update
    // *because* it says 0%, and the click destroys what the corrupt file still held.
    void aFailedLoadBlocksTheInMemoryMergeToo()
    {
        Settings settings;

        const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                            + QStringLiteral("/translations");
        QDir().mkpath(dir);
        const QString path = dir + QStringLiteral("/de.json");
        {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("{ truncated by a crash");
        }
        const QByteArray before = [&]() -> QByteArray {
            QFile f(path);
            return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray("<unreadable>");
        }();

        // setCurrentLanguage("de") is what runs loadTranslations() and sets the failure flag.
        // (Not the constructor: it reads localization/language, which defaults to "en" — this
        // comment used to credit the constructor, which was true only because earlier tests in
        // this binary had persisted "de" into the shared settings store.)
        TranslationManager tm(&m_nam, &settings);
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("Invalid translation file")));
        tm.setCurrentLanguage(QStringLiteral("de"));

        // The retry-then-refuse pair, both by design.
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("previously failed to load")));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("Invalid translation file")));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("Refusing to merge")));
        QVERIFY2(!tm.mergeLanguageUpdate(
                     QJsonObject{{QStringLiteral("a.key"), QStringLiteral("server value")}}),
                 "a failed load must make the merge refuse and say so via its return value");

        const QByteArray after = [&]() -> QByteArray {
            QFile f(path);
            return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray("<unreadable>");
        }();
        QCOMPARE(after, before);   // the corrupt file is left alone, not replaced
        // What the USER ends up seeing is asserted by arefusedUpdateReportsFailureAndNothingElse,
        // at the caller frame — the level where this kept going wrong.
    }

    // Assert at the CALLER frame, because that is where every miss in this area has happened.
    //
    // Twice now a guard was added, tested, negative-controlled, and declared verified while the
    // frame above it undid the guard. First the helper was unreachable; then the refusal was
    // honoured by the helper but the caller emitted success straight afterwards, so the user got
    // languageDownloaded(false) followed by languageDownloaded(true) and the UI acted on the
    // second — reporting success, then offering to AI-translate, which would have destroyed the
    // very file the refusal protected.
    //
    // So this drives applyFetchedLanguageForTest (the body of the network slot) and asserts on
    // what the UI would actually receive: exactly one signal, and it says false.
    void arefusedUpdateReportsFailureAndNothingElse()
    {
        Settings settings;

        const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                            + QStringLiteral("/translations");
        QDir().mkpath(dir);
        const QString path = dir + QStringLiteral("/de.json");
        {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("{ truncated");
        }

        TranslationManager tm(&m_nam, &settings);
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("Invalid translation file")));
        tm.setCurrentLanguage(QStringLiteral("de"));   // this is what runs loadTranslations()

        QSignalSpy spy(&tm, &TranslationManager::languageDownloaded);
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("previously failed to load")));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("Invalid translation file")));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("Refusing to merge")));

        QJsonObject root{
            {QStringLiteral("translations"),
             QJsonObject{{QStringLiteral("a.key"), QStringLiteral("server value")}}}};
        tm.applyFetchedLanguage(QStringLiteral("de"), root);

        QCOMPARE(spy.count(), 1);                        // not two — no success chaser
        QCOMPARE(spy.at(0).at(1).toBool(), false);
        QVERIFY(!spy.at(0).at(2).toString().isEmpty());
    }

    // The third door. A corrupt file shows the language at 0%, and the things a user does at 0%
    // are edit a string and run AI Translate — neither of which goes near the download path that
    // was guarded first. Both land in saveTranslations(), which used to truncate and write the
    // empty-by-failure map straight over the file.
    void aFailedLoadAlsoBlocksSavingAndUploading()
    {
        Settings settings;

        const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                            + QStringLiteral("/translations");
        QDir().mkpath(dir);
        const QString path = dir + QStringLiteral("/de.json");
        {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("{ truncated");
        }
        const QByteArray before = [&]() -> QByteArray {
            QFile f(path);
            return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray("<unreadable>");
        }();

        TranslationManager tm(&m_nam, &settings);
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("Invalid translation file")));
        tm.setCurrentLanguage(QStringLiteral("de"));   // this is what runs loadTranslations()

        // Editing a string is the likeliest response to seeing 0%.
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("Refusing to save")));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("was NOT saved and has been rolled back")));
        tm.setTranslation(QStringLiteral("some.key"), QStringLiteral("edited"));

        // The edit must be rolled back, not merely unsaved. Leaving it in the in-memory map is
        // how the user ends up looking at an edit that no longer exists after a restart — the
        // live run of this exact scenario showed the String Browser reporting the edit applied
        // while the refusal had already blocked the write. And the override must not survive
        // either: recording a key as user-customised while its text was never written leaves
        // the override protecting a string that does not exist.
        QVERIFY2(!tm.hasTranslation(QStringLiteral("some.key")),
                 "a refused save must roll the edit back, not display it as applied");
        QVERIFY2(!tm.m_userOverrides.contains(QStringLiteral("some.key")),
                 "a refused save must not leave the key marked as a user override");

        const QByteArray after = [&]() -> QByteArray {
            QFile f(path);
            return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray("<unreadable>");
        }();
        QCOMPARE(after, before);   // the corrupt file still holds whatever it held

        // And the upload must not publish the empty map — that would overwrite the community
        // copy for every user of this language.
        QSignalSpy spy(&tm, &TranslationManager::translationSubmitted);
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("Refusing to upload")));
        tm.submitTranslation();
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toBool(), false);
    }

    // A provider that answers 200 with something unusable must FAIL the batch, not report zero
    // translations.
    //
    // The difference is not cosmetic and it is not only cosmetic inside the bulk run: a
    // "success" there is what calls submitTranslation(), so a model replying with prose, or a
    // reply truncated by max_tokens, produced a paid run that reported complete and then
    // published an untranslated file over the community copy for that language.
    //
    // Driven through the parse step directly because the alternative is a live provider. The
    // verdict this returns is the whole contract — the caller counts it, and the run's result
    // is derived from that count.
    void anUnusableProviderReplyIsAFailureNotZeroTranslations()
    {
        // NB: ordinary escaped string literals, not R"(...)". moc's parser silently produces an
        // EMPTY .moc for this file when it meets a raw string, and the only symptom is a
        // "missing vtable" link error that points at the class rather than at the literal.
        Settings settings;
        settings.ai()->setAiProvider(QStringLiteral("openai"));
        settings.ai()->setOpenaiApiKey(QStringLiteral("sk-test"));
        TranslationManager tm(&m_nam, &settings);

        // A real OpenAI reply whose content is prose rather than the requested JSON.
        const QByteArray prose =
            "{\"choices\":[{\"message\":{\"content\":\"I cannot help with that.\"}}]}";
        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression(QStringLiteral("Failed to parse AI translation response")));
        QVERIFY2(!tm.parseAutoTranslateResponse(prose),
                 "prose instead of JSON must be reported as a failed batch");

        // 200 with no usable content at all — how several providers signal quota and policy
        // conditions.
        const QByteArray empty = "{\"choices\":[]}";
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("Empty AI response")));
        QVERIFY2(!tm.parseAutoTranslateResponse(empty),
                 "an empty completion must be reported as a failed batch");

        // Truncated JSON — what hitting max_tokens looks like: valid-looking text with an
        // unbalanced brace. The brace-slicing in the parser makes this the subtlest case.
        const QByteArray truncated =
            "{\"choices\":[{\"message\":{\"content\":\"{\\\"Hello\\\": \\\"Hallo\\\", \\\"Wor\"}}]}";
        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression(QStringLiteral("Failed to parse AI translation response")));
        QVERIFY2(!tm.parseAutoTranslateResponse(truncated),
                 "a truncated JSON body must be reported as a failed batch");

        // Control: a well-formed reply is still accepted, so the guard has not simply been made
        // to fail everything.
        const QByteArray good =
            "{\"choices\":[{\"message\":{\"content\":\"{\\\"Hello\\\": \\\"Hallo\\\"}\"}}]}";
        QVERIFY2(tm.parseAutoTranslateResponse(good),
                 "a well-formed reply must still succeed");
    }

    // The consequence, not the helper. Every previous round tested one frame below the bug and
    // shipped it green, and the round that introduced this very code did it again: it asserted
    // parseAutoTranslateResponse's return value while the caller three statements away emitted
    // success regardless.
    //
    // What matters is that a run which could not PERSIST what it translated reports failure. A
    // corrupt local file shows the language at 0%, the user presses AI Translate because of
    // that 0%, pays for the whole language, watches the strings appear — and saveTranslations()
    // refuses. Before this, the run still said "Translated N strings" and the work was gone at
    // restart.
    void arunThatCannotSaveReportsFailureNotSuccess()
    {
        Settings settings;

        const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                            + QStringLiteral("/translations");
        QDir().mkpath(dir);
        const QString path = dir + QStringLiteral("/de.json");
        {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("{ truncated");
        }

        TranslationManager tm(&m_nam, &settings);
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("Invalid translation file")));
        tm.setCurrentLanguage(QStringLiteral("de"));

        // saveTranslations() must refuse AND say so, rather than refusing silently and letting
        // a caller report success on top of it.
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("Refusing to save")));
        QVERIFY2(!tm.saveTranslations(),
                 "a save that refuses must report it; a void refusal is what let the AI run "
                 "claim success for work it did not persist");
    }

    // A manual edit clears the stale AI value — and only when the edit was actually kept.
    //
    // This lived in StringBrowserPage as two lines AFTER the setGroupTranslation call, where it
    // could never run: that call emits translationsChanged() synchronously, the page's handler
    // rebuilds the model, and the ListView delegate executing the function is destroyed mid-call.
    // The live app logged "TranslationManager is not defined" at exactly that line, so every
    // manual edit in a non-English language left the old AI translation on screen beside it.
    //
    // The second half is a bug the QML version had independently: it cleared the AI translation
    // whether or not the save succeeded. In C++ the clear sits after the refusal check, so a
    // refused edit now leaves the AI value alone rather than destroying it for nothing.
    void aManualEditClearsTheAiValueOnlyIfItWasSaved()
    {
        Settings settings;
        TranslationManager tm(&m_nam, &settings);
        tm.setCurrentLanguage(QStringLiteral("de"));

        const QString fallback = QStringLiteral("Brew ratio");
        tm.registerString(QStringLiteral("edit.ratio"), fallback);
        tm.m_aiTranslations[fallback] = QStringLiteral("KI-Verhaeltnis");
        QVERIFY(!tm.getAiTranslation(fallback).isEmpty());

        // Healthy file: the edit saves, so the stale AI value must go.
        tm.setGroupTranslation(fallback, QStringLiteral("Brueh-Verhaeltnis"));
        QVERIFY2(tm.getAiTranslation(fallback).isEmpty(),
                 "a saved manual edit must clear the AI translation it replaces");

        // Now the refusal path: the AI value must survive, because the edit did not.
        tm.m_aiTranslations[fallback] = QStringLiteral("KI-Verhaeltnis");
        tm.m_translationsLoadFailed = true;
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("Refusing to save")));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("was NOT saved and has been rolled back")));
        tm.setGroupTranslation(fallback, QStringLiteral("Etwas anderes"));
        QVERIFY2(!tm.getAiTranslation(fallback).isEmpty(),
                 "a refused edit must not destroy the AI translation it failed to replace");
    }

    // Import must MERGE, not replace — the last unguarded copy of the download bug.
    //
    // Same file, same destruction, one button along: export a backup at 1500 strings,
    // AI-translate up to 3429, then import the backup to restore a few hand-corrections, and
    // the other 1929 used to vanish with no prompt, no diff and no undo.
    void animportMergesRatherThanReplacing()
    {
        Settings settings;
        TranslationManager tm(&m_nam, &settings);

        const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                            + QStringLiteral("/translations");
        QDir().mkpath(dir);

        // A rich local file: two strings the import will not carry.
        writeDownloadedLanguageFile(QStringLiteral("zz"), {
            {QStringLiteral("keep.one"), QStringLiteral("Behalten eins")},
            {QStringLiteral("keep.two"), QStringLiteral("Behalten zwei")},
        });

        // A poorer import file carrying one overlapping key and one new one.
        const QString importPath = dir + QStringLiteral("/incoming.json");
        {
            QJsonObject root{
                {QStringLiteral("language"), QStringLiteral("zz")},
                {QStringLiteral("translations"), QJsonObject{
                    {QStringLiteral("keep.one"), QStringLiteral("Ersetzt")},
                    {QStringLiteral("brand.new"), QStringLiteral("Neu")}}}};
            QFile f(importPath);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(QJsonDocument(root).toJson());
        }

        tm.importTranslation(importPath);

        QFile result(dir + QStringLiteral("/zz.json"));
        QVERIFY(result.open(QIODevice::ReadOnly));
        const QJsonObject after = QJsonDocument::fromJson(result.readAll())
                                      .object().value(QStringLiteral("translations")).toObject();

        QCOMPARE(after.value(QStringLiteral("keep.one")).toString(), QStringLiteral("Ersetzt"));
        QCOMPARE(after.value(QStringLiteral("brand.new")).toString(), QStringLiteral("Neu"));
        QVERIFY2(after.contains(QStringLiteral("keep.two")),
                 "a string the import did not carry must survive it — import merges, never replaces");
    }

    // The payload and the language it was built for must travel together.
    //
    // The 429 retry re-derived the language from m_currentLanguage at fire time while
    // m_pendingUploadData still held the original payload, so uploading French, hitting the
    // rate limit, and switching to German published the French strings AS the German community
    // copy — then reported it as a successful German contribution. The blast radius is every
    // user of that language, not one local file.
    void auploadPayloadStaysPairedWithItsLanguage()
    {
        Settings settings;
        TranslationManager tm(&m_nam, &settings);
        tm.setCurrentLanguage(QStringLiteral("fr"));
        tm.setTranslation(QStringLiteral("a.key"), QStringLiteral("Bonjour"));

        tm.submitTranslation();   // builds m_pendingUploadData for fr (the network call fails, fine)
        QCOMPARE(tm.m_uploadingLangCode, QStringLiteral("fr"));

        // The user switches language while the upload is pending.
        tm.setCurrentLanguage(QStringLiteral("de"));

        QVERIFY2(tm.m_uploadingLangCode == QStringLiteral("fr"),
                 "the pending upload must stay bound to the language whose data it holds, "
                 "not follow the UI to another language");
    }

    // Stop must mean stop — it briefly meant "skip to the next language".
    //
    // Routing per-language failures back into the batch (so one model reply of prose would not
    // abandon eleven other languages) introduced a non-fatal branch that calls processNext().
    // A user cancel took that branch, because cancelAutoTranslate set m_autoTranslateCancelled
    // but not m_autoTranslateFatal — despite the header comment claiming the flag covered "a
    // user cancel". Pressing Stop therefore began translating the NEXT language on the user's
    // paid key, which is close to the worst thing a Stop button can do.
    void acancelIsFatalToTheWholeRunNotJustOneLanguage()
    {
        Settings settings;
        TranslationManager tm(&m_nam, &settings);

        tm.m_autoTranslating = true;
        tm.m_autoTranslateFatal = false;
        tm.m_autoTranslateCancelled = false;

        tm.cancelAutoTranslate();

        QVERIFY2(tm.m_autoTranslateCancelled, "cancel must mark the run cancelled");
        QVERIFY2(tm.m_autoTranslateFatal,
                 "cancel must be FATAL: the batch's non-fatal branch advances to the next "
                 "language, so a non-fatal cancel spends money the user just asked to stop");
    }

    // A translation that loses or renumbers a placeholder is rejected, not applied.
    //
    // Renumbering is the dangerous one. "%1 frames" coming back as "Bilder" is visibly broken —
    // the number just vanishes from the label. But "%2 of %1" against a source of "%1 of %2"
    // looks perfectly fine in review and silently swaps two values at runtime. Neither is caught
    // by anything else: the string is applied, saved, and then uploaded to the community copy,
    // so one bad completion becomes every user's bad completion for that language.
    //
    // Reordering must still be allowed — that is what numbered placeholders are for, and word
    // order differs between languages. Only the SET has to match.
    void aTranslationThatBreaksPlaceholdersIsRejected()
    {
        QCOMPARE(TranslationManager::placeholderSet(QStringLiteral("%1 of %2")),
                 (QSet<int>{1, 2}));
        QCOMPARE(TranslationManager::placeholderSet(QStringLiteral("no placeholders")),
                 QSet<int>());

        // Reordering is legitimate — German and Japanese routinely need it.
        QCOMPARE(TranslationManager::placeholderSet(QStringLiteral("%2 von %1")),
                 TranslationManager::placeholderSet(QStringLiteral("%1 of %2")));

        // Dropping one is not.
        QVERIFY2(TranslationManager::placeholderSet(QStringLiteral("Bilder"))
                     != TranslationManager::placeholderSet(QStringLiteral("%1 frames")),
                 "a translation that drops its placeholder must not compare equal");

        // Nor is inventing one.
        QVERIFY2(TranslationManager::placeholderSet(QStringLiteral("%1 von %2"))
                     != TranslationManager::placeholderSet(QStringLiteral("%1 frames")),
                 "a translation that invents a placeholder must not compare equal");

        // Repetition is harmless: QString::arg replaces every occurrence of %1.
        QCOMPARE(TranslationManager::placeholderSet(QStringLiteral("%1 and %1 again")),
                 TranslationManager::placeholderSet(QStringLiteral("%1")));
    }

    // A community download cannot reintroduce a placeholder-broken string either.
    //
    // The AI path and this one need the same rule, but this is the one with the wider blast
    // radius: the backend accepts uploads unauthenticated, so one bad contribution reaches every
    // user of that language on their next update. The seven broken strings this rule found on a
    // real machine — including a screen-reader label that had lost "%1 of %2" in four languages
    // — arrived through exactly this door.
    void aCommunityUpdateCannotReintroduceBrokenPlaceholders()
    {
        Settings settings;
        TranslationManager tm(&m_nam, &settings);
        tm.setCurrentLanguage(QStringLiteral("de"));

        const QString key = QStringLiteral("bg.thumb");
        tm.registerString(key, QStringLiteral("Background image %1 of %2"));

        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression(QStringLiteral("placeholders do not match")));
        // "Skipped a bad string" is still a successful merge, not a refusal — refusal is
        // reserved for "could not read the local file".
        QVERIFY(tm.mergeLanguageUpdate(QJsonObject{{key, QStringLiteral("Hintergrundbild")}}));

        QVERIFY2(!tm.hasTranslation(key),
                 "a download that dropped its placeholders must be skipped, not applied");

        // The correctly-placeholdered version is accepted, reordered or not.
        QVERIFY(tm.mergeLanguageUpdate(QJsonObject{{key, QStringLiteral("Hintergrundbild %1 von %2")}}));
        QCOMPARE(tm.translateString(key, QStringLiteral("Background image %1 of %2")),
                 QStringLiteral("Hintergrundbild %1 von %2"));
    }

    // The scanner must not capture across a line.
    //
    // It did, and the result was registered, translated into four languages and uploaded to the
    // community server. AISettingsPage contains `fallback: "How to get an API key:"` — the TEXT
    // ends in "key:", so the Tr-component pattern matched at `key:"` INSIDE the string literal
    // and then ran to the next quote in the file, capturing 249 characters of QML source as a
    // translation key.
    //
    // A QML string literal never spans a raw newline, so a key or fallback that appears to is
    // always the regex having escaped its own quotes.
    void theScannerNeverCapturesAcrossALine()
    {
        // The exact shape that broke it: a literal whose text ends in "key:", followed by
        // ordinary QML on the lines after.
        const QString qml = QStringLiteral(
            "Tr {\n"
            "    key: \"real.key\"\n"
            "    fallback: \"How to get an API key:\"\n"
            "}\n"
            "Text {\n"
            "    color: Theme.textColor\n"
            "    font: Theme.subtitleFont\n"
            "}\n"
            "Tr { key: \"second.key\"; fallback: \"Second\" }\n");

        QRegularExpression keyRe(QStringLiteral("\\bkey\\s*:\\s*\"([^\"\\n]+)\""));
        QStringList captured;
        auto it = keyRe.globalMatch(qml);
        while (it.hasNext())
            captured << it.next().captured(1);

        for (const QString& c : captured) {
            QVERIFY2(!c.contains(QLatin1Char('\n')),
                     qPrintable(QStringLiteral("captured a key spanning lines: %1").arg(c.left(40))));
            QVERIFY2(!c.contains(QStringLiteral("Theme.")),
                     qPrintable(QStringLiteral("captured QML source as a key: %1").arg(c.left(40))));
        }
        QVERIFY2(captured.contains(QStringLiteral("real.key")), "must still find genuine keys");
        QVERIFY2(captured.contains(QStringLiteral("second.key")), "must still find keys on one line");
    }

    // "Use AI translation" cannot re-inject a placeholder-broken string.
    //
    // The AI cache is filled from two places: the apply path, which checks, and
    // loadAiTranslations(), which reads a file written before the check existed. So every
    // _ai.json on disk predates the rule and may still hold the strings it was added to stop.
    // This button pushes a cached value straight into the main file, from where it uploads —
    // the propagation loop the rule breaks, entered through a button instead of a download.
    void copyingACachedAiTranslationAlsoObeysThePlaceholderRule()
    {
        Settings settings;
        TranslationManager tm(&m_nam, &settings);
        tm.setCurrentLanguage(QStringLiteral("de"));

        const QString key = QStringLiteral("bg.thumb");
        const QString english = QStringLiteral("Background image %1 of %2");
        tm.registerString(key, english);

        // A cache entry of the kind that shipped: placeholders gone.
        tm.m_aiTranslations[english] = QStringLiteral("Hintergrundbild");

        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression(QStringLiteral("Refusing to copy AI translation")));
        tm.copyAiToFinal(english);

        QVERIFY2(!tm.hasTranslation(key),
                 "a cached AI translation missing its placeholders must not be copied into the "
                 "main file, where it would then be uploaded");

        // A well-formed cached value still copies.
        tm.m_aiTranslations[english] = QStringLiteral("Hintergrundbild %1 von %2");
        tm.copyAiToFinal(english);
        QCOMPARE(tm.translateString(key, english), QStringLiteral("Hintergrundbild %1 von %2"));
    }

};

QTEST_MAIN(TestTranslationSourceDrift)
#include "tst_translationsourcedrift.moc"
