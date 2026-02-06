#include "translationmanager.h"
#include "settings.h"
#include <QStandardPaths>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>
#include <QDesktopServices>
#include <QUrl>
#include <QUrlQuery>
#include <QTimer>
#include <QSet>
#include <QRegularExpression>
#include <QCoreApplication>
#include <functional>
#include <memory>

TranslationManager::TranslationManager(Settings* settings, QObject* parent)
    : QObject(parent)
    , m_settings(settings)
    , m_networkManager(new QNetworkAccessManager(this))
{
    // Ensure translations directory exists
    QDir dir(translationsDir());
    if (!dir.exists()) {
        dir.mkpath(".");
    }

    // Load saved language from settings
    m_currentLanguage = m_settings->value("localization/language", "en").toString();

    // Load language metadata (list of available languages)
    loadLanguageMetadata();

    // Ensure English is always available
    if (!m_languageMetadata.contains("en")) {
        m_languageMetadata["en"] = QVariantMap{
            {"displayName", "English"},
            {"nativeName", "English"},
            {"isRtl", false}
        };
        saveLanguageMetadata();
    }

    // Update available languages list
    m_availableLanguages = m_languageMetadata.keys();
    if (!m_availableLanguages.contains("en")) {
        m_availableLanguages.prepend("en");
    }

    // Load string registry
    loadStringRegistry();

    // Clean up any empty/whitespace keys that might have been saved previously
    QStringList keysToRemove;
    for (auto it = m_stringRegistry.constBegin(); it != m_stringRegistry.constEnd(); ++it) {
        if (it.key().trimmed().isEmpty() || it.value().trimmed().isEmpty()) {
            keysToRemove.append(it.key());
        }
    }
    if (!keysToRemove.isEmpty()) {
        for (const QString& key : keysToRemove) {
            m_stringRegistry.remove(key);
        }
        qDebug() << "TranslationManager: Cleaned up" << keysToRemove.size() << "empty registry entries";
        saveStringRegistry();
    }

    // Load translations for current language
    loadTranslations();

    // Load user overrides for current language
    loadUserOverrides();

    // Load AI translations for current language
    loadAiTranslations();

    // Calculate initial untranslated count
    recalculateUntranslatedCount();

    // Timer to batch-save string registry
    QTimer* registrySaveTimer = new QTimer(this);
    registrySaveTimer->setInterval(5000);  // Save every 5 seconds if dirty
    connect(registrySaveTimer, &QTimer::timeout, this, [this]() {
        if (m_registryDirty) {
            saveStringRegistry();
            recalculateUntranslatedCount();
            m_registryDirty = false;
            emit totalStringCountChanged();
        }
    });
    registrySaveTimer->start();

    qDebug() << "TranslationManager initialized. Language:" << m_currentLanguage
             << "Strings:" << m_stringRegistry.size()
             << "Translations:" << m_translations.size()
             << "AI Translations:" << m_aiTranslations.size();

    // Check for language updates after startup (delayed to not block app launch)
    QTimer::singleShot(3000, this, &TranslationManager::checkForLanguageUpdate);
}

QString TranslationManager::translationsDir() const
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/translations";
}

QString TranslationManager::languageFilePath(const QString& langCode) const
{
    return translationsDir() + "/" + langCode + ".json";
}

// --- Properties ---

QString TranslationManager::currentLanguage() const
{
    return m_currentLanguage;
}

void TranslationManager::setCurrentLanguage(const QString& lang)
{
    if (m_currentLanguage != lang) {
        m_currentLanguage = lang;
        m_settings->setValue("localization/language", lang);
        loadTranslations();
        loadUserOverrides();
        loadAiTranslations();
        recalculateUntranslatedCount();
        m_translationVersion++;
        emit translationsChanged();
        emit currentLanguageChanged();
    }
}

bool TranslationManager::editModeEnabled() const
{
    return m_editModeEnabled;
}

void TranslationManager::setEditModeEnabled(bool enabled)
{
    if (m_editModeEnabled != enabled) {
        m_editModeEnabled = enabled;
        emit editModeEnabledChanged();
    }
}

int TranslationManager::untranslatedCount() const
{
    return m_untranslatedCount;
}

int TranslationManager::totalStringCount() const
{
    return m_stringRegistry.size();
}

QStringList TranslationManager::availableLanguages() const
{
    return m_availableLanguages;
}

bool TranslationManager::isDownloading() const
{
    return m_downloading;
}

bool TranslationManager::isUploading() const
{
    return m_uploading;
}

bool TranslationManager::isScanning() const
{
    return m_scanning;
}

int TranslationManager::scanProgress() const
{
    return m_scanProgress;
}

int TranslationManager::scanTotal() const
{
    return m_scanTotal;
}

QString TranslationManager::lastError() const
{
    return m_lastError;
}

// --- Translation lookup ---

QString TranslationManager::translate(const QString& key, const QString& fallback)
{
    // Skip empty/whitespace keys or fallbacks
    if (key.trimmed().isEmpty() || fallback.trimmed().isEmpty()) {
        return fallback;
    }

    // Auto-register the string if not already registered
    if (!m_stringRegistry.contains(key)) {
        m_stringRegistry[key] = fallback;
        // Don't save on every call - batch save periodically
        m_registryDirty = true;

        // Propagate existing translation from other keys with the same fallback
        // This ensures new keys get translations that were applied before they were registered
        if (m_currentLanguage != "en") {
            QString normalizedFallback = fallback.trimmed();
            for (auto it = m_stringRegistry.constBegin(); it != m_stringRegistry.constEnd(); ++it) {
                if (it.key() != key && it.value().trimmed() == normalizedFallback) {
                    QString existingTranslation = m_translations.value(it.key());
                    if (!existingTranslation.isEmpty()) {
                        m_translations[key] = existingTranslation;
                        if (m_aiGenerated.contains(it.key())) {
                            m_aiGenerated.insert(key);
                        }
                        break;
                    }
                }
            }
        }
    }

    // Check for custom translation (including English customizations)
    if (m_translations.contains(key) && !m_translations.value(key).isEmpty()) {
        return m_translations.value(key);
    }

    // Return fallback
    return fallback;
}

bool TranslationManager::hasTranslation(const QString& key) const
{
    return m_translations.contains(key) && !m_translations.value(key).isEmpty();
}

// --- Translation editing ---

void TranslationManager::setTranslation(const QString& key, const QString& translation)
{
    m_translations[key] = translation;
    m_aiGenerated.remove(key);  // User edited, no longer AI-generated
    m_userOverrides.insert(key);  // Track as user override (preserved during updates)
    saveTranslations();
    saveUserOverrides();
    recalculateUntranslatedCount();
    m_translationVersion++;
    emit translationsChanged();
    emit translationChanged(key);
}

void TranslationManager::deleteTranslation(const QString& key)
{
    if (m_translations.contains(key)) {
        m_translations.remove(key);
        saveTranslations();
        recalculateUntranslatedCount();
        m_translationVersion++;
        emit translationsChanged();
        emit translationChanged(key);
    }
}

// --- Language management ---

void TranslationManager::addLanguage(const QString& langCode, const QString& displayName, const QString& nativeName)
{
    if (langCode.isEmpty() || m_languageMetadata.contains(langCode)) {
        return;
    }

    // Determine RTL based on language code
    static const QStringList rtlLanguages = {"ar", "he", "fa", "ur"};
    bool isRtl = rtlLanguages.contains(langCode);

    m_languageMetadata[langCode] = QVariantMap{
        {"displayName", displayName},
        {"nativeName", nativeName.isEmpty() ? displayName : nativeName},
        {"isRtl", isRtl}
    };

    saveLanguageMetadata();

    // Create empty translation file
    QFile file(languageFilePath(langCode));
    if (file.open(QIODevice::WriteOnly)) {
        QJsonObject root;
        root["language"] = langCode;
        root["displayName"] = displayName;
        root["nativeName"] = nativeName.isEmpty() ? displayName : nativeName;
        root["translations"] = QJsonObject();
        file.write(QJsonDocument(root).toJson());
        file.close();
    }

    m_availableLanguages = m_languageMetadata.keys();
    emit availableLanguagesChanged();

    qDebug() << "Added language:" << langCode << displayName;
}

void TranslationManager::deleteLanguage(const QString& langCode)
{
    if (langCode == "en" || !m_languageMetadata.contains(langCode)) {
        return;  // Can't delete English
    }

    m_languageMetadata.remove(langCode);
    saveLanguageMetadata();

    // Delete translation file
    QFile::remove(languageFilePath(langCode));

    m_availableLanguages = m_languageMetadata.keys();
    emit availableLanguagesChanged();

    // Switch to English if current language was deleted
    if (m_currentLanguage == langCode) {
        setCurrentLanguage("en");
    }

    qDebug() << "Deleted language:" << langCode;
}

QString TranslationManager::getLanguageDisplayName(const QString& langCode) const
{
    if (m_languageMetadata.contains(langCode)) {
        return m_languageMetadata[langCode]["displayName"].toString();
    }
    return langCode;
}

QString TranslationManager::getLanguageNativeName(const QString& langCode) const
{
    if (m_languageMetadata.contains(langCode)) {
        return m_languageMetadata[langCode]["nativeName"].toString();
    }
    return langCode;
}

// --- String registry ---

void TranslationManager::registerString(const QString& key, const QString& fallback)
{
    // Skip empty/whitespace keys or fallbacks
    if (key.trimmed().isEmpty() || fallback.trimmed().isEmpty()) {
        return;
    }

    if (!m_stringRegistry.contains(key)) {
        m_stringRegistry[key] = fallback;
        saveStringRegistry();
        recalculateUntranslatedCount();
        emit totalStringCountChanged();
    }
}

// Scan all QML source files to discover every translatable string in the app.
//
// Why this is needed:
// - Strings are normally registered when translate() is called at runtime
// - This means strings on screens the user hasn't visited aren't in the registry
// - AI translation and community sharing need the complete list of strings
// - By scanning QML files, we find ALL translate("key", "fallback") calls
//
// This runs when entering the Language settings page.
void TranslationManager::scanAllStrings()
{
    if (m_scanning) {
        return;
    }

    m_scanning = true;
    m_scanProgress = 0;
    emit scanningChanged();

    // Collect all QML files from the Qt resource system (:/qml/)
    QStringList qmlFiles;
    QDirIterator it(":/qml", QStringList() << "*.qml", QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        qmlFiles.append(it.next());
    }

    m_scanTotal = qmlFiles.size();
    emit scanProgressChanged();

    qDebug() << "Scanning" << m_scanTotal << "QML files for translatable strings...";

    // Pattern 1: Direct translate() calls - translate("key", "fallback")
    QRegularExpression directCallRegex("translate\\s*\\(\\s*\"([^\"]+)\"\\s*,\\s*\"([^\"]+)\"\\s*\\)");

    // Pattern 2: ActionButton's translationKey/translationFallback properties
    QRegularExpression propKeyRegex("translationKey\\s*:\\s*\"([^\"]+)\"");
    QRegularExpression propFallbackRegex("translationFallback\\s*:\\s*\"([^\"]+)\"");

    // Pattern 3: Tr component's key/fallback properties - Tr { key: "..."; fallback: "..." }
    QRegularExpression trKeyRegex("\\bkey\\s*:\\s*\"([^\"]+)\"");
    QRegularExpression trFallbackRegex("\\bfallback\\s*:\\s*\"([^\"]+)\"");

    int stringsFound = 0;
    int initialCount = m_stringRegistry.size();

    for (const QString& filePath : qmlFiles) {
        QFile file(filePath);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QString content = QString::fromUtf8(file.readAll());
            file.close();

            // Pattern 1: Direct translate() calls
            QRegularExpressionMatchIterator matchIt = directCallRegex.globalMatch(content);
            while (matchIt.hasNext()) {
                QRegularExpressionMatch match = matchIt.next();
                QString key = match.captured(1);
                QString fallback = match.captured(2);

                // Unescape common escape sequences
                key.replace("\\\"", "\"").replace("\\n", "\n").replace("\\t", "\t");
                fallback.replace("\\\"", "\"").replace("\\n", "\n").replace("\\t", "\t");

                if (!key.trimmed().isEmpty() && !fallback.trimmed().isEmpty()) {
                    if (!m_stringRegistry.contains(key)) {
                        m_stringRegistry[key] = fallback;
                        stringsFound++;
                    }
                }
            }

            // Pattern 2: Property-based translations (translationKey + translationFallback pairs)
            // Collect all keys and fallbacks, then match them by proximity in the file
            QMap<int, QString> keyPositions;  // position -> key
            QMap<int, QString> fallbackPositions;  // position -> fallback

            QRegularExpressionMatchIterator keyIt = propKeyRegex.globalMatch(content);
            while (keyIt.hasNext()) {
                QRegularExpressionMatch match = keyIt.next();
                keyPositions[match.capturedStart()] = match.captured(1);
            }

            QRegularExpressionMatchIterator fallbackIt = propFallbackRegex.globalMatch(content);
            while (fallbackIt.hasNext()) {
                QRegularExpressionMatch match = fallbackIt.next();
                fallbackPositions[static_cast<int>(match.capturedStart())] = match.captured(1);
            }

            // Match keys with their nearest following fallback
            for (auto it = keyPositions.constBegin(); it != keyPositions.constEnd(); ++it) {
                int keyPos = it.key();
                QString key = it.value();

                // Find the nearest fallback after this key (within 200 chars)
                for (auto fbIt = fallbackPositions.constBegin(); fbIt != fallbackPositions.constEnd(); ++fbIt) {
                    int fbPos = fbIt.key();
                    if (fbPos > keyPos && fbPos - keyPos < 200) {
                        QString fallback = fbIt.value();

                        // Unescape
                        key.replace("\\\"", "\"").replace("\\n", "\n").replace("\\t", "\t");
                        fallback.replace("\\\"", "\"").replace("\\n", "\n").replace("\\t", "\t");

                        if (!key.trimmed().isEmpty() && !fallback.trimmed().isEmpty()) {
                            if (!m_stringRegistry.contains(key)) {
                                m_stringRegistry[key] = fallback;
                                stringsFound++;
                            }
                        }
                        break;  // Found the matching fallback
                    }
                }
            }

            // Pattern 3: Tr component's key/fallback properties
            QMap<int, QString> trKeyPositions;
            QMap<int, QString> trFallbackPositions;

            QRegularExpressionMatchIterator trKeyIt = trKeyRegex.globalMatch(content);
            while (trKeyIt.hasNext()) {
                QRegularExpressionMatch match = trKeyIt.next();
                trKeyPositions[match.capturedStart()] = match.captured(1);
            }

            QRegularExpressionMatchIterator trFallbackIt = trFallbackRegex.globalMatch(content);
            while (trFallbackIt.hasNext()) {
                QRegularExpressionMatch match = trFallbackIt.next();
                trFallbackPositions[match.capturedStart()] = match.captured(1);
            }

            // Match keys with their nearest fallback (within 200 chars, in either direction)
            for (auto it = trKeyPositions.constBegin(); it != trKeyPositions.constEnd(); ++it) {
                int keyPos = it.key();
                QString key = it.value();

                // Find the nearest fallback (can be before or after the key)
                QString fallback;
                int minDistance = 200;

                for (auto fbIt = trFallbackPositions.constBegin(); fbIt != trFallbackPositions.constEnd(); ++fbIt) {
                    int fbPos = fbIt.key();
                    int distance = qAbs(fbPos - keyPos);
                    if (distance < minDistance) {
                        minDistance = distance;
                        fallback = fbIt.value();
                    }
                }

                if (!fallback.trimmed().isEmpty()) {
                    // Unescape
                    QString keyClean = key;
                    QString fallbackClean = fallback;
                    keyClean.replace("\\\"", "\"").replace("\\n", "\n").replace("\\t", "\t");
                    fallbackClean.replace("\\\"", "\"").replace("\\n", "\n").replace("\\t", "\t");

                    if (!keyClean.trimmed().isEmpty() && !fallbackClean.trimmed().isEmpty()) {
                        if (!m_stringRegistry.contains(keyClean)) {
                            m_stringRegistry[keyClean] = fallbackClean;
                            stringsFound++;
                        }
                    }
                }
            }
        }

        m_scanProgress++;
        emit scanProgressChanged();

        // Process events to keep UI responsive
        QCoreApplication::processEvents();
    }

    // Save the updated registry
    if (stringsFound > 0) {
        saveStringRegistry();
        recalculateUntranslatedCount();
        emit totalStringCountChanged();
    }

    m_scanning = false;
    emit scanningChanged();
    emit scanFinished(m_stringRegistry.size() - initialCount);

    qDebug() << "Scan complete. Found" << stringsFound << "new strings. Total:" << m_stringRegistry.size();
}

// --- Community translations ---

void TranslationManager::downloadLanguageList()
{
    if (m_downloading) {
        return;
    }

    // Reset retry counter for new download
    m_downloadRetryCount = 0;

    m_downloading = true;
    emit downloadingChanged();

    QString url = QString("%1/languages").arg(TRANSLATION_API_BASE);
    qDebug() << "Fetching language list from:" << url;

    QNetworkRequest request{QUrl(url)};
    QNetworkReply* reply = m_networkManager->get(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onLanguageListFetched(reply);
    });
}

void TranslationManager::downloadLanguage(const QString& langCode)
{
    if (m_downloading || langCode == "en") {
        return;
    }

    // Reset retry counter for new download
    m_downloadRetryCount = 0;

    m_downloading = true;
    m_downloadingLangCode = langCode;
    emit downloadingChanged();

    QString url = QString("%1/languages/%2").arg(TRANSLATION_API_BASE, langCode);
    qDebug() << "Fetching language file from:" << url;

    QNetworkRequest request{QUrl(url)};
    QNetworkReply* reply = m_networkManager->get(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onLanguageFileFetched(reply);
    });
}

void TranslationManager::onLanguageListFetched(QNetworkReply* reply)
{
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        // Check for 429 Too Many Requests - retry after delay
        int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (statusCode == 429 && m_downloadRetryCount < MAX_RETRIES) {
            m_downloadRetryCount++;
            qDebug() << "Language list rate limited (429), retrying in" << (RETRY_DELAY_MS / 1000)
                     << "seconds... (attempt" << m_downloadRetryCount << "of" << MAX_RETRIES << ")";

            // Show retry status to user
            m_retryStatus = QString("Server busy, retrying download %1/%2...").arg(m_downloadRetryCount).arg(MAX_RETRIES);
            emit retryStatusChanged();

            // Schedule retry after delay
            QTimer::singleShot(RETRY_DELAY_MS, this, [this]() {
                QString url = QString("%1/languages").arg(TRANSLATION_API_BASE);
                qDebug() << "Retrying language list from:" << url;

                QNetworkRequest request{QUrl(url)};
                QNetworkReply* retryReply = m_networkManager->get(request);

                connect(retryReply, &QNetworkReply::finished, this, [this, retryReply]() {
                    onLanguageListFetched(retryReply);
                });
            });
            return;
        }

        m_downloading = false;
        m_downloadRetryCount = 0;
        m_retryStatus.clear();
        emit retryStatusChanged();
        emit downloadingChanged();

        m_lastError = QString("Failed to fetch language list: %1").arg(reply->errorString());
        emit lastErrorChanged();
        emit languageListDownloaded(false);
        qWarning() << m_lastError;
        return;
    }

    // Success - reset state
    m_downloading = false;
    m_downloadRetryCount = 0;
    if (!m_retryStatus.isEmpty()) {
        m_retryStatus.clear();
        emit retryStatusChanged();
    }
    emit downloadingChanged();

    QByteArray data = reply->readAll();
    QJsonDocument doc = QJsonDocument::fromJson(data);

    if (!doc.isObject()) {
        m_lastError = "Invalid language list format";
        emit lastErrorChanged();
        emit languageListDownloaded(false);
        return;
    }

    QJsonObject root = doc.object();
    QJsonArray languages = root["languages"].toArray();

    for (const QJsonValue& val : languages) {
        QJsonObject lang = val.toObject();
        QString code = lang["code"].toString();
        QString displayName = lang["name"].toString();
        QString nativeName = lang["nativeName"].toString();
        bool isRtl = lang["isRtl"].toBool(false);

        if (!code.isEmpty() && !m_languageMetadata.contains(code)) {
            m_languageMetadata[code] = QVariantMap{
                {"displayName", displayName},
                {"nativeName", nativeName.isEmpty() ? displayName : nativeName},
                {"isRtl", isRtl},
                {"isRemote", true}  // Mark as available for download
            };
        }
    }

    saveLanguageMetadata();
    m_availableLanguages = m_languageMetadata.keys();
    emit availableLanguagesChanged();
    emit languageListDownloaded(true);

    qDebug() << "Language list updated. Available:" << m_availableLanguages;
}

void TranslationManager::onLanguageFileFetched(QNetworkReply* reply)
{
    reply->deleteLater();
    QString langCode = m_downloadingLangCode;

    if (reply->error() != QNetworkReply::NoError) {
        // Check for 429 Too Many Requests - retry after delay
        int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (statusCode == 429 && m_downloadRetryCount < MAX_RETRIES) {
            m_downloadRetryCount++;
            qDebug() << "Download rate limited (429), retrying in" << (RETRY_DELAY_MS / 1000)
                     << "seconds... (attempt" << m_downloadRetryCount << "of" << MAX_RETRIES << ")";

            // Show retry status to user
            m_retryStatus = QString("Server busy, retrying download %1/%2...").arg(m_downloadRetryCount).arg(MAX_RETRIES);
            emit retryStatusChanged();

            // Schedule retry after delay (keep m_downloading true and m_downloadingLangCode set)
            QTimer::singleShot(RETRY_DELAY_MS, this, [this, langCode]() {
                QString url = QString("%1/languages/%2").arg(TRANSLATION_API_BASE, langCode);
                qDebug() << "Retrying download from:" << url;

                QNetworkRequest request{QUrl(url)};
                QNetworkReply* retryReply = m_networkManager->get(request);

                connect(retryReply, &QNetworkReply::finished, this, [this, retryReply]() {
                    onLanguageFileFetched(retryReply);
                });
            });
            return;
        }

        m_downloading = false;
        m_downloadingLangCode.clear();
        m_downloadRetryCount = 0;
        m_retryStatus.clear();
        emit retryStatusChanged();
        emit downloadingChanged();

        m_lastError = QString("Failed to download %1: %2").arg(langCode, reply->errorString());
        emit lastErrorChanged();
        emit languageDownloaded(langCode, false, m_lastError);
        qWarning() << m_lastError;
        return;
    }

    // Success - reset state
    m_downloading = false;
    m_downloadingLangCode.clear();
    m_downloadRetryCount = 0;
    if (!m_retryStatus.isEmpty()) {
        m_retryStatus.clear();
        emit retryStatusChanged();
    }
    emit downloadingChanged();

    QByteArray data = reply->readAll();
    QJsonDocument doc = QJsonDocument::fromJson(data);

    if (!doc.isObject()) {
        m_lastError = "Invalid translation file format";
        emit lastErrorChanged();
        emit languageDownloaded(langCode, false, m_lastError);
        return;
    }

    // Save the downloaded file
    QFile file(languageFilePath(langCode));
    if (file.open(QIODevice::WriteOnly)) {
        file.write(data);
        file.close();
    }

    // Update metadata
    QJsonObject root = doc.object();
    m_languageMetadata[langCode] = QVariantMap{
        {"displayName", root["displayName"].toString(langCode)},
        {"nativeName", root["nativeName"].toString(langCode)},
        {"isRtl", root["isRtl"].toBool(false)},
        {"isRemote", false}  // Now downloaded locally
    };
    saveLanguageMetadata();

    // Update available languages list (overwrites, no duplicates)
    m_availableLanguages = m_languageMetadata.keys();
    emit availableLanguagesChanged();

    // Reload if this is the current language
    if (langCode == m_currentLanguage) {
        loadTranslations();
        recalculateUntranslatedCount();
    }

    // Always increment version to refresh UI (language list colors/percentages)
    m_translationVersion++;
    emit translationsChanged();

    emit languageDownloaded(langCode, true, QString());
    qDebug() << "Downloaded language:" << langCode;
}

void TranslationManager::exportTranslation(const QString& filePath)
{
    // Allow exporting any language including English customizations
    QJsonObject root;
    root["language"] = m_currentLanguage;
    root["displayName"] = getLanguageDisplayName(m_currentLanguage);
    root["nativeName"] = getLanguageNativeName(m_currentLanguage);
    root["isRtl"] = isRtlLanguage(m_currentLanguage);

    // Simple key -> translation format
    QJsonObject translations;
    for (auto it = m_translations.constBegin(); it != m_translations.constEnd(); ++it) {
        translations[it.key()] = it.value();
    }
    root["translations"] = translations;

    QFile file(filePath);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        file.close();
        qDebug() << "Exported translation to:" << filePath;
    } else {
        m_lastError = QString("Failed to write file: %1").arg(filePath);
        emit lastErrorChanged();
    }
}

void TranslationManager::importTranslation(const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        m_lastError = QString("Failed to open file: %1").arg(filePath);
        emit lastErrorChanged();
        return;
    }

    QByteArray data = file.readAll();
    file.close();

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) {
        m_lastError = "Invalid translation file format";
        emit lastErrorChanged();
        return;
    }

    QJsonObject root = doc.object();
    QString langCode = root["language"].toString();

    if (langCode.isEmpty()) {
        m_lastError = "Translation file missing language code";
        emit lastErrorChanged();
        return;
    }

    // Save the imported file
    QFile destFile(languageFilePath(langCode));
    if (destFile.open(QIODevice::WriteOnly)) {
        destFile.write(data);
        destFile.close();
    }

    // Update metadata
    m_languageMetadata[langCode] = QVariantMap{
        {"displayName", root["displayName"].toString(langCode)},
        {"nativeName", root["nativeName"].toString(langCode)},
        {"isRtl", root["isRtl"].toBool(false)},
        {"isRemote", false}
    };
    saveLanguageMetadata();

    m_availableLanguages = m_languageMetadata.keys();
    emit availableLanguagesChanged();

    // If importing for current language, reload
    if (langCode == m_currentLanguage) {
        loadTranslations();
        recalculateUntranslatedCount();
        m_translationVersion++;
        emit translationsChanged();
    }

    qDebug() << "Imported translation for:" << langCode;
}

void TranslationManager::submitTranslation()
{
    if (m_uploading) {
        return;
    }

    // Reset retry counter for new upload
    m_uploadRetryCount = 0;

    if (m_currentLanguage == "en") {
        m_lastError = "Cannot submit English - it's the base language";
        emit lastErrorChanged();
        emit translationSubmitted(false, m_lastError);
        return;
    }

    // Build the translation JSON to upload
    QJsonObject root;
    root["language"] = m_currentLanguage;
    root["displayName"] = getLanguageDisplayName(m_currentLanguage);
    root["nativeName"] = getLanguageNativeName(m_currentLanguage);
    root["isRtl"] = isRtlLanguage(m_currentLanguage);

    // Simple key -> translation format
    QJsonObject translations;
    for (auto it = m_translations.constBegin(); it != m_translations.constEnd(); ++it) {
        translations[it.key()] = it.value();
    }
    root["translations"] = translations;

    // Store the data for upload after we get the pre-signed URL
    m_pendingUploadData = QJsonDocument(root).toJson();

    m_uploading = true;
    emit uploadingChanged();

    // Request a pre-signed URL from the backend, passing the language code
    QString uploadUrlEndpoint = QString("%1/upload-url?lang=%2").arg(TRANSLATION_API_BASE, m_currentLanguage);
    QNetworkRequest request{QUrl(uploadUrlEndpoint)};
    QNetworkReply* reply = m_networkManager->get(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onUploadUrlReceived(reply);
    });

    qDebug() << "Requesting upload URL from:" << uploadUrlEndpoint;
}

void TranslationManager::onUploadUrlReceived(QNetworkReply* reply)
{
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        // Check for 429 Too Many Requests - retry after delay
        int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (statusCode == 429 && m_uploadRetryCount < MAX_RETRIES) {
            m_uploadRetryCount++;
            qDebug() << "Upload rate limited (429), retrying in" << (RETRY_DELAY_MS / 1000)
                     << "seconds... (attempt" << m_uploadRetryCount << "of" << MAX_RETRIES << ")";

            // Show retry status to user
            m_retryStatus = QString("Server busy, retrying upload %1/%2...").arg(m_uploadRetryCount).arg(MAX_RETRIES);
            emit retryStatusChanged();

            // Schedule retry after delay
            QTimer::singleShot(RETRY_DELAY_MS, this, [this]() {
                // Re-request the upload URL
                QString uploadUrlEndpoint = QString("%1/upload-url?lang=%2")
                    .arg(TRANSLATION_API_BASE)
                    .arg(m_currentLanguage);

                QNetworkRequest request{QUrl(uploadUrlEndpoint)};
                QNetworkReply* retryReply = m_networkManager->get(request);

                connect(retryReply, &QNetworkReply::finished, this, [this, retryReply]() {
                    onUploadUrlReceived(retryReply);
                });

                qDebug() << "Retrying upload URL request...";
            });
            return;
        }

        m_uploading = false;
        m_uploadRetryCount = 0;  // Reset for next upload
        m_retryStatus.clear();
        emit retryStatusChanged();
        m_lastError = QString("Failed to get upload URL: %1").arg(reply->errorString());
        emit uploadingChanged();
        emit lastErrorChanged();
        emit translationSubmitted(false, m_lastError);
        qWarning() << m_lastError;
        return;
    }

    // Success - reset retry counter and clear status
    m_uploadRetryCount = 0;
    if (!m_retryStatus.isEmpty()) {
        m_retryStatus.clear();
        emit retryStatusChanged();
    }

    QByteArray data = reply->readAll();
    QJsonDocument doc = QJsonDocument::fromJson(data);

    if (!doc.isObject()) {
        m_uploading = false;
        m_lastError = "Invalid response from upload server";
        emit uploadingChanged();
        emit lastErrorChanged();
        emit translationSubmitted(false, m_lastError);
        return;
    }

    QJsonObject root = doc.object();
    QString uploadUrl = root["url"].toString();

    if (uploadUrl.isEmpty()) {
        m_uploading = false;
        m_lastError = "No upload URL in response";
        emit uploadingChanged();
        emit lastErrorChanged();
        emit translationSubmitted(false, m_lastError);
        return;
    }

    // Now upload the translation file to S3 using the pre-signed URL
    QNetworkRequest uploadRequest{QUrl(uploadUrl)};
    uploadRequest.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QNetworkReply* uploadReply = m_networkManager->put(uploadRequest, m_pendingUploadData);

    connect(uploadReply, &QNetworkReply::finished, this, [this, uploadReply]() {
        onTranslationUploaded(uploadReply);
    });

    qDebug() << "Uploading translation to S3...";
}

void TranslationManager::onTranslationUploaded(QNetworkReply* reply)
{
    reply->deleteLater();
    m_uploading = false;
    m_pendingUploadData.clear();
    emit uploadingChanged();

    if (reply->error() != QNetworkReply::NoError) {
        m_lastError = QString("Failed to upload translation: %1").arg(reply->errorString());
        emit lastErrorChanged();
        emit translationSubmitted(false, m_lastError);
        qWarning() << m_lastError;
        return;
    }

    QString message = QString("Translation for %1 submitted successfully! Thank you for contributing.")
                          .arg(getLanguageDisplayName(m_currentLanguage));
    emit translationSubmitted(true, message);
    qDebug() << message;
}

// --- Utility ---

QVariantList TranslationManager::getUntranslatedStrings() const
{
    QVariantList result;

    for (auto it = m_stringRegistry.constBegin(); it != m_stringRegistry.constEnd(); ++it) {
        if (!m_translations.contains(it.key()) || m_translations.value(it.key()).isEmpty()) {
            result.append(QVariantMap{
                {"key", it.key()},
                {"fallback", it.value()}
            });
        }
    }

    return result;
}

QVariantList TranslationManager::getAllStrings() const
{
    QVariantList result;

    for (auto it = m_stringRegistry.constBegin(); it != m_stringRegistry.constEnd(); ++it) {
        QString fallback = it.value();
        QString translation = m_translations.value(it.key());
        QString aiTranslation = m_aiTranslations.value(fallback);
        bool isTranslated = !translation.isEmpty();
        bool isAiGen = m_aiGenerated.contains(it.key());

        result.append(QVariantMap{
            {"key", it.key()},
            {"fallback", fallback},
            {"translation", translation},
            {"isTranslated", isTranslated},
            {"aiTranslation", aiTranslation},
            {"isAiGenerated", isAiGen}
        });
    }

    return result;
}

bool TranslationManager::isRtlLanguage(const QString& langCode) const
{
    if (m_languageMetadata.contains(langCode)) {
        return m_languageMetadata[langCode]["isRtl"].toBool();
    }

    // Common RTL languages
    static const QStringList rtlLanguages = {"ar", "he", "fa", "ur"};
    return rtlLanguages.contains(langCode);
}

bool TranslationManager::isRemoteLanguage(const QString& langCode) const
{
    if (m_languageMetadata.contains(langCode)) {
        return m_languageMetadata[langCode]["isRemote"].toBool();
    }
    return false;
}

int TranslationManager::getTranslationPercent(const QString& langCode) const
{
    if (langCode == "en") {
        return 100;  // English is always complete
    }

    // Count total strings (excluding empty fallbacks)
    int total = 0;
    for (auto it = m_stringRegistry.constBegin(); it != m_stringRegistry.constEnd(); ++it) {
        if (!it.value().trimmed().isEmpty()) {
            total++;
        }
    }
    if (total == 0) return 0;

    // For current language, use cached untranslated count
    if (langCode == m_currentLanguage) {
        int translated = total - m_untranslatedCount;
        return (translated * 100) / total;
    }

    // For other languages, read from file
    QFile file(languageFilePath(langCode));
    if (!file.open(QIODevice::ReadOnly)) {
        return 0;
    }

    QByteArray data = file.readAll();
    file.close();

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) {
        return 0;
    }

    QJsonObject root = doc.object();
    QJsonObject translations = root["translations"].toObject();

    // Count keys that have translations (excluding empty fallbacks)
    int translated = 0;
    for (auto it = m_stringRegistry.constBegin(); it != m_stringRegistry.constEnd(); ++it) {
        if (it.value().trimmed().isEmpty()) continue;  // Skip empty fallbacks
        if (translations.contains(it.key()) && !translations.value(it.key()).toString().isEmpty()) {
            translated++;
        }
    }

    return (translated * 100) / total;
}

QVariantList TranslationManager::getGroupedStrings() const
{
    // Return individual strings (one per key) - no grouping
    // This ensures the "missing" count matches the percentage calculation exactly
    QVariantList result;

    for (auto it = m_stringRegistry.constBegin(); it != m_stringRegistry.constEnd(); ++it) {
        QString key = it.key();
        QString fallback = it.value();

        // Skip empty fallbacks - they're not real translatable strings
        if (fallback.trimmed().isEmpty()) continue;

        QString translation = m_translations.value(key);
        bool isAiGen = m_aiGenerated.contains(key);

        // Look up AI translation by normalized fallback
        QString aiTranslation = m_aiTranslations.value(fallback.trimmed());
        if (aiTranslation.isEmpty()) {
            aiTranslation = m_aiTranslations.value(fallback);  // Try exact match too
        }

        result.append(QVariantMap{
            {"key", key},
            {"fallback", fallback},
            {"translation", translation},
            {"aiTranslation", aiTranslation},
            {"isTranslated", !translation.isEmpty()},
            {"isAiGenerated", isAiGen},
            // Keep these for compatibility with QML that might use them
            {"keyCount", 1},
            {"isSplit", false}
        });
    }

    return result;
}

QStringList TranslationManager::getKeysForFallback(const QString& fallback) const
{
    QStringList keys;
    QString normalizedFallback = fallback.trimmed();
    for (auto it = m_stringRegistry.constBegin(); it != m_stringRegistry.constEnd(); ++it) {
        // Use trimmed comparison for robustness against whitespace differences
        if (it.value().trimmed() == normalizedFallback) {
            keys.append(it.key());
        }
    }
    return keys;
}

void TranslationManager::setGroupTranslation(const QString& fallback, const QString& translation)
{
    QStringList keys = getKeysForFallback(fallback);
    for (const QString& key : keys) {
        if (translation.isEmpty()) {
            m_translations.remove(key);
        } else {
            m_translations[key] = translation;
        }
        m_aiGenerated.remove(key);  // User edited, no longer AI-generated
    }

    saveTranslations();
    recalculateUntranslatedCount();
    m_translationVersion++;
    emit translationsChanged();
}

bool TranslationManager::isGroupSplit(const QString& fallback) const
{
    QStringList keys = getKeysForFallback(fallback);
    if (keys.size() <= 1) return false;

    QString firstTranslation;
    bool hasFirst = false;

    for (const QString& key : keys) {
        QString translation = m_translations.value(key);
        if (!translation.isEmpty()) {
            if (!hasFirst) {
                firstTranslation = translation;
                hasFirst = true;
            } else if (translation != firstTranslation) {
                return true;  // Different translations found
            }
        }
    }

    return false;
}

void TranslationManager::mergeGroupTranslation(const QString& key)
{
    // Find the fallback for this key
    QString fallback = m_stringRegistry.value(key);
    if (fallback.isEmpty()) return;

    // Find the most common translation among keys with this fallback
    QStringList keys = getKeysForFallback(fallback);
    QMap<QString, int> translationCounts;

    for (const QString& k : keys) {
        QString translation = m_translations.value(k);
        if (!translation.isEmpty()) {
            translationCounts[translation]++;
        }
    }

    // Find the most common translation
    QString mostCommon;
    int maxCount = 0;
    for (auto it = translationCounts.constBegin(); it != translationCounts.constEnd(); ++it) {
        if (it.value() > maxCount) {
            maxCount = it.value();
            mostCommon = it.key();
        }
    }

    // Set this key to use the most common translation
    if (!mostCommon.isEmpty()) {
        m_translations[key] = mostCommon;
        saveTranslations();
        m_translationVersion++;
        emit translationsChanged();
    }
}

int TranslationManager::uniqueStringCount() const
{
    QSet<QString> uniqueFallbacks;
    for (auto it = m_stringRegistry.constBegin(); it != m_stringRegistry.constEnd(); ++it) {
        uniqueFallbacks.insert(it.value());
    }
    return uniqueFallbacks.size();
}

int TranslationManager::uniqueUntranslatedCount() const
{
    // Count unique fallback texts that have NO translation for ANY of their keys
    QMap<QString, bool> fallbackTranslated;

    for (auto it = m_stringRegistry.constBegin(); it != m_stringRegistry.constEnd(); ++it) {
        QString fallback = it.value();
        QString translation = m_translations.value(it.key());

        if (!fallbackTranslated.contains(fallback)) {
            fallbackTranslated[fallback] = !translation.isEmpty();
        } else if (!translation.isEmpty()) {
            fallbackTranslated[fallback] = true;
        }
    }

    int untranslated = 0;
    for (auto it = fallbackTranslated.constBegin(); it != fallbackTranslated.constEnd(); ++it) {
        if (!it.value()) {
            untranslated++;
        }
    }

    return untranslated;
}

// --- Private helpers ---

void TranslationManager::loadTranslations()
{
    m_translations.clear();

    // Load translations for any language (including English customizations)
    QFile file(languageFilePath(m_currentLanguage));
    if (!file.open(QIODevice::ReadOnly)) {
        qDebug() << "No translation file for:" << m_currentLanguage;
        return;
    }

    QByteArray data = file.readAll();
    file.close();

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) {
        qWarning() << "Invalid translation file for:" << m_currentLanguage;
        return;
    }

    QJsonObject root = doc.object();
    QJsonObject translations = root["translations"].toObject();

    // Simple key -> translation format
    for (auto it = translations.constBegin(); it != translations.constEnd(); ++it) {
        m_translations[it.key()] = it.value().toString();
    }

    qDebug() << "Loaded" << m_translations.size() << "translations for:" << m_currentLanguage;
}

void TranslationManager::saveTranslations()
{
    // Save translations for any language (including English customizations)
    QJsonObject root;
    root["language"] = m_currentLanguage;
    root["displayName"] = getLanguageDisplayName(m_currentLanguage);
    root["nativeName"] = getLanguageNativeName(m_currentLanguage);
    root["isRtl"] = isRtlLanguage(m_currentLanguage);

    // Simple key -> translation format
    QJsonObject translations;
    for (auto it = m_translations.constBegin(); it != m_translations.constEnd(); ++it) {
        translations[it.key()] = it.value();
    }
    root["translations"] = translations;

    QFile file(languageFilePath(m_currentLanguage));
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(root).toJson());
        file.close();
    }
}

void TranslationManager::loadLanguageMetadata()
{
    QString metaPath = translationsDir() + "/languages_meta.json";
    QFile file(metaPath);

    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }

    QByteArray data = file.readAll();
    file.close();

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) {
        return;
    }

    QJsonObject root = doc.object();
    for (auto it = root.constBegin(); it != root.constEnd(); ++it) {
        m_languageMetadata[it.key()] = it.value().toObject().toVariantMap();
    }
}

void TranslationManager::saveLanguageMetadata()
{
    QJsonObject root;
    for (auto it = m_languageMetadata.constBegin(); it != m_languageMetadata.constEnd(); ++it) {
        root[it.key()] = QJsonObject::fromVariantMap(it.value());
    }

    QString metaPath = translationsDir() + "/languages_meta.json";
    QFile file(metaPath);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(root).toJson());
        file.close();
    }
}

void TranslationManager::loadStringRegistry()
{
    QString regPath = translationsDir() + "/string_registry.json";
    QFile file(regPath);

    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }

    QByteArray data = file.readAll();
    file.close();

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) {
        return;
    }

    QJsonObject root = doc.object();
    QJsonObject strings = root["strings"].toObject();

    for (auto it = strings.constBegin(); it != strings.constEnd(); ++it) {
        QString key = it.key();
        QString fallback = it.value().toString();
        // Skip empty/whitespace keys or fallbacks
        if (key.trimmed().isEmpty() || fallback.trimmed().isEmpty()) {
            continue;
        }
        m_stringRegistry[key] = fallback;
    }
}

void TranslationManager::saveStringRegistry()
{
    QJsonObject root;
    root["version"] = "1.0";

    QJsonObject strings;
    for (auto it = m_stringRegistry.constBegin(); it != m_stringRegistry.constEnd(); ++it) {
        // Skip empty/whitespace keys or fallbacks
        if (it.key().trimmed().isEmpty() || it.value().trimmed().isEmpty()) {
            continue;
        }
        strings[it.key()] = it.value();
    }
    root["strings"] = strings;

    QString regPath = translationsDir() + "/string_registry.json";
    QFile file(regPath);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(root).toJson());
        file.close();
    }
}

void TranslationManager::propagateTranslationsToAllKeys()
{
    // For each unique fallback (normalized), find if any key has a translation
    // and propagate it to all other keys with the same fallback
    if (m_currentLanguage == "en") return;

    // Build map of normalized fallback -> first found translation
    QMap<QString, QPair<QString, bool>> fallbackToTranslation;  // normalized fallback -> (translation, isAiGenerated)

    for (auto it = m_stringRegistry.constBegin(); it != m_stringRegistry.constEnd(); ++it) {
        QString normalizedFallback = it.value().trimmed();
        if (normalizedFallback.isEmpty()) continue;  // Skip empty fallbacks
        if (!fallbackToTranslation.contains(normalizedFallback)) {
            QString translation = m_translations.value(it.key());
            if (!translation.isEmpty()) {
                bool isAiGen = m_aiGenerated.contains(it.key());
                fallbackToTranslation[normalizedFallback] = qMakePair(translation, isAiGen);
            }
        }
    }

    // Now propagate to all keys that don't have translations
    int propagated = 0;
    for (auto it = m_stringRegistry.constBegin(); it != m_stringRegistry.constEnd(); ++it) {
        QString normalizedFallback = it.value().trimmed();
        if (normalizedFallback.isEmpty()) continue;  // Skip empty fallbacks
        if (m_translations.value(it.key()).isEmpty()) {
            if (fallbackToTranslation.contains(normalizedFallback)) {
                auto& pair = fallbackToTranslation[normalizedFallback];
                m_translations[it.key()] = pair.first;
                if (pair.second) {
                    m_aiGenerated.insert(it.key());
                }
                propagated++;
            }
        }
    }

    if (propagated > 0) {
        qDebug() << "TranslationManager: Propagated translations to" << propagated << "keys";
        saveTranslations();  // Save propagated translations to file
    }
}

void TranslationManager::recalculateUntranslatedCount()
{
    // First, propagate any existing translations to keys that are missing them
    // This handles keys that were registered after AI translation ran
    propagateTranslationsToAllKeys();

    // For English: count uncustomized strings
    // For other languages: count untranslated strings
    int count = 0;
    for (auto it = m_stringRegistry.constBegin(); it != m_stringRegistry.constEnd(); ++it) {
        // Skip empty fallbacks - they're not real translatable strings
        if (it.value().trimmed().isEmpty()) continue;

        if (!m_translations.contains(it.key()) || m_translations.value(it.key()).isEmpty()) {
            count++;
        }
    }
    m_untranslatedCount = count;
    emit untranslatedCountChanged();
}

// --- AI Auto-Translation ---

bool TranslationManager::canAutoTranslate() const
{
    if (m_currentLanguage == "en") return false;
    if (m_autoTranslating) return false;

    QString provider = m_settings->aiProvider();
    if (provider == "openai" && !m_settings->openaiApiKey().isEmpty()) return true;
    if (provider == "anthropic" && !m_settings->anthropicApiKey().isEmpty()) return true;
    if (provider == "gemini" && !m_settings->geminiApiKey().isEmpty()) return true;
    if (provider == "ollama" && !m_settings->ollamaEndpoint().isEmpty() && !m_settings->ollamaModel().isEmpty()) return true;

    return false;
}

void TranslationManager::autoTranslate()
{
    if (!canAutoTranslate()) {
        m_lastError = "AI provider not configured. Set up an AI provider in Settings.";
        emit lastErrorChanged();
        emit autoTranslateFinished(false, m_lastError);
        return;
    }

    // Get unique untranslated fallback texts (more efficient - translate once, apply to all keys)
    // Use trimmed fallbacks for comparison to handle whitespace variations
    QSet<QString> seenFallbacks;
    m_stringsToTranslate.clear();

    for (auto it = m_stringRegistry.constBegin(); it != m_stringRegistry.constEnd(); ++it) {
        QString fallback = it.value();
        QString normalizedFallback = fallback.trimmed();

        // Skip if already translated (check if ANY key with this fallback is translated)
        // Use trimmed comparison for robustness
        bool hasTranslation = false;
        for (auto keyIt = m_stringRegistry.constBegin(); keyIt != m_stringRegistry.constEnd(); ++keyIt) {
            if (keyIt.value().trimmed() == normalizedFallback && !m_translations.value(keyIt.key()).isEmpty()) {
                hasTranslation = true;
                break;
            }
        }

        if (!hasTranslation && !seenFallbacks.contains(normalizedFallback)) {
            seenFallbacks.insert(normalizedFallback);
            // Use normalized fallback to avoid whitespace issues with AI
            m_stringsToTranslate.append(QVariantMap{
                {"key", normalizedFallback},  // Use normalized fallback as key for grouped translation
                {"fallback", normalizedFallback}
            });
        }
    }

    if (m_stringsToTranslate.isEmpty()) {
        emit autoTranslateFinished(true, "All strings are already translated!");
        return;
    }

    m_translationRunId++;  // New run - stale responses from previous run will be ignored
    m_autoTranslating = true;
    m_autoTranslateCancelled = false;
    m_autoTranslateProgress = 0;
    m_autoTranslateTotal = m_stringsToTranslate.size();
    m_pendingBatchCount = 0;
    emit autoTranslatingChanged();
    emit autoTranslateProgressChanged();

    QString provider = getActiveProvider();
    qDebug() << "=== AUTO-TRANSLATE START (run" << m_translationRunId << ") ===";
    qDebug() << "Language:" << m_currentLanguage;
    qDebug() << "Provider:" << provider << (m_batchProcessing ? "(batch mode)" : "(single mode)");
    qDebug() << "Registry total:" << m_stringRegistry.size() << "keys";
    qDebug() << "Translations loaded:" << m_translations.size();
    qDebug() << "AI cache loaded:" << m_aiTranslations.size();
    qDebug() << "Unique fallbacks:" << uniqueStringCount();
    qDebug() << "Unique untranslated:" << uniqueUntranslatedCount();
    qDebug() << "Strings to translate:" << m_autoTranslateTotal;

    // Fire all batches in parallel for faster translation
    while (!m_stringsToTranslate.isEmpty() && !m_autoTranslateCancelled) {
        sendNextAutoTranslateBatch();
    }

    qDebug() << "Fired" << m_pendingBatchCount << "parallel batch requests";
}

void TranslationManager::cancelAutoTranslate()
{
    if (m_autoTranslating) {
        m_autoTranslateCancelled = true;
        m_autoTranslating = false;
        emit autoTranslatingChanged();
        emit autoTranslateFinished(false, "Translation cancelled");
    }
}

void TranslationManager::sendNextAutoTranslateBatch()
{
    if (m_autoTranslateCancelled || m_stringsToTranslate.isEmpty()) {
        return;
    }

    // Get next batch
    QVariantList batch;
    int batchSize = static_cast<int>(qMin(static_cast<qsizetype>(AUTO_TRANSLATE_BATCH_SIZE), m_stringsToTranslate.size()));
    for (int i = 0; i < batchSize; i++) {
        batch.append(m_stringsToTranslate.takeFirst());
    }

    QString prompt = buildTranslationPrompt(batch);
    QString provider = getActiveProvider();

    qDebug() << "TranslationManager: Sending batch of" << batch.size() << "strings to" << provider
             << "for language" << m_currentLanguage;

    QNetworkRequest request;
    QByteArray postData;

    if (provider == "openai") {
        request.setUrl(QUrl("https://api.openai.com/v1/chat/completions"));
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        request.setRawHeader("Authorization", ("Bearer " + m_settings->openaiApiKey()).toUtf8());

        QJsonObject json;
        json["model"] = "gpt-4o-mini";  // Use mini for translation - cheaper and fast
        json["temperature"] = 0.3;
        QJsonArray messages;
        QJsonObject msg;
        msg["role"] = "user";
        msg["content"] = prompt;
        messages.append(msg);
        json["messages"] = messages;
        postData = QJsonDocument(json).toJson();

    } else if (provider == "anthropic") {
        request.setUrl(QUrl("https://api.anthropic.com/v1/messages"));
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        request.setRawHeader("x-api-key", m_settings->anthropicApiKey().toUtf8());
        request.setRawHeader("anthropic-version", "2023-06-01");

        QJsonObject json;
        json["model"] = "claude-3-5-haiku-20241022";  // Use haiku for translation - cheaper and fast
        json["max_tokens"] = 4096;
        QJsonArray messages;
        QJsonObject msg;
        msg["role"] = "user";
        msg["content"] = prompt;
        messages.append(msg);
        json["messages"] = messages;
        postData = QJsonDocument(json).toJson();

    } else if (provider == "gemini") {
        QString apiKey = m_settings->geminiApiKey();
        request.setUrl(QUrl("https://generativelanguage.googleapis.com/v1beta/models/gemini-2.0-flash:generateContent"));
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        request.setRawHeader("x-goog-api-key", apiKey.toUtf8());

        QJsonObject json;
        QJsonArray contents;
        QJsonObject content;
        QJsonArray parts;
        QJsonObject part;
        part["text"] = prompt;
        parts.append(part);
        content["parts"] = parts;
        contents.append(content);
        json["contents"] = contents;
        postData = QJsonDocument(json).toJson();

    } else if (provider == "ollama") {
        QString endpoint = m_settings->ollamaEndpoint();
        if (!endpoint.endsWith("/")) endpoint += "/";
        request.setUrl(QUrl(endpoint + "api/generate"));
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

        QJsonObject json;
        json["model"] = m_settings->ollamaModel();
        json["prompt"] = prompt;
        json["stream"] = false;
        postData = QJsonDocument(json).toJson();
    }

    m_pendingBatchCount++;
    int runId = m_translationRunId;  // Capture current run ID
    QNetworkReply* reply = m_networkManager->post(request, postData);
    connect(reply, &QNetworkReply::finished, this, [this, reply, runId]() {
        // Check if this response belongs to the current run
        if (runId != m_translationRunId) {
            qDebug() << "TranslationManager: Stale response from run" << runId
                     << "(current run:" << m_translationRunId << ") - ignoring";
            reply->deleteLater();
            return;
        }
        onAutoTranslateBatchReply(reply);
    });
}

QString TranslationManager::buildTranslationPrompt(const QVariantList& strings) const
{
    QString langName = getLanguageDisplayName(m_currentLanguage);
    QString nativeName = getLanguageNativeName(m_currentLanguage);

    QString prompt = QString(
        "Translate the following English strings to %1 (%2).\n"
        "Return ONLY a JSON object with the translations, no explanation.\n"
        "The format must be exactly: {\"key\": \"translated text\", ...}\n"
        "Keep formatting like %1, %2, \\n exactly as-is.\n"
        "Be natural and idiomatic in %1.\n\n"
        "Strings to translate:\n"
    ).arg(langName, nativeName);

    for (const QVariant& v : strings) {
        QVariantMap item = v.toMap();
        QString key = item["key"].toString();
        QString fallback = item["fallback"].toString();
        prompt += QString("\"%1\": \"%2\"\n").arg(key, fallback.replace("\"", "\\\""));
    }

    return prompt;
}

void TranslationManager::onAutoTranslateBatchReply(QNetworkReply* reply)
{
    reply->deleteLater();
    m_pendingBatchCount--;

    QString provider = getActiveProvider();
    int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    qDebug() << "TranslationManager: Response from" << provider
             << "HTTP:" << httpStatus
             << "pending:" << m_pendingBatchCount
             << "run:" << m_translationRunId;

    // If cancelled mid-run, ignore content but still count down
    if (m_autoTranslateCancelled) {
        qDebug() << "TranslationManager: Response ignored (cancelled), waiting for" << m_pendingBatchCount << "more";
        // Wait for ALL batches to complete before signaling done
        if (m_pendingBatchCount == 0) {
            qDebug() << "TranslationManager: All batches drained after cancellation";
            m_autoTranslating = false;
            emit autoTranslatingChanged();
            emit autoTranslateFinished(false, m_lastError);
        }
        return;
    }

    if (reply->error() != QNetworkReply::NoError) {
        // Set cancelled flag but DON'T emit autoTranslateFinished yet
        // Wait for all in-flight responses to complete first
        m_autoTranslateCancelled = true;
        m_lastError = QString("AI request failed (%1): %2").arg(provider, reply->errorString());
        qWarning() << "TranslationManager:" << m_lastError;
        qWarning() << "Response body:" << reply->readAll().left(500);
        emit lastErrorChanged();

        // If this was the last batch, we can finish now
        if (m_pendingBatchCount == 0) {
            qDebug() << "TranslationManager: Error on last batch, finishing";
            m_autoTranslating = false;
            emit autoTranslatingChanged();
            emit autoTranslateFinished(false, m_lastError);
        } else {
            qDebug() << "TranslationManager: Error occurred, waiting for" << m_pendingBatchCount << "batches to drain";
        }
        return;
    }

    QByteArray data = reply->readAll();
    parseAutoTranslateResponse(data);

    // Check if all batches are complete
    if (m_pendingBatchCount == 0) {
        qDebug() << "TranslationManager: All batches complete for" << m_currentLanguage;
        m_autoTranslating = false;
        emit autoTranslatingChanged();
        saveTranslations();
        saveAiTranslations();
        recalculateUntranslatedCount();
        m_translationVersion++;
        emit translationsChanged();
        emit autoTranslateFinished(true, QString("Translated %1 strings").arg(m_autoTranslateProgress));
    }
}

void TranslationManager::parseAutoTranslateResponse(const QByteArray& data)
{
    QString provider = getActiveProvider();
    QString content;

    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonObject root = doc.object();

    if (provider == "openai") {
        QJsonArray choices = root["choices"].toArray();
        if (!choices.isEmpty()) {
            content = choices[0].toObject()["message"].toObject()["content"].toString();
        }
    } else if (provider == "anthropic") {
        QJsonArray contentArr = root["content"].toArray();
        if (!contentArr.isEmpty()) {
            content = contentArr[0].toObject()["text"].toString();
        }
    } else if (provider == "gemini") {
        QJsonArray candidates = root["candidates"].toArray();
        if (!candidates.isEmpty()) {
            QJsonArray parts = candidates[0].toObject()["content"].toObject()["parts"].toArray();
            if (!parts.isEmpty()) {
                content = parts[0].toObject()["text"].toString();
            }
        }
    } else if (provider == "ollama") {
        content = root["response"].toString();
    }

    if (content.isEmpty()) {
        qWarning() << "Empty AI response for provider:" << provider;
        return;
    }

    // Extract JSON from response (AI might include markdown code blocks)
    int jsonStart = content.indexOf('{');
    int jsonEnd = content.lastIndexOf('}');
    if (jsonStart >= 0 && jsonEnd > jsonStart) {
        content = content.mid(jsonStart, jsonEnd - jsonStart + 1);
    }

    // Parse translations and apply directly to empty keys
    // Note: The "key" in the response is actually the fallback text (since we translate unique texts)
    QJsonDocument transDoc = QJsonDocument::fromJson(content.toUtf8());
    if (transDoc.isObject()) {
        QJsonObject translations = transDoc.object();
        int count = 0;
        int appliedCount = 0;
        for (auto it = translations.constBegin(); it != translations.constEnd(); ++it) {
            QString fallbackText = it.key();
            QString translation = it.value().toString().trimmed();
            if (!translation.isEmpty()) {
                // Store in AI translations (for display in AI column)
                m_aiTranslations[fallbackText] = translation;

                // Apply to ALL keys with this fallback text that don't have a translation yet
                // getKeysForFallback uses trimmed comparison for robustness
                QStringList keys = getKeysForFallback(fallbackText);
                if (keys.isEmpty()) {
                    qDebug() << "TranslationManager: No keys found for fallback:" << fallbackText.left(50);
                }

                for (const QString& key : keys) {
                    if (m_translations.value(key).isEmpty()) {
                        m_translations[key] = translation;
                        m_aiGenerated.insert(key);  // Mark as AI-generated
                        appliedCount++;
                    }
                }

                // Update last translated text for UI feedback
                m_lastTranslatedText = fallbackText + " → " + translation;
                emit lastTranslatedTextChanged();

                count++;
            }
        }
        // Track actual translations applied, not just AI responses
        m_autoTranslateProgress += appliedCount;
        emit autoTranslateProgressChanged();

        qDebug() << "AI translated" << count << "unique texts," << appliedCount << "keys applied, progress:" << m_autoTranslateProgress << "/" << m_autoTranslateTotal;
    } else {
        qWarning() << "Failed to parse AI translation response:" << content.left(200);
    }
}

// --- AI Translation Management ---

QString TranslationManager::getAiTranslation(const QString& fallback) const
{
    return m_aiTranslations.value(fallback);
}

bool TranslationManager::isAiGenerated(const QString& key) const
{
    return m_aiGenerated.contains(key);
}

void TranslationManager::copyAiToFinal(const QString& fallback)
{
    QString aiTranslation = m_aiTranslations.value(fallback);
    if (aiTranslation.isEmpty()) return;

    QStringList keys = getKeysForFallback(fallback);
    for (const QString& key : keys) {
        m_translations[key] = aiTranslation;
        m_aiGenerated.insert(key);  // Mark as AI-generated
    }

    saveTranslations();
    recalculateUntranslatedCount();
    m_translationVersion++;
    emit translationsChanged();
}

void TranslationManager::clearAiTranslation(const QString& fallback)
{
    if (!m_aiTranslations.contains(fallback)) return;

    m_aiTranslations.remove(fallback);

    // Also clear the AI-generated flag for all keys using this fallback
    QStringList keys = getKeysForFallback(fallback);
    for (const QString& key : keys) {
        m_aiGenerated.remove(key);
    }

    saveAiTranslations();
    m_translationVersion++;
    emit translationsChanged();
}

void TranslationManager::clearAllAiTranslations()
{
    if (m_currentLanguage == "en") return;

    int aiCacheCount = m_aiTranslations.size();
    int clearedFromMain = 0;
    int preservedUserEdits = 0;

    // Clear AI-generated translations from main translations map
    // but preserve user overrides (human edits)
    QStringList keysToRemove;
    for (auto it = m_translations.constBegin(); it != m_translations.constEnd(); ++it) {
        if (m_aiGenerated.contains(it.key()) && !m_userOverrides.contains(it.key())) {
            keysToRemove.append(it.key());
        } else if (m_userOverrides.contains(it.key())) {
            preservedUserEdits++;
        }
    }

    for (const QString& key : keysToRemove) {
        m_translations.remove(key);
        clearedFromMain++;
    }

    // Clear AI caches
    m_aiTranslations.clear();
    m_aiGenerated.clear();

    // Delete the AI translations file
    QString aiPath = translationsDir() + "/" + m_currentLanguage + "_ai.json";
    QFile::remove(aiPath);

    // Save updated translations
    saveTranslations();
    recalculateUntranslatedCount();

    qDebug() << "Cleared AI translations for" << m_currentLanguage
             << "- AI cache:" << aiCacheCount
             << "- Removed from main:" << clearedFromMain
             << "- Preserved user edits:" << preservedUserEdits;

    m_translationVersion++;
    emit translationsChanged();
}

void TranslationManager::loadAiTranslations()
{
    m_aiTranslations.clear();
    m_aiGenerated.clear();

    if (m_currentLanguage == "en") {
        return;
    }

    QString aiPath = translationsDir() + "/" + m_currentLanguage + "_ai.json";
    QFile file(aiPath);
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }

    QByteArray data = file.readAll();
    file.close();

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) {
        return;
    }

    QJsonObject root = doc.object();

    // Load AI translations (fallback -> translation)
    QJsonObject translations = root["translations"].toObject();
    for (auto it = translations.constBegin(); it != translations.constEnd(); ++it) {
        m_aiTranslations[it.key()] = it.value().toString();
    }

    // Load AI-generated flags (list of keys)
    QJsonArray generated = root["generated"].toArray();
    for (const QJsonValue& val : generated) {
        m_aiGenerated.insert(val.toString());
    }

    qDebug() << "Loaded" << m_aiTranslations.size() << "AI translations for:" << m_currentLanguage;
}

void TranslationManager::saveAiTranslations()
{
    if (m_currentLanguage == "en") {
        return;
    }

    QString aiPath = translationsDir() + "/" + m_currentLanguage + "_ai.json";

    if (m_aiTranslations.isEmpty()) {
        QFile::remove(aiPath);
        return;
    }

    QJsonObject root;
    root["language"] = m_currentLanguage;

    // Save AI translations
    QJsonObject translations;
    for (auto it = m_aiTranslations.constBegin(); it != m_aiTranslations.constEnd(); ++it) {
        translations[it.key()] = it.value();
    }
    root["translations"] = translations;

    // Save AI-generated flags
    QJsonArray generated;
    for (const QString& key : m_aiGenerated) {
        generated.append(key);
    }
    root["generated"] = generated;

    QFile file(aiPath);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(root).toJson());
        file.close();
    }
}

// --- User Overrides (preserved during language updates) ---

void TranslationManager::loadUserOverrides()
{
    m_userOverrides.clear();

    if (m_currentLanguage == "en") {
        return;  // English has no remote updates
    }

    QString overridesPath = translationsDir() + "/" + m_currentLanguage + "_overrides.json";
    QFile file(overridesPath);
    if (!file.open(QIODevice::ReadOnly)) {
        return;  // No overrides file yet
    }

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();

    if (!doc.isObject()) {
        return;
    }

    QJsonArray overrides = doc.object()["overrides"].toArray();
    for (const QJsonValue& val : overrides) {
        m_userOverrides.insert(val.toString());
    }

    qDebug() << "Loaded" << m_userOverrides.size() << "user overrides for:" << m_currentLanguage;
}

void TranslationManager::saveUserOverrides()
{
    if (m_currentLanguage == "en") {
        return;
    }

    QString overridesPath = translationsDir() + "/" + m_currentLanguage + "_overrides.json";

    if (m_userOverrides.isEmpty()) {
        QFile::remove(overridesPath);
        return;
    }

    QJsonObject root;
    QJsonArray overrides;
    for (const QString& key : m_userOverrides) {
        overrides.append(key);
    }
    root["overrides"] = overrides;

    QFile file(overridesPath);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(root).toJson());
        file.close();
    }
}

void TranslationManager::checkForLanguageUpdate()
{
    // Only check for non-English languages that were downloaded from the server
    if (m_currentLanguage == "en") {
        return;
    }

    // Check if this language was downloaded (not locally created)
    if (!m_languageMetadata.contains(m_currentLanguage)) {
        return;  // Language not in metadata
    }
    QVariantMap metadata = m_languageMetadata.value(m_currentLanguage);

    // If it's marked as remote (not yet downloaded), don't auto-update
    if (metadata.value("isRemote", false).toBool()) {
        return;  // User hasn't downloaded this language yet
    }

    // Check if translation file exists locally (indicates it was downloaded at some point)
    QFile localFile(languageFilePath(m_currentLanguage));
    if (!localFile.exists()) {
        return;  // No local file to update
    }

    qDebug() << "Checking for language update:" << m_currentLanguage;

    // Fetch the latest version from server
    QString url = QString("%1/languages/%2").arg(TRANSLATION_API_BASE, m_currentLanguage);
    QNetworkRequest request{QUrl(url)};
    QNetworkReply* reply = m_networkManager->get(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            qDebug() << "Language update check failed:" << reply->errorString();
            return;
        }

        QByteArray data = reply->readAll();
        QJsonDocument doc = QJsonDocument::fromJson(data);

        if (!doc.isObject()) {
            qDebug() << "Invalid language update response";
            return;
        }

        QJsonObject root = doc.object();
        QJsonObject newTranslations = root["translations"].toObject();

        if (newTranslations.isEmpty()) {
            return;
        }

        // Merge new translations, preserving user overrides
        mergeLanguageUpdate(newTranslations);
    });
}

void TranslationManager::mergeLanguageUpdate(const QJsonObject& newTranslations)
{
    int added = 0;
    int updated = 0;
    int preserved = 0;

    for (auto it = newTranslations.constBegin(); it != newTranslations.constEnd(); ++it) {
        const QString& key = it.key();
        const QString& newValue = it.value().toString();

        // Skip if user has customized this translation
        if (m_userOverrides.contains(key)) {
            preserved++;
            continue;
        }

        if (!m_translations.contains(key)) {
            // New translation
            m_translations[key] = newValue;
            added++;
        } else if (m_translations[key] != newValue) {
            // Updated translation
            m_translations[key] = newValue;
            updated++;
        }
    }

    if (added > 0 || updated > 0) {
        qDebug() << "Language update merged:" << added << "new," << updated << "updated," << preserved << "preserved user overrides";
        saveTranslations();
        recalculateUntranslatedCount();
        m_translationVersion++;
        emit translationsChanged();
    } else {
        qDebug() << "Language is up to date";
    }
}

// --- Batch Translate and Upload All Languages ---

QStringList TranslationManager::getConfiguredProviders() const
{
    // Order: Claude first (best quality), then OpenAI
    // Gemini excluded due to aggressive rate limiting
    // Each provider fills in gaps left by previous ones
    QStringList providers;
    if (!m_settings->anthropicApiKey().isEmpty()) providers << "anthropic";
    if (!m_settings->openaiApiKey().isEmpty()) providers << "openai";
    // if (!m_settings->geminiApiKey().isEmpty()) providers << "gemini";
    // if (!m_settings->ollamaEndpoint().isEmpty() && !m_settings->ollamaModel().isEmpty()) providers << "ollama";
    return providers;
}

QString TranslationManager::getActiveProvider() const
{
    // During batch processing, use the override provider (bypasses QSettings cache)
    if (m_batchProcessing && !m_batchCurrentProvider.isEmpty()) {
        return m_batchCurrentProvider;
    }
    // Otherwise use the normal settings value
    return m_settings->aiProvider();
}

void TranslationManager::translateAndUploadAllLanguages()
{
    if (m_batchProcessing || m_autoTranslating || m_uploading) {
        qDebug() << "Batch processing already in progress";
        return;
    }

    // Get all configured providers - we'll cycle through them
    m_batchProviderQueue = getConfiguredProviders();
    if (m_batchProviderQueue.isEmpty()) {
        m_lastError = "No AI providers configured. Set up at least one AI provider in Settings.";
        emit lastErrorChanged();
        emit batchTranslateUploadFinished(false, m_lastError);
        return;
    }

    // Save original provider to restore later
    m_originalProvider = m_settings->aiProvider();

    // Ensure all strings are scanned first
    if (!m_scanning) {
        scanAllStrings();
    }

    // Build list of all local (non-remote, non-English) languages
    QStringList allLanguages;
    for (const QString& langCode : m_availableLanguages) {
        if (langCode != "en" && !isRemoteLanguage(langCode)) {
            allLanguages.append(langCode);
        }
    }

    if (allLanguages.isEmpty()) {
        emit batchTranslateUploadFinished(true, "No local languages to process");
        return;
    }

    m_batchProcessing = true;
    qDebug() << "=== BATCH TRANSLATE+UPLOAD START ===";
    qDebug() << "Languages:" << allLanguages.size() << allLanguages;
    qDebug() << "AI Providers:" << m_batchProviderQueue.size() << m_batchProviderQueue;

    // Start with first provider, queue all languages for it
    QString firstProvider = m_batchProviderQueue.takeFirst();
    m_batchCurrentProvider = firstProvider;  // Bypass QSettings cache
    m_settings->setAiProvider(firstProvider);  // Still set for UI consistency
    m_batchLanguageQueue = allLanguages;

    qDebug() << "Batch: Starting with provider:" << firstProvider << "(m_batchCurrentProvider set)";

    // Set up connections for the batch process flow
    QMetaObject::Connection* autoConn = new QMetaObject::Connection();
    QMetaObject::Connection* submitConn = new QMetaObject::Connection();

    // Lambda to process next language (using shared_ptr to allow recursion and survive async calls)
    auto processNext = std::make_shared<std::function<void()>>();
    *processNext = [this, autoConn, submitConn, processNext]() {
        if (!m_batchProcessing) return;

        if (!m_batchLanguageQueue.isEmpty()) {
            // More languages to process - reset provider queue for new language
            m_batchProviderQueue = getConfiguredProviders();
            if (!m_batchProviderQueue.isEmpty()) {
                m_batchCurrentProvider = m_batchProviderQueue.takeFirst();
                m_settings->setAiProvider(m_batchCurrentProvider);
            }
            QString nextLang = m_batchLanguageQueue.takeFirst();
            qDebug() << "Batch: Processing language:" << nextLang << "with provider:" << m_batchCurrentProvider;
            setCurrentLanguage(nextLang);

            // Check if translation is needed or just upload
            int untranslated = uniqueUntranslatedCount();
            qDebug() << "Batch: Language status -"
                     << "Registry:" << m_stringRegistry.size()
                     << "Translations:" << m_translations.size()
                     << "Unique untranslated:" << untranslated;
            if (m_translations.size() < m_stringRegistry.size()) {
                qDebug() << "****************** MISSING TRANSLATIONS:" << (m_stringRegistry.size() - m_translations.size()) << "******************";
            }
            if (untranslated == 0) {
                qDebug() << "Batch:" << nextLang << "is fully translated, skipping (no changes needed)";
                (*processNext)();
            } else {
                qDebug() << "Batch:" << nextLang << "has" << untranslated << "untranslated strings, translating...";
                autoTranslate();
            }
        } else {
            // All done - restore original provider and clear batch state
            m_batchCurrentProvider.clear();
            m_settings->setAiProvider(m_originalProvider);
            m_batchProcessing = false;
            disconnect(*autoConn);
            disconnect(*submitConn);
            delete autoConn;
            delete submitConn;
            qDebug() << "=== BATCH TRANSLATE+UPLOAD COMPLETE ===";
            qDebug() << "Restored provider:" << m_originalProvider;
            emit batchTranslateUploadFinished(true, "Batch processing complete");
        }
    };

    *autoConn = connect(this, &TranslationManager::autoTranslateFinished, this, [this, processNext](bool success, const QString& message) {
        if (!m_batchProcessing) return;

        qDebug() << "Batch: autoTranslateFinished for" << m_currentLanguage
                 << "success:" << success << "message:" << message
                 << "provider:" << m_batchCurrentProvider;

        if (success) {
            // Check if there were actual translations made (not "all already translated")
            if (message.contains("already translated")) {
                qDebug() << "Batch: Skipping upload for" << m_currentLanguage << "(no changes needed)";
                (*processNext)();
            } else {
                // Translation done with changes, now upload
                qDebug() << "Batch: Uploading" << m_currentLanguage << "...";
                submitTranslation();
            }
        } else {
            // Translation failed - check if we can try another provider
            if (!m_batchProviderQueue.isEmpty()) {
                // Try next provider for the SAME language
                QString nextProvider = m_batchProviderQueue.takeFirst();
                m_batchCurrentProvider = nextProvider;
                m_settings->setAiProvider(nextProvider);
                qDebug() << "Batch: Rate limited/error, trying provider:" << nextProvider << "for" << m_currentLanguage;
                autoTranslate();
            } else {
                // All providers exhausted for this language, move to next
                // (*processNext)() will reset the provider queue
                qDebug() << "Batch: All providers exhausted for" << m_currentLanguage << ", moving to next language";
                (*processNext)();
            }
        }
    });

    *submitConn = connect(this, &TranslationManager::translationSubmitted, this, [this, processNext](bool success, const QString& message) {
        if (!m_batchProcessing) return;

        qDebug() << "Batch: Upload" << (success ? "SUCCEEDED" : "FAILED")
                 << "for" << m_currentLanguage << "-" << message;
        (*processNext)();
    });

    // Start with first language
    QString firstLang = m_batchLanguageQueue.takeFirst();
    qDebug() << "Batch: Starting with language:" << firstLang;
    setCurrentLanguage(firstLang);

    // Check if translation is needed or just upload
    int untranslated = uniqueUntranslatedCount();
    qDebug() << "Batch: Language status -"
             << "Registry:" << m_stringRegistry.size()
             << "Translations:" << m_translations.size()
             << "Unique untranslated:" << untranslated;
    if (m_translations.size() < m_stringRegistry.size()) {
        qDebug() << "****************** MISSING TRANSLATIONS:" << (m_stringRegistry.size() - m_translations.size()) << "******************";
    }
    if (untranslated == 0) {
        qDebug() << "Batch:" << firstLang << "is fully translated, skipping (no changes needed)";
        (*processNext)();
    } else {
        qDebug() << "Batch:" << firstLang << "has" << untranslated << "untranslated strings, translating...";
        autoTranslate();
    }
}
