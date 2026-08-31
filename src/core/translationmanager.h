#pragma once

#include <QObject>
#include <QJSValue>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QMap>
#include <QVariantMap>
#include <QPointer>
#include <QSet>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

class QQmlEngine;
class QThread;
class Settings;

class TranslationManager : public QObject {
    Q_OBJECT

    // Registered as a QML singleton at COMPILE time, which is what lets qmllint (and
    // qmlcachegen, and the language server) resolve `TranslationManager` in the 3,668 QML
    // references to it. The macros are the load-bearing part: qmltyperegistrar reads them out
    // of the moc output and writes the type into the module's generated Decenza.qmltypes, and
    // that file is qmllint's only source of truth about C++ types. A runtime
    // qmlRegisterSingletonInstance() call is invisible to all three tools — before this, that
    // .qmltypes contained nothing but `Module {}` and every use of a C++ name in QML was an
    // unqualified-access warning.

    // Current language settings
    Q_PROPERTY(QString currentLanguage READ currentLanguage WRITE setCurrentLanguage NOTIFY currentLanguageChanged)
    Q_PROPERTY(bool editModeEnabled READ editModeEnabled WRITE setEditModeEnabled NOTIFY editModeEnabledChanged)

    // Translation status
    Q_PROPERTY(int untranslatedCount READ untranslatedCount NOTIFY untranslatedCountChanged)
    Q_PROPERTY(int totalStringCount READ totalStringCount NOTIFY totalStringCountChanged)
    Q_PROPERTY(QStringList availableLanguages READ availableLanguages NOTIFY availableLanguagesChanged)

    // Network status
    Q_PROPERTY(bool downloading READ isDownloading NOTIFY downloadingChanged)
    Q_PROPERTY(bool uploading READ isUploading NOTIFY uploadingChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    Q_PROPERTY(QString retryStatus READ retryStatus NOTIFY retryStatusChanged)

    // String scanning status
    Q_PROPERTY(bool scanning READ isScanning NOTIFY scanningChanged)
    Q_PROPERTY(int scanProgress READ scanProgress NOTIFY scanProgressChanged)
    Q_PROPERTY(int scanTotal READ scanTotal NOTIFY scanProgressChanged)

    // AI translation status
    Q_PROPERTY(bool autoTranslating READ isAutoTranslating NOTIFY autoTranslatingChanged)
    Q_PROPERTY(int autoTranslateProgress READ autoTranslateProgress NOTIFY autoTranslateProgressChanged)
    Q_PROPERTY(int autoTranslateTotal READ autoTranslateTotal NOTIFY autoTranslateProgressChanged)
    Q_PROPERTY(QString lastTranslatedText READ lastTranslatedText NOTIFY lastTranslatedTextChanged)

    // Version counter - increments when translations change, used for QML reactivity
    Q_PROPERTY(int translationVersion READ translationVersion NOTIFY translationsChanged)

    // The QML-facing translation lookup. A PROPERTY holding a callable, not a Q_INVOKABLE —
    // and that distinction is the whole fix for the language-switch staleness bug.
    //
    // A QML binding re-evaluates when a NOTIFY fires for a property it READ during its last
    // evaluation. Calling an invokable records no dependency, so
    //
    //     text: TranslationManager.translate("settings.title", "Settings")
    //
    // used to compute once at construction and then freeze: changing language left the old
    // language on screen until restart. Tr.qml worked around it by reading translationVersion
    // first, but 3,248 call sites in qml/ called translate() bare and none of them updated.
    //
    // Exposed as a property, reading `TranslationManager.translate` IS a property read, so the
    // binding depends on translationsChanged and re-runs — with the call-site syntax completely
    // unchanged. That is why this fix touches zero of those 3,248 lines.
    //
    // Proven before the codebase was swept: tests/tst_translationreactivity.cpp drives a real
    // QQmlEngine and includes a negative control showing the invokable form does NOT update.
    // If that test is ever deleted, a refactor back to Q_INVOKABLE would silently re-freeze
    // every translated string in the app.
    //
    // C++ callers use translateString() directly and are unaffected.
    Q_PROPERTY(QJSValue translate READ translateFn NOTIFY translationsChanged)

public:
    explicit TranslationManager(QNetworkAccessManager* networkManager, Settings* settings, QObject* parent = nullptr);
    ~TranslationManager() override;

    // Properties
    QString currentLanguage() const;
    void setCurrentLanguage(const QString& lang);
    bool editModeEnabled() const;
    void setEditModeEnabled(bool enabled);
    int untranslatedCount() const;
    int totalStringCount() const;
    QStringList availableLanguages() const;
    bool isDownloading() const;
    bool isUploading() const;
    QString lastError() const;
    QString retryStatus() const { return m_retryStatus; }
    bool isScanning() const;
    int scanProgress() const;
    int scanTotal() const;
    int translationVersion() const { return m_translationVersion; }
    bool isAutoTranslating() const { return m_autoTranslating; }
    int autoTranslateProgress() const { return m_autoTranslateProgress; }
    int autoTranslateTotal() const { return m_autoTranslateTotal; }
    QString lastTranslatedText() const { return m_lastTranslatedText; }

    // Translation lookup (auto-registers strings). This is the real implementation and the
    // entry point for C++ callers; QML reaches it through the `translate` property above.
    Q_INVOKABLE QString translateString(const QString& key, const QString& fallback);
    Q_INVOKABLE bool hasTranslation(const QString& key) const;

    // Must be called once, after the QML engine exists and before QML loads. The engine does not
    // construct this object (see create() above), so qmlEngine(this) is null and the engine has
    // to be handed in explicitly — `translate` is a QJSValue property and needs one to build the
    // callable it returns.
    //
    // Calling it twice is NOT harmless: handed a different engine it qFatal()s rather than
    // rebind, because bindings already hold a callable from the first engine and there is no
    // migration path. create() therefore calls it only when nothing is bound yet, and declines
    // the singleton to a second engine instead. Do not add an unconditional call.
    void setJsEngine(QJSEngine* engine);

    // Which engine `translate` is currently bound to, or null. create() needs this because
    // setJsEngine() deliberately qFatal()s rather than rebind, so it must not be called
    // speculatively for a second engine.
    QJSEngine* boundJsEngine() const { return m_jsEngine; }

    QJSValue translateFn();

    // Translation editing
    Q_INVOKABLE void setTranslation(const QString& key, const QString& translation);
    Q_INVOKABLE void deleteTranslation(const QString& key);

    // Language management
    Q_INVOKABLE void addLanguage(const QString& langCode, const QString& displayName, const QString& nativeName = QString());
    Q_INVOKABLE void deleteLanguage(const QString& langCode);
    Q_INVOKABLE QString getLanguageDisplayName(const QString& langCode) const;
    Q_INVOKABLE QString getLanguageNativeName(const QString& langCode) const;

    // String Registry System
    // ----------------------
    // The app uses dynamic string discovery: strings are registered when translate() is called.
    // This means strings on unvisited screens aren't in the registry until the user sees them.
    //
    // For complete translations (AI or community), we need ALL strings upfront.
    // scanAllStrings() solves this by parsing QML source files at runtime to extract
    // translatable strings, ensuring the registry is complete.
    //
    // Patterns detected:
    //   1. translate("key", "fallback") - direct function calls
    //   2. translationKey: "..." + translationFallback: "..." - ActionButton properties
    //   3. key: "..." + fallback: "..." - Tr component properties
    //
    // Flow:
    //   1. User enters Language settings → scanAllStrings() runs
    //   2. All QML files under :/qt/qml/Decenza/qml are parsed with regex — that is where
    //      qt_add_qml_module publishes them, NOT :/qml, which is what this said while the
    //      scanner looked there and silently found zero files
    //   3. All translation patterns are extracted and registered
    //   4. AI translation / upload now has access to all strings
    //
    // Decode the escapes a QML string literal can carry in this codebase. Deliberately a SUBSET
    // of what the QML engine accepts: it handles the sequences our fallbacks actually use and
    // leaves anything else byte-for-byte, where the engine would apply identity-escape rules
    // (\q -> q) and also decode \b \f \v \0 \xNN \u{...}. Leaving an unknown escape alone is the
    // safe direction for a scanner — it can only fail to decode, never invent a character.
    //
    // Why it must exist at all: the scanner reads QML as TEXT, so it sees the escape sequences,
    // while the runtime sees the characters they denote. Those two must agree, because both
    // write the registry — some fallbacks use \uXXXX (GraphLegend's
    // superscript two, a degree sign) and a scanner that stored the literal backslash-u would
    // disagree with the runtime forever, each seeing the other's value as a rewrite.
    static QString unescapeQmlLiteral(const QString& literal);

    Q_INVOKABLE void registerString(const QString& key, const QString& fallback);

    // Returns without doing the parsing: it enumerates the qrc tree on the calling thread, hands
    // the file list to a worker, and writes the registry from a queued callback on the main
    // thread. Watch `scanning` / `scanProgress`, or the scanFinished() signal, for completion.
    //
    // It used to be synchronous, and pumped QCoreApplication::processEvents() once per file "to
    // keep the UI responsive". That nested pump crashed the app (#1692, SIGABRT): the scan is
    // kicked off from SettingsLanguageTab's Component.onCompleted, which QML incubates
    // synchronously (SettingsPage.qml sets `asynchronous: false` on the tab Loaders) while
    // SettingsPage's TabBar.onCurrentIndexChanged handler is still on the stack. The symbolicated
    // stack then shows QCoreApplicationPrivate::sendPostedEvents -> QObject::event ->
    // ~QQmlElement<QQuickPage>, i.e. the outgoing page was destroyed inside the pump, taking its
    // TabBar with it; QObject::event deletes on DeferredDelete at qobject.cpp:1463-1464. Qt turns
    // a destroy-during-signal-handler into an abort, not a warning:
    // qtdeclarative/src/qml/qml/qqmlengine.cpp:1370-1396 (QQmlData::destroyed() -> qFatal() when
    // the handler isNotifying()).
    //
    // What is NOT established is which posted event did it. A bare processEvents() normally will
    // NOT deliver a queued DeferredDelete — qcoreapplication.cpp:1858-1873 allows one through only
    // when it was posted at a deeper loop+scope level, or was posted before the outermost loop, or
    // when DeferredDelete was passed explicitly — and qobject.cpp:2534-2557 records those levels
    // for exactly the `foo->deleteLater(); qApp->processEvents();` case. So one of those clauses
    // held on the device, or the destroyer was some other queued event (a queued signal, a timer,
    // a Loader status change) that deleted synchronously. Qt's own message lists "deleted
    // synchronously" ahead of the nested loop for that reason.
    //
    // The rule survives the uncertainty: a nested pump under a QML signal handler delivers
    // events that can destroy the object whose handler is running, and the failure is an abort.
    // Do not reintroduce one here.
    Q_INVOKABLE void scanAllStrings();

    // One (key, English fallback) pair lifted out of QML source text by the scanner.
    struct ScannedString {
        QString key;
        QString fallback;
    };

    // Pure text → pairs, in SCAN order: all of pattern 1, then 2, then 3, each positional within
    // itself. Not file order — do not "fix" it to positional, because that changes which fallback
    // wins for a key used twice, and the old inline loop applied them in exactly this sequence.
    // Static and state-free so it can run on the scan's worker thread — and so a test can
    // exercise the three patterns without an instance or the QML resource tree.
    static QList<ScannedString> parseTranslatableStrings(const QString& qmlSource);

    // Public + static so tst_aiproviders can assert these stay equal to each provider's first
    // catalog entry in aiprovider.cpp. That test is what stops this list going stale again.
    static QString fallbackTranslationModel(const QString& providerId);

    // Community Translation Sharing
    // -----------------------------
    // Translations are stored as: key → translated text (simple format)
    // Each string key maps directly to its translation.
    //
    // Upload: Serializes current translations to JSON, uploads to S3
    // Download: Fetches translation JSON, loads into local translation map
    //
    // Backend API (AWS):
    //   GET  /v1/translations/languages        - List available translations
    //   GET  /v1/translations/languages/{code} - Download a translation file
    //   GET  /v1/translations/upload-url?lang= - Get pre-signed S3 URL for upload
    //
    // Fold a downloaded set into the current one, keeping any local translation the download
    // does not carry and never overwriting a user override. Public because it is the meaning of
    // "apply a downloaded language", not an implementation detail: both the launch-time check
    // and the Update button go through it, and a test pins that Update no longer replaces.
    // Returns false if it REFUSED — the local file failed to load, so an empty in-memory map
    // is not evidence of an empty language and merging into it would replace the user's
    // translations with the server's. Callers must honour it: this was void, and the refusal
    // fell through to a success emit that told the user the update had applied.
    [[nodiscard]] bool mergeLanguageUpdate(const QJsonObject& newTranslations);

    Q_INVOKABLE void downloadLanguageList();
    Q_INVOKABLE void downloadLanguage(const QString& langCode);
    Q_INVOKABLE void exportTranslation(const QString& filePath);
    Q_INVOKABLE void importTranslation(const QString& filePath);
    Q_INVOKABLE void submitTranslation();

    // Utility
    Q_INVOKABLE QVariantList getUntranslatedStrings() const;
    Q_INVOKABLE QVariantList getAllStrings() const;
    Q_INVOKABLE QVariantList getGroupedStrings() const;  // Groups by fallback text
    Q_INVOKABLE QStringList getKeysForFallback(const QString& fallback) const;
    Q_INVOKABLE void setGroupTranslation(const QString& fallback, const QString& translation);  // Sets for all keys with fallback
    Q_INVOKABLE bool isGroupSplit(const QString& fallback) const;  // True if keys have different translations
    Q_INVOKABLE void mergeGroupTranslation(const QString& key);  // Resets key to use group's common translation
    Q_INVOKABLE bool isRtlLanguage(const QString& langCode) const;
    Q_INVOKABLE bool isRemoteLanguage(const QString& langCode) const;  // Available for download but not yet downloaded
    Q_INVOKABLE int getTranslationPercent(const QString& langCode) const;  // Get translation % for any language
    Q_INVOKABLE int uniqueStringCount() const;  // Count of unique fallback texts
    Q_INVOKABLE int uniqueUntranslatedCount() const;  // Count of unique untranslated fallback texts

    // AI auto-translation
    Q_INVOKABLE void autoTranslate();
    Q_INVOKABLE void cancelAutoTranslate();
    Q_INVOKABLE bool canAutoTranslate() const;

    // Batch translate and upload all languages (developer tool)
    // Cycles through all configured AI providers to fill gaps
    Q_INVOKABLE void translateAndUploadAllLanguages();

    // AI translation tracking
    Q_INVOKABLE QString getAiTranslation(const QString& fallback) const;  // Get AI translation for fallback text
    Q_INVOKABLE bool isAiGenerated(const QString& key) const;  // Check if translation is unmodified AI output
    Q_INVOKABLE void copyAiToFinal(const QString& fallback);  // Copy AI translation to final for all keys
    Q_INVOKABLE void clearAiTranslation(const QString& fallback);  // Clear AI translation for a string
    Q_INVOKABLE void clearAllAiTranslations();  // Clear all AI translations for current language

    // Auto-update language on startup
    void checkForLanguageUpdate();

signals:
    void currentLanguageChanged();
    void editModeEnabledChanged();
    void untranslatedCountChanged();
    void totalStringCountChanged();
    void availableLanguagesChanged();
    void downloadingChanged();
    void uploadingChanged();
    void lastErrorChanged();
    // An outcome the user should see that is NOT a failure — "stopped, N strings saved". These
    // used to travel through lastError, which made the toast dress a successful stop in error
    // styling. Informational is opt-in via this signal; anything set on lastError still gets
    // error styling by default, which is the correct default for a message nobody classified.
    void translationNotice(const QString& message);
    void retryStatusChanged();
    void translationSubmitted(bool success, const QString& message);
    void scanningChanged();
    void scanProgressChanged();
    void scanFinished(int stringsFound);

    void translationsChanged();
    void translationChanged(const QString& key);
    void languageDownloaded(const QString& langCode, bool success, const QString& error);
    void languageListDownloaded(bool success);

    void autoTranslatingChanged();
    void autoTranslateProgressChanged();
    void autoTranslateFinished(bool success, const QString& message);
    void lastTranslatedTextChanged();
    void batchTranslateUploadFinished(bool success, const QString& message);

private slots:
    void onLanguageListFetched(QNetworkReply* reply);
    void onLanguageFileFetched(QNetworkReply* reply);
    void onAutoTranslateBatchReply(QNetworkReply* reply);
    void onUploadUrlReceived(QNetworkReply* reply);
    void onTranslationUploaded(QNetworkReply* reply);

private:
    // The instance create() hands to the engine. Not owned here — main.cpp's stack object
    // outlives the engine, which is why create() must also pin CppOwnership.

    void loadTranslations();
    // Every verdict-returning helper below is [[nodiscard]], and CMake promotes a discarded
    // verdict to a hard build error (-Werror=unused-result / MSVC /we4834). This is not
    // decoration: seven bugs on one branch had the identical shape — a save whose refusal a
    // caller silently dropped, then reported success over — and the last two were introduced
    // while fixing the previous ones, so a convention demonstrably did not hold. A deliberate
    // discard must be written `(void)call();` with a comment saying why the failure is
    // tolerable (in practice: the data is rediscovered or rewritten on the next launch, and
    // the helper has already warned and set lastError).
    //
    // Returns false if it refused (the local file failed to load, so the in-memory map is empty
    // by failure) or the write failed. Callers that report success must honour it.
    // Atomic JSON write with a reported verdict. All the save* helpers route through this;
    // see the comment on the definition for why the old QFile+if(open) shape was a bug factory.
    [[nodiscard]] bool writeJsonFile(const QString& path, const QJsonDocument& doc, const QString& what);

    [[nodiscard]] bool saveTranslations();
    void loadLanguageMetadata();
    [[nodiscard]] bool saveLanguageMetadata();
    // Runs the once-per-launch community-translation merge as soon as the network is up.
    // Replaces a fixed 3s delay; see the definition for why that delay was wrong in both
    // directions.
    void scheduleLanguageUpdateCheck();

    void loadStringRegistry();
    [[nodiscard]] bool saveStringRegistry();

    // Record the CURRENT English for a key, and deal with the case where it changed.
    //
    // Every registry write used to be guarded by `if (!m_stringRegistry.contains(key))`, so a
    // key's English was captured once and never revisited — including by a full rescan. The
    // registry therefore drifted into holding text the app no longer displays, and since it is
    // what the AI translator is prompted with and what the community upload publishes, the
    // drift propagated outward. `settings.ai.remoteMcp.setupGuidance` is the worked example:
    // rewritten in QML to drop its arrows, still stored here with them.
    //
    // Returns true when the registry changed, so callers can decide whether to save.
    bool noteSourceString(const QString& key, const QString& fallback);

    // Main-thread tail of scanAllStrings(): writes what the worker parsed into the registry,
    // saves, and reports. Nothing on the worker thread touches m_stringRegistry — the scan's
    // writes all happen here. `unreadableFiles` are the QML files the worker could not open;
    // they make the registry short, which matters because it is what gets AI-translated and
    // published, so they are reported rather than swallowed.
    void applyScanResults(const QList<ScannedString>& found, const QSet<QString>& seenInQml,
                          const QStringList& unreadableFiles);

    void propagateTranslationsToAllKeys();
    void recalculateUntranslatedCount();
    QString translationsDir() const;
    QString languageFilePath(const QString& langCode) const;

    // AI translation helpers
    void sendNextAutoTranslateBatch();
    // Returns false when the provider answered but the reply was unusable — empty content, or
    // not the JSON object that was asked for. That is a FAILED batch, not zero translations,
    // and the difference matters: inside the bulk run a "success" triggers the upload.
    [[nodiscard]] bool parseAutoTranslateResponse(const QByteArray& data);

    // Placeholders a string carries, e.g. {1, 2} for "%1 of %2". A translation must carry the
    // same set: reordering is fine and expected, losing or inventing one is not.
    static QSet<int> placeholderSet(const QString& text);
    QString buildTranslationPrompt(const QVariantList& strings) const;
    void loadAiTranslations();
    [[nodiscard]] bool saveAiTranslations();

    // Language update helpers
    void loadUserOverrides();
    [[nodiscard]] bool saveUserOverrides();

    Settings* m_settings;
    QNetworkAccessManager* m_networkManager;

    QJSEngine* m_jsEngine = nullptr;
    QJSValue m_translateFn;
    bool m_warnedNoEngine = false;  // warn once — see translateFn()

    QString m_currentLanguage;
    bool m_editModeEnabled = false;
    bool m_downloading = false;
    bool m_uploading = false;
    bool m_scanning = false;
    int m_scanProgress = 0;
    int m_scanTotal = 0;

    // A scan has finished at least once this session. The bulk translator needs the complete
    // string list, which it used to get by calling the (then synchronous) scan inline; with an
    // asynchronous scan it parks itself on scanFinished() instead.
    bool m_scanCompleted = false;
    bool m_batchAwaitingScan = false;

    // Null once the worker has finished and deleteLater() has collected it. The destructor waits
    // on it while it is alive, so a scan can never outlive the object it posts its results to.
    QPointer<QThread> m_scanThread;
    QString m_lastError;
    QString m_retryStatus;
    QByteArray m_pendingUploadData;

    // The language m_pendingUploadData was built for. These two must travel together: the 429
    // retry used to re-derive the language from m_currentLanguage at fire time, so switching
    // language during the wait published one language's strings under another's name.
    QString m_uploadingLangCode;

    // translations[key] = translated_text
    QMap<QString, QString> m_translations;

    // True when the local language file EXISTS but could not be read or parsed, so
    // m_translations is empty by failure rather than by fact. mergeLanguageUpdate() refuses
    // when this is set: merging into a wrongly-empty map and saving is a whole-file replace.
    bool m_translationsLoadFailed = false;

    // Registry of all known string keys and their English fallbacks
    // registry[key] = english_fallback
    QMap<QString, QString> m_stringRegistry;

    // Guards the launch-time language update so it runs once even if reachability flaps.
    bool m_launchUpdateCheckDone = false;

    // Language metadata: {langCode: {displayName, nativeName, isRtl}}
    QMap<QString, QVariantMap> m_languageMetadata;

    // List of available language codes (local + community)
    QStringList m_availableLanguages;

    int m_untranslatedCount = 0;
    int m_translationVersion = 0;

    // Track which language is being downloaded
    QString m_downloadingLangCode;

    // Dirty flag for batch saving string registry
    bool m_registryDirty = false;

    // AI auto-translation state
    bool m_autoTranslating = false;
    bool m_autoTranslateCancelled = false;
    int m_autoTranslateProgress = 0;

    // Batches the provider answered with something unusable. Reset per run; any non-zero value
    // turns the run's result from success into a named failure, which also stops the bulk path
    // from uploading an untranslated file.
    int m_autoTranslateParseFailures = 0;

    // Translations discarded this run because they did not preserve their placeholders.
    int m_autoTranslateRejected = 0;

    // True when a run failed for a reason that would recur for EVERY remaining language — the
    // provider erroring, a user cancel, or a failed save. A batch stops on this. It exists
    // because routing parse failures into autoTranslateFinished(false) made the bulk run treat
    // one model reply of prose as grounds to abandon the other eleven languages, which the
    // terminal branch was never meant to cover.
    bool m_autoTranslateFatal = false;
    int m_autoTranslateTotal = 0;
    int m_pendingBatchCount = 0;  // Track parallel batch requests
    int m_translationRunId = 0;   // Increments each translation run to identify stale responses
    QVariantList m_stringsToTranslate;
    QString m_lastTranslatedText;
    static constexpr int AUTO_TRANSLATE_BATCH_SIZE = 25;

    // AI translations - stored per unique fallback text (not per key)
    // m_aiTranslations[fallback] = AI-generated translation
    QMap<QString, QString> m_aiTranslations;

    // Set of keys whose current translation is unmodified AI output
    QSet<QString> m_aiGenerated;

    // Set of keys that the user has explicitly edited (preserved during language updates)
    QSet<QString> m_userOverrides;

    // Batch translate+upload state
    QStringList m_batchLanguageQueue;
    QStringList m_batchProviderQueue;
    QString m_originalProvider;
    QString m_originalLanguage;   // restored when a batch finishes; see translateAndUploadAllLanguages

    // Languages whose upload failed during a batch, as "code: reason".
    //
    // The batch reported success unconditionally: the upload handler read its `success` flag
    // only to choose a word for a qDebug line, then advanced regardless. A run that hit the
    // hourly rate limit on languages 11 and 12 of 12 still finished "Batch processing complete"
    // with two languages never sent. That is the same shape as the provider substitution this
    // change set out to kill — a run reporting success for work it did not do — and it was
    // thirty lines away in the same function.
    QStringList m_batchFailedUploads;
    QString m_batchCurrentProvider;  // Bypasses QSettings cache during batch ops
    bool m_batchProcessing = false;

    // Retry state (for 429 rate limiting)
    int m_uploadRetryCount = 0;
    int m_downloadRetryCount = 0;
    // Retries exist for a burst that clears in seconds, not for an exhausted quota.
    //
    // This was 100 retries at 10s — about 17 minutes of hammering a server we do not own,
    // against a limit whose window is a FULL HOUR (the backend allows 10 translation
    // upload-url requests per IP per hour). It could not succeed by construction: the window
    // cannot reset inside the retry span, so every one of those 100 requests was guaranteed to
    // fail. Worse, 429 was the ONLY status it retried — the one case where retrying is futile.
    //
    // Three quick attempts covers a genuine burst; past that the honest answer is that the
    // quota is spent and the user should come back later, which retryStatus now says.
    static constexpr int MAX_RETRIES = 3;
    static constexpr int RETRY_DELAY_MS = 10000;  // 10 seconds

    // The backend's window for translation endpoints, used only to tell the user roughly how
    // long to wait. Mirrors RATE_LIMIT_WINDOW_SECONDS in the shotmap backend.
    static constexpr int RATE_LIMIT_WINDOW_MINUTES = 60;

    // Which model to translate with: the user's configured model if set, else the catalog
    // fallback for their provider.
    QString translationModelFor(const QString& provider, const QString& fallback) const;

    // Human-readable name of the selected provider, for error messages that must say which
    // provider failed rather than just "translation failed".
    QString selectedProviderLabel() const;

    // The SELECTED provider, and nothing else — despite the plural name, this returns at most
    // one entry. It used to return every provider holding a key, which is how a user with
    // OpenAI selected got billed on Anthropic. The list shape is kept because callers iterate.
    QStringList getConfiguredProviders() const;

    // Merge a download into the on-disk file of a language that is NOT currently loaded.
    // Returns false if it refused or failed, having already warned and emitted
    // languageDownloaded(..., false, reason).
    //
    // Note this is the RARE branch: both UI buttons set currentLanguage to the language they
    // are about to download, so the in-memory path (mergeLanguageUpdate) is what normally runs.
    // Reachable when the user switches language while a download is in flight.
    // Body of the language-download reply slot. Separate so a test can drive the whole
    // apply step — merge, metadata, signals — rather than just the merge helper.
    void applyFetchedLanguage(const QString& langCode, const QJsonObject& root);

    [[nodiscard]] bool mergeDownloadedLanguageFile(const QString& langCode, const QJsonObject& root);

    // Helper to get provider for AI requests (uses batch override if active)
    QString getActiveProvider() const;

    // Backend base URL for translation API
    static constexpr const char* TRANSLATION_API_BASE = "https://api.decenza.coffee";
    // Endpoints used:
    //   GET /v1/translations/upload-url?lang=  - returns pre-signed S3 URL for uploads
    //   GET /v1/translations/languages         - returns list of available languages
    //   GET /v1/translations/languages/{code}  - returns translation file for a language

#ifdef DECENZA_TESTING
    // getConfiguredProviders() is the one function in this class with money attached: it decides
    // whose API key gets spent. Reachable from a test for that reason — the rule that it returns
    // ONLY the selected provider is otherwise enforced by nothing, and re-adding a single
    // `if (!openaiApiKey().isEmpty())` would leave the whole suite green while billing a user
    // on an account they did not choose. That is exactly how the retired-model bug survived.
    friend class TestTranslationSourceDrift;

    // The scan's main-thread tail (applyScanResults) and the flags that order it. The ordering
    // between m_scanCompleted and scanFinished() is what stops the parked bulk translator from
    // re-parking forever; nothing enforces it but a test that reads both from inside the signal.
    friend class TestTranslationScan;
#endif
};

// Translate `key`, falling back to the English literal when no TranslationManager
// is installed — the case tests and command-line tools run in.
//
// One definition because there were nine. Every class holding an optional
// TranslationManager* had hand-copied this same body into its own `tr_`, so a
// change to the fallback rule (a null `fallback`, or what an empty
// translateString() should mean) had to be found and reapplied nine times, with
// nothing failing when one was missed.
inline QString translateOrFallback(TranslationManager* manager,
                                   const QString& key, const QString& fallback) {
    if (manager)
        return manager->translateString(key, fallback);
    return fallback;
}

// Literal-key overload, which is how most callers spell it.
inline QString translateOrFallback(TranslationManager* manager,
                                   const char* key, const char* fallback) {
    return translateOrFallback(manager, QString::fromUtf8(key), QString::fromUtf8(fallback));
}

