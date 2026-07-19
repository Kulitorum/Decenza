#pragma once

#include <QObject>
#include <QJSValue>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QMap>
#include <QVariantMap>
#include <QStringList>

class Settings;

class TranslationManager : public QObject {
    Q_OBJECT

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

    // Must be called once, after the QML engine exists and before QML loads. TranslationManager
    // is exposed with setContextProperty rather than being created by the engine, so
    // qmlEngine(this) is null and the engine has to be handed in explicitly.
    void setJsEngine(QJSEngine* engine);
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
    Q_INVOKABLE void scanAllStrings();

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
    void mergeLanguageUpdate(const QJsonObject& newTranslations);

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
    void loadTranslations();
    void saveTranslations();
    void loadLanguageMetadata();
    void saveLanguageMetadata();
    // Runs the once-per-launch community-translation merge as soon as the network is up.
    // Replaces a fixed 3s delay; see the definition for why that delay was wrong in both
    // directions.
    void scheduleLanguageUpdateCheck();

    void loadStringRegistry();
    void saveStringRegistry();

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
    void propagateTranslationsToAllKeys();
    void recalculateUntranslatedCount();
    QString translationsDir() const;
    QString languageFilePath(const QString& langCode) const;

    // AI translation helpers
    void sendNextAutoTranslateBatch();
    void parseAutoTranslateResponse(const QByteArray& data);
    QString buildTranslationPrompt(const QVariantList& strings) const;
    void loadAiTranslations();
    void saveAiTranslations();

    // Language update helpers
    void loadUserOverrides();
    void saveUserOverrides();

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
    QString m_lastError;
    QString m_retryStatus;
    QByteArray m_pendingUploadData;

    // translations[key] = translated_text
    QMap<QString, QString> m_translations;

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
    // Merge a download into the on-disk file of a language that is NOT currently loaded.
    // Returns false if it refused or failed, having already warned and emitted
    // languageDownloaded(..., false, reason).
    bool mergeDownloadedLanguageFile(const QString& langCode, const QJsonObject& root);

    QStringList getConfiguredProviders() const;

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
#endif
};
