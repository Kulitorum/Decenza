#pragma once

#include <QObject>
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

    // Translation lookup (auto-registers strings)
    Q_INVOKABLE QString translate(const QString& key, const QString& fallback);
    Q_INVOKABLE bool hasTranslation(const QString& key) const;

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
    //   2. All QML files in :/qml/ are parsed with regex
    //   3. All translation patterns are extracted and registered
    //   4. AI translation / upload now has access to all strings
    //
    Q_INVOKABLE void registerString(const QString& key, const QString& fallback);
    Q_INVOKABLE void scanAllStrings();

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
    void loadStringRegistry();
    void saveStringRegistry();
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
    void mergeLanguageUpdate(const QJsonObject& newTranslations);

    Settings* m_settings;
    QNetworkAccessManager* m_networkManager;

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
    QString m_batchCurrentProvider;  // Bypasses QSettings cache during batch ops
    bool m_batchProcessing = false;

    // Retry state (for 429 rate limiting)
    int m_uploadRetryCount = 0;
    int m_downloadRetryCount = 0;
    static constexpr int MAX_RETRIES = 100;
    static constexpr int RETRY_DELAY_MS = 10000;  // 10 seconds

    // Helper to get all configured AI providers
    QStringList getConfiguredProviders() const;

    // Helper to get provider for AI requests (uses batch override if active)
    QString getActiveProvider() const;

    // Backend base URL for translation API
    static constexpr const char* TRANSLATION_API_BASE = "https://api.decenza.coffee";
    // Endpoints used:
    //   GET /v1/translations/upload-url?lang=  - returns pre-signed S3 URL for uploads
    //   GET /v1/translations/languages         - returns list of available languages
    //   GET /v1/translations/languages/{code}  - returns translation file for a language
};
