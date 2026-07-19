#include <QJSEngine>
#include "translationmanager.h"
#include "settings.h"
#include "settings_ai.h"
#include <QStandardPaths>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>
#include <QDesktopServices>
#include <QUrl>
#include <QUrlQuery>
#include <QTimer>
#include <QNetworkInformation>
#include <QSet>
#include <QRegularExpression>
#include <QCoreApplication>
#include <functional>
#include <memory>

TranslationManager::TranslationManager(QNetworkAccessManager* networkManager, Settings* settings, QObject* parent)
    : QObject(parent)
    , m_settings(settings)
    , m_networkManager(networkManager)
{
    Q_ASSERT(networkManager);
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
        // Tolerable discard: the English entry is re-seeded on every launch, so a failed write
        // here costs nothing that the next start does not restore. The helper has warned.
        (void)saveLanguageMetadata();
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
        // Tolerable discard: this cleanup re-runs on every launch, so a failed write only
        // defers it. The helper has warned.
        (void)saveStringRegistry();
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
            // Tolerable discard: the registry is rebuilt by rendering — every string
            // re-registers the next time it is drawn — so a lost batch is rediscovered, not
            // gone. Clearing the dirty flag regardless is deliberate: retrying a broken disk
            // every 5 seconds would toast the user at the same cadence.
            (void)saveStringRegistry();
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

    scheduleLanguageUpdateCheck();
}

// Merge community translations for the active language, once per launch, as soon as there is
// a network to do it over.
//
// This was `QTimer::singleShot(3000, ...)` — a fixed delay standing in for "wait until the
// network is up", which is the timer-as-a-guard pattern the project rules out: three seconds
// is too long on a warm desktop and too short on a tablet still associating with Wi-Fi, and
// when it is too short the check simply fails and nothing retries until the next launch.
// QNetworkInformation already reports reachability — main.cpp loads the backend and logs it —
// so the condition can be waited on directly.
void TranslationManager::scheduleLanguageUpdateCheck()
{
    auto* info = QNetworkInformation::instance();
    if (!info) {
        // No backend on this platform, or the manager was constructed before main.cpp loaded
        // one. Ask immediately; being offline just fails the request, which is already handled.
        checkForLanguageUpdate();
        return;
    }

    // Wait ONLY when the backend positively says there is no network. Everything else —
    // including Unknown — attempts the request.
    //
    // Unknown is not a synonym for offline: it is what the backend reports when it cannot
    // tell, and macOS reports exactly that at startup on the machine this was tested on, with
    // no reachabilityChanged ever following. A first version of this waited for Online and so
    // would have sat there forever, never running a check the old timer always ran. Local and
    // Site are treated the same way: the API host is on the internet so they will probably
    // fail, but attempting and failing is what the timer did, and failure is already handled.
    if (info->reachability() != QNetworkInformation::Reachability::Disconnected) {
        checkForLanguageUpdate();
        return;
    }

    // Genuinely disconnected. Wait for that to stop being true rather than guessing at a
    // delay — this also covers the case the old timer could not: a device that only gets a
    // network minutes in now picks up translations without needing a restart.
    connect(info, &QNetworkInformation::reachabilityChanged, this,
            [this](QNetworkInformation::Reachability reachability) {
        if (reachability != QNetworkInformation::Reachability::Disconnected)
            checkForLanguageUpdate();
    });
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
    return static_cast<int>(m_stringRegistry.size());
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

void TranslationManager::setJsEngine(QJSEngine* engine)
{
    if (m_jsEngine && m_jsEngine != engine) {
        // Bindings that already read `translate` hold a callable bound to the PREVIOUS
        // engine. They keep calling into it, and if that engine is destroyed the value is
        // invalid — materially worse than undefined. There is no migration path, so fail
        // loudly at the wiring mistake rather than mysteriously later.
        qFatal("[i18n] setJsEngine() called twice with different engines — 3,248 bindings "
               "hold a callable from the first one and cannot be migrated.");
    }

    m_jsEngine = engine;
    m_translateFn = QJSValue();  // rebuild against this engine on next read

    if (!engine)
        return;

    // Build eagerly so a wiring fault surfaces here, at the call site that got it wrong,
    // instead of 3,248 times over from the property getter.
    (void)translateFn();

    // The property name below is a STRING coupled to a C++ method with no compile-time
    // link: an IDE rename of translateString() compiles clean, yields undefined, and turns
    // every translated string in the app into a TypeError. tst_translationreactivity's
    // indexOfMethod check catches removal, not a consistent rename.
    Q_ASSERT_X(m_translateFn.isCallable(), "TranslationManager",
               "translateString not found on the QJSEngine wrapper — was it renamed?");

    // Anything that already evaluated (before wiring, or against the old engine) is stale
    // and will never re-run on its own, because nothing else fires for this transition.
    emit translationsChanged();
}

QJSValue TranslationManager::translateFn()
{
    if (!m_jsEngine) {
        // Nobody called setJsEngine(). Every translated binding in the app evaluates to
        // undefined — a startup wiring fault, not a content problem.
        //
        // Warn ONCE. This is a Q_PROPERTY read accessor behind ~3,248 bindings; warning per
        // read would emit thousands of identical lines on startup and again on every
        // translationsChanged, burying the one message that explains the blank UI. Debug logs
        // here are read by users' AI assistants via MCP, so a flooded log is actively harmful.
        if (!m_warnedNoEngine) {
            m_warnedNoEngine = true;
            qCritical() << "[i18n] TranslationManager has no QJSEngine — EVERY translated "
                           "string in the app will be undefined. main.cpp must call "
                           "translationManager.setJsEngine(&engine) before engine.load(). "
                           "This message is logged once.";
        }
        return QJSValue();
    }

    if (m_translateFn.isUndefined() || m_translateFn.isNull()) {
        // Built once and cached. Bindings re-evaluate on every translationsChanged, and
        // rebuilding the wrapper each time would allocate on every language change.
        //
        // CppOwnership is required, not defensive: newQObject() hands the object to the JS
        // engine's GC by default, and TranslationManager is a stack object in main() that
        // outlives nothing. Without this the engine could collect it out from under the
        // callable held by 3,248 live bindings.
        QJSEngine::setObjectOwnership(this, QJSEngine::CppOwnership);
        QJSValue wrapper = m_jsEngine->newQObject(this);
        m_translateFn = wrapper.property(QStringLiteral("translateString"));
    }
    return m_translateFn;
}

QString TranslationManager::translateString(const QString& key, const QString& fallback)
{
    // Skip empty/whitespace keys or fallbacks
    if (key.trimmed().isEmpty() || fallback.trimmed().isEmpty()) {
        return fallback;
    }

    // Auto-register the string, or update it if the English changed under this key.
    if (noteSourceString(key, fallback)) {
        // Don't save on every call - batch save periodically
        m_registryDirty = true;

        // Propagate existing translation from other keys with the same fallback
        // This ensures new keys get translations that were applied before they were registered.
        // It now also runs for a key whose English was REWORDED: noteSourceString has just
        // dropped that key's old translation, and if the new wording matches a string already
        // translated elsewhere, this refills it for free rather than waiting for a re-translate.
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
    // Same rollback contract as setGroupTranslation: an edit that cannot be persisted must not
    // be left on screen looking applied. Note m_userOverrides in particular — recording a key as
    // "user customised" while the text it refers to was never written is worse than doing
    // nothing, because the override then protects a string that does not exist.
    const bool hadPrevious = m_translations.contains(key);
    const QString previous = m_translations.value(key);
    const bool wasAiGenerated = m_aiGenerated.contains(key);
    const bool wasOverride = m_userOverrides.contains(key);

    m_translations[key] = translation;
    m_aiGenerated.remove(key);  // User edited, no longer AI-generated
    m_userOverrides.insert(key);  // Track as user override (preserved during updates)

    if (!saveTranslations()) {
        if (hadPrevious) m_translations[key] = previous; else m_translations.remove(key);
        if (wasAiGenerated) m_aiGenerated.insert(key);
        if (!wasOverride) m_userOverrides.remove(key);
        qWarning() << "Edit of" << key << "was NOT saved and has been rolled back";
        emit translationsChanged();
        emit translationChanged(key);
        return;
    }

    // The override marker matters as much as the text: it is the only thing stopping the next
    // community update from overwriting this edit. Persisting the translation but not the marker
    // means the edit silently reverts on the next Update — the exact failure m_userOverrides
    // exists to prevent, arrived at from the success path.
    if (!saveUserOverrides()) {
        qWarning() << "Translation for" << key << "was saved but its user-override marker was"
                   << "NOT — a community update could overwrite this edit";
    }
    recalculateUntranslatedCount();
    m_translationVersion++;
    emit translationsChanged();
    emit translationChanged(key);
}

void TranslationManager::deleteTranslation(const QString& key)
{
    if (m_translations.contains(key)) {
        const QString previous = m_translations.value(key);
        m_translations.remove(key);
        if (!saveTranslations()) {
            m_translations[key] = previous;   // deletion not persisted; do not show it as gone
            qWarning() << "Deletion of" << key << "was NOT saved and has been rolled back";
            emit translationsChanged();
            emit translationChanged(key);
            return;
        }
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

    // Honour the verdict: a language that exists only in memory shows in the picker today and
    // vanishes at restart, which reads as data loss. Refuse the add instead — the helper has
    // already warned and set lastError.
    if (!saveLanguageMetadata()) {
        m_languageMetadata.remove(langCode);
        qWarning() << "Language" << langCode << "was NOT added - its metadata could not be saved";
        return;
    }

    // Create an empty translation file ONLY if there is not one already.
    //
    // This opened WriteOnly unconditionally, which truncates. The guard above is only
    // `m_languageMetadata.contains(langCode)`, so if the metadata file was ever lost the
    // language vanishes from the picker while its .json sits on disk — and the obvious recovery,
    // adding the language back, destroyed exactly the file the user was trying to recover.
    // The action a person takes to fix the problem must not be the one that makes it permanent.
    const QString newLangPath = languageFilePath(langCode);
    if (QFile::exists(newLangPath)) {
        qWarning() << "Language" << langCode << "was missing from the metadata but its file"
                   << "exists — keeping the existing translations rather than overwriting them";
    } else {
        QJsonObject root;
        root["language"] = langCode;
        root["displayName"] = displayName;
        root["nativeName"] = nativeName.isEmpty() ? displayName : nativeName;
        root["translations"] = QJsonObject();
        // Tolerable discard: this file is an empty scaffold, and the first real save of a
        // translation writes the same path in full. The helper has warned.
        (void)writeJsonFile(newLangPath, QJsonDocument(root), tr("the new %1 language file").arg(langCode));
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

    const QVariantMap removed = m_languageMetadata.take(langCode);

    // Honour the verdict BEFORE deleting the translation file. With the old order a failed
    // metadata save left the language listed on disk while its file was already gone — at the
    // next launch it reappears in the picker holding nothing, which reads as corruption.
    if (!saveLanguageMetadata()) {
        m_languageMetadata[langCode] = removed;
        qWarning() << "Language" << langCode << "was NOT deleted - the metadata could not be saved,"
                   << "so its translation file has been left in place";
        return;
    }

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

    if (noteSourceString(key, fallback)) {
        // Tolerable discard: a lost registry write is rediscovered the next time the string
        // renders (this very function re-runs). The helper has warned.
        (void)saveStringRegistry();
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

    // Collect all QML files from the Qt resource system.
    //
    // The root is ":/qt/qml/Decenza/qml" — where qt_add_qml_module actually publishes, and what
    // main.cpp loads ("qrc:/qt/qml/Decenza/qml/main.qml"). This used to iterate ":/qml", which
    // does not exist in a Qt 6 module build, so the scan silently walked an empty tree and
    // registered nothing. Every key in the registry got there by being RENDERED at runtime, so
    // the registry has never been the app's string set — it is "strings this device happened to
    // display". That is why machineStatus.idle and four siblings are absent while their
    // rendered neighbours are present, why 446 keys have German translations the registry has
    // never heard of, and why the translation percentage's denominator was wrong. It also
    // means AI translation and community upload have never been offered the full string list.
    QStringList qmlFiles;
    QDirIterator it(QStringLiteral(":/qt/qml/Decenza/qml"), QStringList() << "*.qml",
                    QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        qmlFiles.append(it.next());
    }

    m_scanTotal = static_cast<int>(qmlFiles.size());
    emit scanProgressChanged();

    // A scan that finds no files is always a bug, never a valid state — the QML is compiled
    // into the binary. Say so loudly; the silent zero is what let this sit unnoticed.
    if (m_scanTotal == 0) {
        qWarning() << "TranslationManager: string scan found NO QML files under"
                   << ":/qt/qml/Decenza/qml — the resource root is wrong, so the registry will"
                   << "only ever contain strings this device has rendered. AI translation and"
                   << "community upload will be working from an incomplete list.";
    }
    qDebug() << "Scanning" << m_scanTotal << "QML files for translatable strings...";

    // Pattern 1: Direct translate() calls - translate("key", "fallback")
    QRegularExpression directCallRegex("translate\\s*\\(\\s*\"([^\"\\n]+)\"\\s*,\\s*\"([^\"\\n]+)\"\\s*\\)");

    // Pattern 2: ActionButton's translationKey/translationFallback properties
    QRegularExpression propKeyRegex("translationKey\\s*:\\s*\"([^\"\\n]+)\"");
    QRegularExpression propFallbackRegex("translationFallback\\s*:\\s*\"([^\"\\n]+)\"");

    // Pattern 3: Tr component's key/fallback properties - Tr { key: "..."; fallback: "..." }
    // [^"\n] — the capture must NOT cross a line.
    //
    // With [^"]+ this matched inside a string literal and then ran away. AISettingsPage has
    // `fallback: "How to get an API key:"`, whose TEXT ends in `key:` — so `\bkey\s*:\s*"`
    // matched at `key:"` and captured everything up to the next quote in the file: 249
    // characters of QML source, registered as a translation key.
    //
    // It was then dutifully AI-translated into every language and uploaded to the community
    // server, where "How to get an API key:" is now stored against a key made of
    // `color: Theme.textColor / font: Theme.subtitleFont / switch(Settings.ai.aiProvider)`.
    //
    // No QML string literal spans a raw newline, so excluding it costs nothing and stops the
    // regex escaping its own quotes.
    QRegularExpression trKeyRegex("\\bkey\\s*:\\s*\"([^\"\\n]+)\"");
    QRegularExpression trFallbackRegex("\\bfallback\\s*:\\s*\"([^\"\\n]+)\"");

    int stringsFound = 0;
    qsizetype initialCount = m_stringRegistry.size();
    QSet<QString> seenInQml;

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
                key = unescapeQmlLiteral(key);
                fallback = unescapeQmlLiteral(fallback);

                if (!key.trimmed().isEmpty() && !fallback.trimmed().isEmpty()) {
                    seenInQml.insert(key);
                    if (noteSourceString(key, fallback)) {
                        stringsFound++;
                    }
                }
            }

            // Pattern 2: Property-based translations (translationKey + translationFallback pairs)
            // Collect all keys and fallbacks, then match them by proximity in the file
            QMap<qsizetype, QString> keyPositions;  // position -> key
            QMap<qsizetype, QString> fallbackPositions;  // position -> fallback

            QRegularExpressionMatchIterator keyIt = propKeyRegex.globalMatch(content);
            while (keyIt.hasNext()) {
                QRegularExpressionMatch match = keyIt.next();
                keyPositions[match.capturedStart()] = match.captured(1);
            }

            QRegularExpressionMatchIterator fallbackIt = propFallbackRegex.globalMatch(content);
            while (fallbackIt.hasNext()) {
                QRegularExpressionMatch match = fallbackIt.next();
                fallbackPositions[match.capturedStart()] = match.captured(1);
            }

            // Match keys with their nearest following fallback
            for (auto posIt = keyPositions.constBegin(); posIt != keyPositions.constEnd(); ++posIt) {
                qsizetype keyPos = posIt.key();
                QString key = posIt.value();

                // Find the nearest fallback after this key (within 200 chars)
                for (auto fbIt = fallbackPositions.constBegin(); fbIt != fallbackPositions.constEnd(); ++fbIt) {
                    qsizetype fbPos = fbIt.key();
                    if (fbPos > keyPos && fbPos - keyPos < 200) {
                        QString fallback = fbIt.value();

                        // Unescape
                        key = unescapeQmlLiteral(key);
                        fallback = unescapeQmlLiteral(fallback);

                        if (!key.trimmed().isEmpty() && !fallback.trimmed().isEmpty()) {
                            seenInQml.insert(key);
                            if (noteSourceString(key, fallback)) {
                                stringsFound++;
                            }
                        }
                        break;  // Found the matching fallback
                    }
                }
            }

            // Pattern 3: Tr component's key/fallback properties
            QMap<qsizetype, QString> trKeyPositions;
            QMap<qsizetype, QString> trFallbackPositions;

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
            for (auto posIt = trKeyPositions.constBegin(); posIt != trKeyPositions.constEnd(); ++posIt) {
                qsizetype keyPos = posIt.key();
                QString key = posIt.value();

                // Find the nearest fallback (can be before or after the key)
                QString fallback;
                qsizetype minDistance = 200;

                for (auto fbIt = trFallbackPositions.constBegin(); fbIt != trFallbackPositions.constEnd(); ++fbIt) {
                    qsizetype fbPos = fbIt.key();
                    qsizetype distance = qAbs(fbPos - keyPos);
                    if (distance < minDistance) {
                        minDistance = distance;
                        fallback = fbIt.value();
                    }
                }

                if (!fallback.trimmed().isEmpty()) {
                    // Unescape
                    QString keyClean = key;
                    QString fallbackClean = fallback;
                    keyClean = unescapeQmlLiteral(keyClean);
                    fallbackClean = unescapeQmlLiteral(fallbackClean);

                    if (!keyClean.trimmed().isEmpty() && !fallbackClean.trimmed().isEmpty()) {
                        seenInQml.insert(keyClean);
                        if (noteSourceString(keyClean, fallbackClean)) {
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
        // Tolerable discard: the scan can simply be run again, and rendering re-registers
        // strings anyway. The helper has warned.
        (void)saveStringRegistry();
        recalculateUntranslatedCount();
        emit totalStringCountChanged();
    }

    m_scanning = false;
    emit scanningChanged();
    emit scanFinished(static_cast<int>(m_stringRegistry.size() - initialCount));

    qDebug() << "Scan complete. Found" << stringsFound << "new strings. Total:" << m_stringRegistry.size();

    // Keys held in the registry that this scan did not find in any QML file. Reported, never
    // removed: plenty of live strings are registered from C++ (blemanager, aimanager,
    // updatechecker, visualizer*, and main.cpp's per-provider model hints all call
    // translateString directly), and those legitimately appear nowhere in the QML tree. So this
    // list is CANDIDATES, not garbage — the registry does accumulate keys for UI that has since
    // been deleted (settings.ai.remoteMcp.adminFunnel/.adminHttps are two), but telling those
    // apart from a C++-registered key needs a human, and deleting a live one silently
    // untranslates it in every language.
    QStringList notInQml;
    for (auto regIt = m_stringRegistry.constBegin(); regIt != m_stringRegistry.constEnd(); ++regIt) {
        if (!seenInQml.contains(regIt.key()))
            notInQml << regIt.key();
    }
    if (!notInQml.isEmpty()) {
        notInQml.sort();
        qDebug().noquote() << "TranslationManager:" << notInQml.size()
                           << "registry keys were not found in any QML file. Live C++-registered"
                           << "strings look like this too — verify before removing any:"
                           << "\n    " + notInQml.join(QStringLiteral("\n    "));
    }
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

    QString url = QString("%1/v1/translations/languages").arg(TRANSLATION_API_BASE);
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

    QString url = QString("%1/v1/translations/languages/%2").arg(TRANSLATION_API_BASE, langCode);
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
                QString url = QString("%1/v1/translations/languages").arg(TRANSLATION_API_BASE);
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

    // Tolerable discard: this is a cache of the server's language list, refetched on the next
    // visit to the language page. The helper has warned.
    (void)saveLanguageMetadata();
    m_availableLanguages = m_languageMetadata.keys();
    emit availableLanguagesChanged();
    emit languageListDownloaded(true);

    qDebug() << "Language list updated. Available:" << m_availableLanguages;
}

// Merge a downloaded language into the on-disk file for a language that is NOT loaded.
//
// Returns false if the merge was refused or the write failed, in which case it has already
// warned and emitted languageDownloaded(..., false, reason) — the caller must simply return.
//
// Extracted from onLanguageFileFetched so a test can reach it. The bug this guards against
// (a local file that exists but cannot be read being replaced wholesale by the server's copy)
// lived in a network-reply slot, which is untestable, and was found by review rather than by
// the suite for exactly that reason.
bool TranslationManager::mergeDownloadedLanguageFile(const QString& langCode, const QJsonObject& root)
{
    // Not the active language, so it is not in memory. Merge on the file instead, keeping
    // any local translation that the download does not carry.
    // "No local file yet" and "local file exists but I could not read it" must be handled
    // differently, and conflating them is how a merge turns back into the replace it was
    // written to stop. If the file is there but locked, permission-denied, or corrupt, an
    // empty `merged` is not the truth — it is a failure to read the truth, and writing the
    // server's copy on top of it destroys exactly what this branch exists to protect.
    const QString path = languageFilePath(langCode);
    QJsonObject merged;
    QFile existing(path);
    if (existing.exists()) {
        if (!existing.open(QIODevice::ReadOnly)) {
            qWarning() << "Language merge ABORTED for" << langCode << "- cannot read"
                       << path << ":" << existing.errorString()
                       << "- refusing to overwrite it with the server copy";
            m_lastError = tr("Could not read the existing %1 file, so the update was not applied "
                             "(your local translations are untouched).").arg(langCode);
            emit lastErrorChanged();
            emit languageDownloaded(langCode, false, m_lastError);
            return false;
        }
        QJsonParseError parseError;
        const QJsonDocument localDoc = QJsonDocument::fromJson(existing.readAll(), &parseError);
        existing.close();
        if (parseError.error != QJsonParseError::NoError) {
            qWarning() << "Language merge ABORTED for" << langCode << "- local file is not"
                       << "valid JSON:" << parseError.errorString()
                       << "- refusing to overwrite it";
            m_lastError = tr("The existing %1 file could not be parsed, so the update was not "
                             "applied.").arg(langCode);
            emit lastErrorChanged();
            emit languageDownloaded(langCode, false, m_lastError);
            return false;
        }
        merged = localDoc.object().value(QStringLiteral("translations")).toObject();
    }

    // Same placeholder rule as mergeLanguageUpdate. This branch is narrow — both Update buttons
    // set currentLanguage first, so it is reached only when the language changes while a
    // download is in flight — but the check costs nothing and a sibling path enforcing a rule
    // its twin does not is exactly how the seven broken strings survived in the first place.
    const QJsonObject incoming = root["translations"].toObject();
    int skippedBadPlaceholders = 0;
    for (auto it = incoming.constBegin(); it != incoming.constEnd(); ++it) {
        const QString sourceEnglish = m_stringRegistry.value(it.key());
        if (!sourceEnglish.isEmpty()
            && placeholderSet(it.value().toString()) != placeholderSet(sourceEnglish)) {
            qWarning().noquote() << "Skipping community translation for" << it.key()
                                 << "- placeholders do not match. source=" << sourceEnglish
                                 << "incoming=" << it.value().toString();
            skippedBadPlaceholders++;
            continue;
        }
        merged[it.key()] = it.value();
    }
    if (skippedBadPlaceholders > 0)
        qWarning() << "Language file merge for" << langCode << "skipped"
                   << skippedBadPlaceholders << "string(s) with broken placeholders";

    QJsonObject out = root;
    out["translations"] = merged;

    // QSaveFile, not QFile: it writes to a temporary and renames on commit(), so a failure
    // leaves the original file exactly as it was.
    //
    // With plain QFile this had a hole that defeated the point of the whole function.
    // QIODevice::WriteOnly TRUNCATES on open, so the moment the open succeeded the user's
    // translations were already gone — a full disk, a crash, or a short write between there and
    // the end left them with an empty or half-written file. The previous version checked the
    // write length and reported a loss it had no way to reverse. Detecting destruction is not
    // the same as preventing it, and this function exists to prevent it.
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "Failed to write merged language file" << path << ":" << file.errorString();
        m_lastError = tr("Could not save the updated %1 file.").arg(langCode);
        emit lastErrorChanged();
        emit languageDownloaded(langCode, false, m_lastError);
        return false;
    }
    const QByteArray payload = QJsonDocument(out).toJson();
    file.write(payload);
    if (!file.commit()) {   // commit() reports short writes and rename failures alike
        qWarning() << "Failed to commit merged language file" << path << ":" << file.errorString()
                   << "- the existing file is unchanged";
        m_lastError = tr("Could not save the updated %1 file (your existing translations are "
                         "unchanged).").arg(langCode);
        emit lastErrorChanged();
        emit languageDownloaded(langCode, false, m_lastError);
        return false;
    }

    return true;
}

// Apply a fetched language document. Split out of the network slot so it is reachable from a
// test AT THE CALLER FRAME.
//
// That framing is deliberate. Two separate guards in this area were added, tested, and
// negative-controlled while the frame ABOVE them undid the guard — first by never running it,
// then by emitting success straight after its refusal. Both times the test was green and the
// bug shipped. Testing the helper proves the helper; only this level proves the outcome.
void TranslationManager::applyFetchedLanguage(const QString& langCode, const QJsonObject& root)
{
    // MERGE the download into what is already here — do not replace it.
    //
    // This used to write `data` straight over the language file. That made the Update button
    // destructive: the server's copy is whatever was last submitted by someone, and a machine
    // that has since AI-translated the gaps holds far more than that. One click replaced 3429
    // German strings with the server's 1515 and silently discarded the difference, twice, on
    // this machine. Nothing uploads automatically, so a richer local set is the NORMAL state,
    // not an edge case.
    //
    // The automatic check at launch has always merged (mergeLanguageUpdate) and preserved user
    // overrides. That the manual button did the opposite was the whole bug — the same action,
    // by two paths, with opposite outcomes.
    //
    // Merging is also what makes the overrides file meaningful: it stores only KEY NAMES, and
    // the text they refer to lives in this file. Replacing the file therefore threw away the
    // user's own wording while leaving the key still marked as customised.
    if (langCode == m_currentLanguage) {
        // Loaded language: merge in memory, which preserves overrides, then persist that.
        //
        // The return value MUST be honoured. It used to be void, so a refusal here fell through
        // to the success emit at the end of this function: the user got languageDownloaded(false)
        // immediately followed by languageDownloaded(true), and both QML handlers act on the
        // second. The Update button reported success having applied nothing — and worse, the
        // success branch then offers to AI-translate, which writes the file and destroys exactly
        // what the refusal had just protected. The guard was defeated by its own caller.
        if (!mergeLanguageUpdate(root["translations"].toObject())) {
            emit languageDownloaded(langCode, false, m_lastError);
            return;
        }
    } else {
        // Not the active language, so it is not in memory: merge on the FILE instead. Split out
        // so it can be tested — the destructive-replace bug lived in exactly this branch, and a
        // network-reply slot is not reachable from a test.
        if (!mergeDownloadedLanguageFile(langCode, root))
            return;   // helper has already warned and emitted languageDownloaded(false, ...)
    }

    // Update metadata
    m_languageMetadata[langCode] = QVariantMap{
        {"displayName", root["displayName"].toString(langCode)},
        {"nativeName", root["nativeName"].toString(langCode)},
        {"isRtl", root["isRtl"].toBool(false)},
        {"isRemote", false}  // Now downloaded locally
    };
    // Warn-and-continue, not refuse: the translations themselves persisted above, so the
    // download genuinely succeeded. A lost metadata write can hide the language from the picker
    // after a restart, but addLanguage() now preserves an orphaned file, so re-adding recovers
    // it. The helper has set lastError, which the toast surfaces.
    if (!saveLanguageMetadata()) {
        qWarning() << "Downloaded" << langCode << "but its metadata could not be saved -"
                   << "the language may be missing from the picker after a restart";
    }

    // Update available languages list (overwrites, no duplicates)
    m_availableLanguages = m_languageMetadata.keys();
    emit availableLanguagesChanged();

    // No reload for the current language: mergeLanguageUpdate() above already folded the
    // download into the in-memory map, saved it, and refreshed the counts. Re-reading the file
    // here would be harmless but pointless — and reloading BEFORE that merge existed is
    // precisely how the replaced file became the live state.
    if (langCode != m_currentLanguage) {
        recalculateUntranslatedCount();
    }

    // Always increment version to refresh UI (language list colors/percentages)
    m_translationVersion++;
    emit translationsChanged();

    emit languageDownloaded(langCode, true, QString());
    qDebug() << "Downloaded language:" << langCode;
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
                QString url = QString("%1/v1/translations/languages/%2").arg(TRANSLATION_API_BASE, langCode);
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

    const QJsonObject root = doc.object();

    applyFetchedLanguage(langCode, root);
}

void TranslationManager::exportTranslation(const QString& filePath)
{
    // Refuse to export a map that is empty by failure. Taking a backup before repairing a
    // broken file is the natural move, and this silently wrote {"translations":{}} and called
    // it done — handing the user a file that looks like a backup and contains nothing.
    if (m_translationsLoadFailed) {
        qWarning() << "Refusing to export" << m_currentLanguage
                   << "- its local file could not be read, so there is nothing trustworthy to"
                   << "export. The exported file would be empty.";
        m_lastError = tr("The %1 file could not be read, so there was nothing to export.")
                          .arg(m_currentLanguage);
        emit lastErrorChanged();
        return;
    }

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

    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        m_lastError = QString("Failed to write file: %1").arg(filePath);
        emit lastErrorChanged();
        return;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        qWarning() << "Failed to commit export to" << filePath << ":" << file.errorString();
        m_lastError = QString("Failed to write file: %1").arg(filePath);
        emit lastErrorChanged();
        return;
    }
    qDebug() << "Exported translation to:" << filePath;
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

    // MERGE the import into what is already here — do not replace it.
    //
    // This wrote `data` straight over the language file, which made Import the last unguarded
    // copy of the bug the download path was rewritten to fix. Same file, same destruction, one
    // button along: export a backup at 1500 strings, AI-translate to 3429, then import the
    // backup to restore a few hand-corrections, and the other 1929 are gone with no prompt, no
    // diff and no undo. The plain QFile made it worse — WriteOnly truncates at open, so a short
    // write leaves exactly the corrupt file loadTranslations() now has to guard against.
    //
    // Merging is the safe direction: it cannot lose a string. A user who genuinely wants to
    // start over can delete the language first, which is explicit and reversible in a way that
    // a silent overwrite is not.
    const QJsonObject incoming = root["translations"].toObject();
    QJsonObject merged;
    const QString destPath = languageFilePath(langCode);
    QFile existing(destPath);
    if (existing.exists()) {
        if (!existing.open(QIODevice::ReadOnly)) {
            qWarning() << "Import ABORTED for" << langCode << "- cannot read existing"
                       << destPath << ":" << existing.errorString();
            m_lastError = tr("Could not read the existing %1 file, so nothing was imported.")
                              .arg(langCode);
            emit lastErrorChanged();
            return;
        }
        QJsonParseError parseError;
        const QJsonDocument localDoc = QJsonDocument::fromJson(existing.readAll(), &parseError);
        existing.close();
        if (parseError.error != QJsonParseError::NoError) {
            qWarning() << "Import ABORTED for" << langCode << "- existing file is not valid JSON:"
                       << parseError.errorString();
            m_lastError = tr("The existing %1 file could not be parsed, so nothing was imported.")
                              .arg(langCode);
            emit lastErrorChanged();
            return;
        }
        merged = localDoc.object().value(QStringLiteral("translations")).toObject();
    }
    for (auto it = incoming.constBegin(); it != incoming.constEnd(); ++it)
        merged[it.key()] = it.value();

    QJsonObject out = root;
    out["translations"] = merged;
    if (!writeJsonFile(destPath, QJsonDocument(out), tr("the imported %1 translations").arg(langCode)))
        return;   // writeJsonFile has already warned and set m_lastError

    // Update metadata
    m_languageMetadata[langCode] = QVariantMap{
        {"displayName", root["displayName"].toString(langCode)},
        {"nativeName", root["nativeName"].toString(langCode)},
        {"isRtl", root["isRtl"].toBool(false)},
        {"isRemote", false}
    };
    // Same warn-and-continue as the download path: the imported translations persisted above,
    // and addLanguage() recovers an orphaned file if the metadata is lost.
    if (!saveLanguageMetadata()) {
        qWarning() << "Imported" << langCode << "but its metadata could not be saved -"
                   << "the language may be missing from the picker after a restart";
    }

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
        // Silent no-op, and the batch has no other way to advance: if this is ever reached from
        // the batch it stalls with m_batchProcessing stuck true, which then blocks every future
        // batch for the life of the process. Say so rather than returning quietly.
        qWarning() << "submitTranslation ignored - an upload is already in progress for"
                   << m_currentLanguage;
        emit translationSubmitted(false, tr("An upload is already in progress."));
        return;
    }

    // Never publish a map that is empty by failure. This is the same ambiguity as the save and
    // merge guards, but with the widest blast radius: the upload replaces the SERVER's copy for
    // every user of this language, not just this machine's file. Reachable from the same
    // corrupt-file-shows-0% state, and from the batch, which uploads without asking.
    if (m_translationsLoadFailed) {
        qWarning() << "Refusing to upload" << m_currentLanguage
                   << "- its local file could not be read, so the in-memory map is empty by"
                   << "failure. Publishing it would overwrite the community copy with nothing.";
        m_lastError = tr("The %1 file could not be read, so nothing was uploaded.")
                          .arg(m_currentLanguage);
        emit lastErrorChanged();
        emit translationSubmitted(false, m_lastError);
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
    m_uploadingLangCode = m_currentLanguage;   // pin: the retry must not re-derive this later

    m_uploading = true;
    emit uploadingChanged();

    // Request a pre-signed URL from the backend, passing the language code
    QString uploadUrlEndpoint = QString("%1/v1/translations/upload-url?lang=%2").arg(TRANSLATION_API_BASE, m_currentLanguage);
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
            m_retryStatus = QString("Upload rate limited, retrying %1/%2...")
                                .arg(m_uploadRetryCount).arg(MAX_RETRIES);
            emit retryStatusChanged();

            // Schedule retry after delay.
            //
            // Capture the language this upload is FOR. Reading m_currentLanguage inside the
            // lambda meant the retry asked for an upload URL for whatever language was selected
            // 30 seconds later, while m_pendingUploadData still held the payload captured for
            // the original one. Nothing stops the user switching language while a rate-limited
            // upload waits — m_uploading gates submitTranslation(), not setCurrentLanguage() —
            // so uploading French, hitting 429, and switching to German published the French
            // strings as the German community copy, then reported "Translation for German
            // submitted successfully! Thank you for contributing."
            //
            // Up to MAX_RETRIES chances per upload, on the status this file documents as the
            // ordinary one. The blast radius is every user of that language, not this machine.
            const QString uploadLang = m_uploadingLangCode.isEmpty() ? m_currentLanguage
                                                                     : m_uploadingLangCode;
            QTimer::singleShot(RETRY_DELAY_MS, this, [this, uploadLang]() {
                if (uploadLang != m_uploadingLangCode) {
                    qWarning() << "Abandoning upload retry for" << uploadLang
                               << "- the pending upload is now for" << m_uploadingLangCode;
                    return;
                }
                // Re-request the upload URL
                QString uploadUrlEndpoint = QString("%1/v1/translations/upload-url?lang=%2")
                    .arg(TRANSLATION_API_BASE)
                    .arg(uploadLang);

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
        const int statusCodeFinal = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        m_lastError = statusCodeFinal == 429
            ? QString("Upload limit reached. The server allows a limited number of translation "
                      "uploads per hour — try again in up to %1 minutes.")
                  .arg(RATE_LIMIT_WINDOW_MINUTES)
            : QString("Failed to get upload URL: %1").arg(reply->errorString());
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

    // Name the language that was actually uploaded. The retry fix pinned the URL to
    // m_uploadingLangCode but left this reading m_currentLanguage, so a rate-limited French
    // upload completing after a switch to German still announced a successful GERMAN
    // contribution — the exact wrong report the fix's own comment quotes.
    QString message = QString("Translation for %1 submitted successfully! Thank you for contributing.")
                          .arg(getLanguageDisplayName(m_uploadingLangCode.isEmpty() ? m_currentLanguage : m_uploadingLangCode));
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

    // Snapshot so the edit can be rolled back if it cannot be persisted. Showing the user an
    // edit that did not reach disk is the failure this whole area is about: they see it applied,
    // close the app, and it is gone — with nothing having said so at any point.
    QMap<QString, QString> previous;
    QSet<QString> previouslyAiGenerated;
    for (const QString& key : keys) {
        if (m_translations.contains(key))
            previous[key] = m_translations.value(key);
        if (m_aiGenerated.contains(key))
            previouslyAiGenerated.insert(key);
    }

    for (const QString& key : keys) {
        if (translation.isEmpty()) {
            m_translations.remove(key);
        } else {
            m_translations[key] = translation;
        }
        m_aiGenerated.remove(key);  // User edited, no longer AI-generated
    }

    if (!saveTranslations()) {
        // Roll back rather than display an edit that was not kept. m_lastError is already set.
        for (const QString& key : keys) {
            if (previous.contains(key))
                m_translations[key] = previous.value(key);
            else
                m_translations.remove(key);
            if (previouslyAiGenerated.contains(key))
                m_aiGenerated.insert(key);
        }
        qWarning() << "Edit of" << fallback.left(40) << "was NOT saved and has been rolled back";
        emit translationsChanged();   // repaint the reverted state, not the phantom edit
        return;
    }

    // Clear the AI translation here rather than leaving it to the caller.
    //
    // StringBrowserPage used to do this in the two lines AFTER calling this function, and those
    // lines could not run: emit translationsChanged() below is synchronous, the page's handler
    // calls stringModel.refresh(), and the ListView delegate whose function is mid-execution is
    // destroyed underneath it. The result was a ReferenceError — "TranslationManager is not
    // defined" — because the delegate's context had gone, so a manual edit never cleared the
    // stale AI value. The setEditing() call after it was failing silently for the same reason.
    //
    // Doing it here also fixes a correctness bug the QML version had: it cleared the AI
    // translation whether or not the edit was actually saved. Now it only happens on the path
    // where the save succeeded, because the refusal above returns before reaching this.
    if (!translation.isEmpty() && m_currentLanguage != QLatin1String("en"))
        clearAiTranslation(fallback);

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
        // Rollback on a refused save, like every other edit path: no QML calls this today, but
        // it is a Q_INVOKABLE — one line of QML away from reachable — and an exposed mutator
        // that displays a change it could not persist is the exact bug this file kept growing.
        const bool hadPrevious = m_translations.contains(key);
        const QString previous = m_translations.value(key);
        m_translations[key] = mostCommon;
        if (!saveTranslations()) {
            if (hadPrevious) m_translations[key] = previous;
            else m_translations.remove(key);
            qWarning() << "Group merge for" << key << "was NOT saved and has been rolled back";
            return;
        }
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
    return static_cast<int>(uniqueFallbacks.size());
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

    // "No file yet" and "there is a file but I could not read it" both used to leave
    // m_translations empty and indistinguishable, and that ambiguity is a data-loss bug rather
    // than an untidiness: mergeLanguageUpdate() folds a download into this map and saves the
    // result, so merging into a wrongly-empty map REPLACES the user's file with the server's
    // copy. The likely sequence is bleak — a corrupt file shows the language at 0% translated,
    // the user presses Update precisely because it says 0%, and that click destroys the strings
    // the corrupt file still held.
    m_translationsLoadFailed = false;

    // Load translations for any language (including English customizations)
    QFile file(languageFilePath(m_currentLanguage));
    if (!file.exists()) {
        qDebug() << "No translation file for:" << m_currentLanguage;
        return;   // genuinely absent: an empty map is the truth
    }
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "Translation file for" << m_currentLanguage << "exists but cannot be read:"
                   << file.errorString() << "- refusing to treat it as empty";
        m_translationsLoadFailed = true;
        return;
    }

    QByteArray data = file.readAll();
    file.close();

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) {
        qWarning() << "Invalid translation file for:" << m_currentLanguage
                   << "- refusing to treat it as empty";
        m_translationsLoadFailed = true;
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

// Write a JSON document to `path` atomically, or report why not.
//
// Every persistence helper in this file used to be the same four lines — QFile, if(open),
// write, close — with no else, no check of write()'s return, and a void signature. That shape
// is what made a full disk or a read-only profile directory indistinguishable from success at
// every call site, and it is why four review rounds each found another caller reporting work
// it had not done.
//
// QSaveFile writes a temporary and renames on commit, so a failure leaves the previous file
// exactly as it was. QIODevice::WriteOnly on a plain QFile truncates at open, which means the
// old content is destroyed before the first byte of the new content is written — the very way
// the corrupt files this class now has to guard against get made.
bool TranslationManager::writeJsonFile(const QString& path, const QJsonDocument& doc,
                                       const QString& what)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "Failed to open" << path << "for writing:" << file.errorString()
                   << "-" << what << "NOT saved";
        m_lastError = tr("Could not save %1.").arg(what);
        emit lastErrorChanged();
        return false;
    }
    file.write(doc.toJson());
    if (!file.commit()) {
        qWarning() << "Failed to commit" << path << ":" << file.errorString()
                   << "-" << what << "NOT saved; the previous file is intact";
        m_lastError = tr("Could not save %1 (the previous file is unchanged).").arg(what);
        emit lastErrorChanged();
        return false;
    }
    return true;
}

bool TranslationManager::saveTranslations()
{
    // Refuse when the local file failed to LOAD, for the same reason mergeLanguageUpdate does:
    // m_translations is empty by failure, not by fact, and writing it out replaces the user's
    // file with that emptiness.
    //
    // This guard was originally put only on the download/merge path, which was the wrong half.
    // A user whose file is corrupt sees the language at 0% translated, and the two things the
    // UI invites at 0% are editing a string and running AI Translate — both of which land here
    // via setTranslation() and the auto-translate pass, neither of which went anywhere near the
    // guarded path. The guarded door was the one least likely to be used, and the AI route
    // costs money before it destroys anything.
    if (m_translationsLoadFailed) {
        qWarning() << "Refusing to save" << m_currentLanguage
                   << "- its local file could not be read, so the in-memory map is empty by"
                   << "failure rather than by fact. Writing it would replace the file with"
                   << "nothing. Repair or delete" << languageFilePath(m_currentLanguage);
        m_lastError = tr("The %1 file could not be read earlier, so nothing was saved over it. "
                         "Repair or delete that file, then restart.").arg(m_currentLanguage);
        emit lastErrorChanged();
        return false;
    }

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

    // QSaveFile: temp + atomic rename. This is the writer that RUNS on every string edit and
    // after every AI pass, so it is the one most likely to produce the truncated file the load
    // guard above then has to detect. Atomicity was applied first to the rare download-merge
    // branch and not to this one, which had the priority exactly backwards.
    const QString path = languageFilePath(m_currentLanguage);
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "Failed to open" << path << "for writing:" << file.errorString()
                   << "- translations NOT saved";
        m_lastError = tr("Could not save translations for %1.").arg(m_currentLanguage);
        emit lastErrorChanged();
        return false;
    }
    file.write(QJsonDocument(root).toJson());
    if (!file.commit()) {
        qWarning() << "Failed to commit" << path << ":" << file.errorString()
                   << "- the previous file is intact";
        m_lastError = tr("Could not save translations for %1 (the previous file is unchanged).")
                          .arg(m_currentLanguage);
        emit lastErrorChanged();
        return false;
    }
    return true;
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

bool TranslationManager::saveLanguageMetadata()
{
    QJsonObject root;
    for (auto it = m_languageMetadata.constBegin(); it != m_languageMetadata.constEnd(); ++it) {
        root[it.key()] = QJsonObject::fromVariantMap(it.value());
    }

    QString metaPath = translationsDir() + "/languages_meta.json";
    return writeJsonFile(metaPath, QJsonDocument(root), tr("the language list"));
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

QString TranslationManager::unescapeQmlLiteral(const QString& literal)
{
    QString out;
    out.reserve(literal.size());
    for (qsizetype i = 0; i < literal.size(); ++i) {
        if (literal[i] != u'\\' || i + 1 >= literal.size()) {
            out += literal[i];
            continue;
        }
        const QChar esc = literal[++i];
        switch (esc.unicode()) {
        case u'n':  out += u'\n'; break;
        case u't':  out += u'\t'; break;
        case u'r':  out += u'\r'; break;
        case u'"':  out += u'"';  break;
        case u'\'': out += u'\''; break;
        case u'\\': out += u'\\'; break;
        case u'u': {
            bool ok = false;
            const char16_t cp = literal.mid(i + 1, 4).toUShort(&ok, 16);
            if (ok && i + 4 < literal.size()) {
                out += QChar(cp);
                i += 4;
                break;
            }
            out += u'\\';
            out += esc;
            break;
        }
        default:
            // An escape this scanner does not model. The engine may well decode it (identity
            // escapes, \b \f \v \0 \xNN, \u{...}), so leaving it written verbatim is the
            // deliberate choice: a scanner that guesses can invent a character, one that
            // declines can only fail to decode. See the header for the full subset.
            // rather than silently eating the backslash.
            out += u'\\';
            out += esc;
            break;
        }
    }
    return out;
}

bool TranslationManager::noteSourceString(const QString& key, const QString& fallback)
{
    const auto existing = m_stringRegistry.constFind(key);
    if (existing == m_stringRegistry.constEnd()) {
        m_stringRegistry[key] = fallback;
        return true;
    }

    // Exact compare first. This is the path taken for every string on every render, and it
    // allocates nothing; the trimmed() compare below would allocate twice per call if it ran
    // unconditionally. Trimming is only consulted once the two already differ, so that
    // re-indenting a multi-line fallback does not count as a rewrite.
    const QString previous = existing.value();
    if (previous == fallback || previous.trimmed() == fallback.trimmed())
        return false;

    m_stringRegistry[key] = fallback;

    // Persist immediately rather than leaving it to the batched save. translateString() is one
    // of the callers and only marks the registry dirty, so on that path the new English could
    // be lost if the process ended first, and the next launch would rediscover the same change.
    // A change here is rare by construction, so the write is cheap. Tolerable discard: if it
    // fails, the next launch rediscovers the same change — exactly the situation this write
    // exists to shorten, not to guarantee. The helper has warned.
    (void)saveStringRegistry();

    // Report, do NOT touch the translation.
    //
    // An earlier version of this dropped the translation on the reasoning that it rendered a
    // sentence that no longer exists and could be flatly wrong. Running it against the real app
    // killed that idea: a key having ONE English string is an assumption this codebase does not
    // hold. 26 keys are used with two different fallbacks in the SAME build — every
    // beanbase.details.* label ("Elevation" vs "Elevation:" between BeanBaseDetailsPopup and
    // ChangeBeansDialog), and common.accessibility.dismissDialog across eleven files
    // ("Close dialog" vs "Dismiss dialog"). Those keys do not drift, they oscillate: whichever
    // site is scanned or rendered last wins, so dropping would destroy their translations
    // repeatedly and silently, on every launch, forever. It ate 11 German strings on the first
    // real run.
    //
    // Weighed on measured numbers, the drop cost more than it bought: 52 keys of genuine drift,
    // mostly cosmetic and partly self-healing through the propagate step below, against 26 keys
    // of permanent damage. So the honest thing is to say what happened and leave the data alone.
    // Anyone who wants the translation refreshed can re-translate that key; anyone who sees this
    // warning repeatedly for one key is looking at a source conflict, not a rewrite.
    if (m_translations.contains(key)) {
        qWarning().noquote()
            << "TranslationManager: source text changed for" << key
            << "— its" << m_currentLanguage << "translation still renders the OLD text."
            << "Repeats for the same key mean two QML sites disagree about this key's English,"
            << "not that it was rewritten."
            << "\n    was:" << previous.left(80)
            << "\n    now:" << fallback.left(80);
    }
    return true;
}

bool TranslationManager::saveStringRegistry()
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
    return writeJsonFile(regPath, QJsonDocument(root), tr("the string registry"));
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
        // Tolerable discard: propagation is deterministic — the same copies are recomputed
        // from the registry on the next recalculate, which runs at every launch — and the
        // source translations it copied FROM are already on disk. The helper has warned.
        (void)saveTranslations();
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

    QString provider = m_settings->ai()->aiProvider();
    if (provider == "openai" && !m_settings->ai()->openaiApiKey().isEmpty()) return true;
    if (provider == "anthropic" && !m_settings->ai()->anthropicApiKey().isEmpty()) return true;
    if (provider == "gemini" && !m_settings->ai()->geminiApiKey().isEmpty()) return true;
    if (provider == "ollama" && !m_settings->ai()->ollamaEndpoint().isEmpty() && !m_settings->ai()->ollamaModel().isEmpty()) return true;

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
    m_autoTranslateParseFailures = 0;   // per-run, like m_batchFailedUploads
    m_autoTranslateRejected = 0;
    m_autoTranslateFatal = false;
    m_autoTranslateTotal = static_cast<int>(m_stringsToTranslate.size());
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
        // Fatal to the whole run, not just this language. The header says this flag covers "a
        // user cancel" and it did not: the batch's non-fatal branch then recorded the cancel as
        // one language's failure and called processNext(), so pressing Stop began translating
        // the NEXT language on the user's paid key. Stop must mean stop.
        m_autoTranslateFatal = true;
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
        request.setRawHeader("Authorization", ("Bearer " + m_settings->ai()->openaiApiKey()).toUtf8());

        QJsonObject json;
        json["model"] = translationModelFor(provider, QString());
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
        request.setRawHeader("x-api-key", m_settings->ai()->anthropicApiKey().toUtf8());
        request.setRawHeader("anthropic-version", "2023-06-01");

        QJsonObject json;
        json["model"] = translationModelFor(provider, QString());
        json["max_tokens"] = 4096;
        QJsonArray messages;
        QJsonObject msg;
        msg["role"] = "user";
        msg["content"] = prompt;
        messages.append(msg);
        json["messages"] = messages;
        postData = QJsonDocument(json).toJson();

    } else if (provider == "gemini") {
        QString apiKey = m_settings->ai()->geminiApiKey();
        // Gemini carries the model in the PATH rather than the body, which is why it was the
        // easiest one to leave stale — "gemini-2.0-flash" is not in the app's own model list.
        const QString geminiModel = translationModelFor(provider, QString());
        request.setUrl(QUrl("https://generativelanguage.googleapis.com/v1beta/models/"
                            + geminiModel + ":generateContent"));
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
        QString endpoint = m_settings->ai()->ollamaEndpoint();
        if (!endpoint.endsWith("/")) endpoint += "/";
        request.setUrl(QUrl(endpoint + "api/generate"));
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

        QJsonObject json;
        json["model"] = m_settings->ai()->ollamaModel();
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

            // Persist whatever DID apply before the stop. Batches that parsed and applied are
            // paid for and already in m_translations; this path used to drop them on the floor,
            // so ten good batches followed by one transport error left the user billed for
            // eleven and holding none. Only the completion path saved, and the two exits
            // disagreeing about that is the bug.
            // Honour the verdict here too. Writing this block while fixing the identical bug on
            // the completion path ten lines below, and still calling saveTranslations() bare, is
            // how the message below came to promise "and saved" for work that may never have
            // reached disk. The two exits disagreeing about the save is the bug this block was
            // added to fix; it then disagreed in the other direction.
            bool drainSaved = true;
            if (m_autoTranslateProgress > 0) {
                qDebug() << "Persisting" << m_autoTranslateProgress << "strings applied before the stop";
                drainSaved = saveTranslations();
                if (!drainSaved)
                    m_autoTranslateFatal = true;   // a local failure, same as the completion path
                // Attempt the AI cache EVEN IF the main save failed: _ai.json is then the only
                // record of the paid AI output, re-applicable via copyAiToFinal after repair.
                if (!saveAiTranslations() && drainSaved)
                    // Only the AI-provenance cache failed. Strings keep working but lose their
                    // "AI generated" marking after a restart.
                    qWarning() << "Stopped run saved, but the AI cache file did not";
                recalculateUntranslatedCount();
                m_translationVersion++;
                emit translationsChanged();
            }

            // A user-initiated cancel sets no error, which left this emitting an empty or, worse,
            // a stale unrelated message. Say which of the two actually happened.
            QString finishMessage = m_lastError;
            if (!drainSaved) {
                // saveTranslations() has already set m_lastError explaining why.
                qWarning().noquote() << "AI translation stopped and could NOT persist"
                                     << m_autoTranslateProgress << "applied strings:" << m_lastError;
            } else if (finishMessage.isEmpty()) {
                // A clean user stop is an outcome, not an error — the notice channel keeps the
                // toast from dressing "your work is saved" in error styling.
                finishMessage = tr("Translation stopped. %1 strings were translated and saved.")
                                    .arg(m_autoTranslateProgress);
                emit translationNotice(finishMessage);
            }
            emit autoTranslateFinished(false, finishMessage);
        }
        return;
    }

    if (reply->error() != QNetworkReply::NoError) {
        // Set cancelled flag but DON'T emit autoTranslateFinished yet
        // Wait for all in-flight responses to complete first
        m_autoTranslateCancelled = true;
        m_autoTranslateFatal = true;   // the provider itself is unusable; later languages would fail identically
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
    if (!parseAutoTranslateResponse(data))
        m_autoTranslateParseFailures++;

    // Check if all batches are complete
    if (m_pendingBatchCount == 0) {
        qDebug() << "TranslationManager: All batches complete for" << m_currentLanguage;
        m_autoTranslating = false;
        emit autoTranslatingChanged();
        // The verdict MUST be honoured. saveTranslations() returned void, so a refusal here was
        // invisible and the block below still emitted success: a user whose file was corrupt saw
        // 0%, pressed AI Translate for that reason, paid for the whole language, watched the
        // strings appear on screen — and lost every one of them at restart, having been told
        // "Translated 3429 strings". That is round two's bug reproduced on round three's guard,
        // on the path this branch itself identified as the one users actually take.
        const bool saved = saveTranslations();
        // Attempt the AI cache EVEN IF the main save failed: _ai.json is then the only record
        // of the paid AI output, and copyAiToFinal can re-apply it once the disk is repaired.
        if (!saveAiTranslations() && saved)
            // Main file persisted, provenance cache did not: translations survive a restart
            // but stop being marked "AI generated". Worth a line, not a failed run.
            qWarning() << "Completed run saved, but the AI cache file did not";
        recalculateUntranslatedCount();
        m_translationVersion++;
        emit translationsChanged();
        // A batch the provider answered with something unusable is a FAILURE, not zero
        // translations. This used to be an unconditional `true`: parseAutoTranslateResponse
        // returned void, so a provider replying 200-with-prose produced
        // autoTranslateFinished(true, "Translated 0 strings") — and inside the bulk run that
        // success is what triggers submitTranslation(), publishing an untranslated file over
        // the community copy. A paid run reported complete, having translated nothing and
        // uploaded damage. Same "credit for work not done" shape as the upload accounting.
        //
        // m_autoTranslateCancelled is belt-and-braces, NOT a fixed defect. The commit that added
        // it claimed the cancelled drain reported success; that was wrong — the drain above
        // already emits autoTranslateFinished(false, ...) and always did. The only way to arrive
        // here with the flag set is a QML handler calling cancelAutoTranslate() re-entrantly
        // from a progress signal emitted inside the parser. Kept for that narrow case, and
        // labelled honestly because a false claim of a fixed bug is exactly what this branch
        // keeps having to correct.
        if (!saved) {
            m_autoTranslateFatal = true;   // disk/state problem — every remaining language hits it too
            // m_lastError is already set by saveTranslations().
            qWarning().noquote() << "AI translation: applied" << m_autoTranslateProgress
                                 << "strings but could NOT persist them -" << m_lastError;
            emit autoTranslateFinished(false, m_lastError);
        } else if (m_autoTranslateParseFailures > 0) {
            // A provider returning garbage is a genuine failure — error channel.
            m_lastError = tr("%1 returned %2 unusable response(s); %3 strings were "
                             "translated. Nothing was uploaded.")
                              .arg(selectedProviderLabel())
                              .arg(m_autoTranslateParseFailures)
                              .arg(m_autoTranslateProgress);
            emit lastErrorChanged();
            qWarning().noquote() << "AI translation:" << m_lastError;
            emit autoTranslateFinished(false, m_lastError);
        } else if (m_autoTranslateCancelled) {
            QString finishMessage = m_lastError;
            if (finishMessage.isEmpty()) {
                // A stop the user asked for, with the work saved — an outcome, not an error.
                finishMessage = tr("Translation stopped early; %1 strings were translated. "
                                   "Nothing was uploaded.").arg(m_autoTranslateProgress);
                emit translationNotice(finishMessage);
            }
            qWarning().noquote() << "AI translation:" << finishMessage;
            emit autoTranslateFinished(false, finishMessage);
        } else {
            QString okMsg = QString("Translated %1 strings").arg(m_autoTranslateProgress);
            if (m_autoTranslateRejected > 0) {
                // Not a failed run — the rest applied — but the user must be told these were
                // dropped, or they are left looking English with no explanation.
                okMsg += QStringLiteral(" (%1 rejected: placeholders did not match)")
                             .arg(m_autoTranslateRejected);
                qWarning().noquote() << "AI translation:" << m_autoTranslateRejected
                                     << "string(s) rejected for placeholder mismatch";
            }
            emit autoTranslateFinished(true, okMsg);
        }
    }
}

// The set of %N placeholders a string carries.
//
// Numbered rather than positional, so a translation is free to REORDER them — that is the whole
// point of %1/%2 — but it must not lose or invent one. Compared as a set, not a list: QString::arg
// replaces every occurrence of %1, so repeating or de-duplicating one is harmless.
//
// 184 of the ~3680 registered strings carry these, and %N is the only form the corpus uses.
QSet<int> TranslationManager::placeholderSet(const QString& text)
{
    static const QRegularExpression re(QStringLiteral("%L?(\\d)"));
    QSet<int> found;
    auto it = re.globalMatch(text);
    while (it.hasNext())
        found.insert(it.next().captured(1).toInt());
    return found;
}

bool TranslationManager::parseAutoTranslateResponse(const QByteArray& data)
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
        // HTTP 200 with nothing usable in it. Several providers answer this way for quota and
        // content-policy conditions, so this is an ordinary failure, not a corrupt-server case.
        qWarning() << "Empty AI response for provider:" << provider
                   << "- treating this batch as FAILED, not as zero translations";
        return false;
    }

    // Extract JSON from response (AI might include markdown code blocks)
    qsizetype jsonStart = content.indexOf('{');
    qsizetype jsonEnd = content.lastIndexOf('}');
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

            // Reject a translation that dropped, added or renumbered a placeholder.
            //
            // "%1 frames" coming back as "Bilder" leaves QString::arg with nowhere to put the
            // number: the user sees a label with the value silently missing. Renumbering is
            // worse — "%2 of %1" against a source of "%1 of %2" swaps two values with no visible
            // damage at all, so it survives review and ships.
            //
            // Nothing downstream checks this. The string is applied, saved, and then UPLOADED to
            // the community copy for that language, so one bad completion becomes everyone's
            // bad completion. Models drop placeholders exactly when the surrounding text is
            // being restructured, which is most likely in the languages that need it most.
            if (!translation.isEmpty()
                && placeholderSet(translation) != placeholderSet(fallbackText)) {
                qWarning().noquote()
                    << "Rejecting AI translation - placeholders do not match. source="
                    << fallbackText << "translation=" << translation;
                m_autoTranslateRejected++;
                continue;
            }

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
        return true;
    }

    // The model answered, but not with the JSON object it was asked for — prose, a truncated
    // reply from hitting max_tokens, or an echo of the prompt. Nothing was applied.
    qWarning() << "Failed to parse AI translation response:" << content.left(200);
    return false;
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
    // The cached value must satisfy the placeholder rule too.
    //
    // m_aiTranslations is filled from two places: parseAutoTranslateResponse, which now checks,
    // and loadAiTranslations(), which reads a file written before the check existed. So every
    // _ai.json on disk today may still hold the placeholder-losing strings the rule was added to
    // stop — and this button pushes them straight into the main file, from where they upload.
    // That is the propagation loop the check exists to break, entered through a button rather
    // than a download.
    const QString sourceEnglish = m_stringRegistry.value(keys.isEmpty() ? QString() : keys.first());
    if (!sourceEnglish.isEmpty()
        && placeholderSet(aiTranslation) != placeholderSet(sourceEnglish)) {
        qWarning().noquote() << "Refusing to copy AI translation for" << fallback
                             << "- placeholders do not match. source=" << sourceEnglish
                             << "ai=" << aiTranslation;
        m_lastError = tr("That AI translation is missing a value placeholder, so it was not used.");
        emit lastErrorChanged();
        return;
    }

    // Snapshot for rollback: the edit paths were hardened to never display a change that was
    // not persisted, and this button — "use the AI translation" — is on the same page and just
    // as reachable. It was skipped in that pass.
    QMap<QString, QString> previous;
    QSet<QString> previouslyAi;
    for (const QString& key : keys) {
        if (m_translations.contains(key)) previous[key] = m_translations.value(key);
        if (m_aiGenerated.contains(key)) previouslyAi.insert(key);
    }

    for (const QString& key : keys) {
        m_translations[key] = aiTranslation;
        m_aiGenerated.insert(key);  // Mark as AI-generated
    }

    if (!saveTranslations()) {
        for (const QString& key : keys) {
            if (previous.contains(key)) m_translations[key] = previous.value(key);
            else m_translations.remove(key);
            if (!previouslyAi.contains(key)) m_aiGenerated.remove(key);
        }
        qWarning() << "Copy of the AI translation was NOT saved and has been rolled back";
        emit translationsChanged();
        return;
    }
    recalculateUntranslatedCount();
    m_translationVersion++;
    emit translationsChanged();
}

void TranslationManager::clearAiTranslation(const QString& fallback)
{
    // NOTE: saveAiTranslations() below is checked. setGroupTranslation() calls this on its
    // SUCCESS path, so a silently failed write leaves the main file saying "not AI generated"
    // while _ai.json still holds the old value — the two files disagree and nothing says so.

    if (!m_aiTranslations.contains(fallback)) return;

    m_aiTranslations.remove(fallback);

    // Also clear the AI-generated flag for all keys using this fallback
    QStringList keys = getKeysForFallback(fallback);
    for (const QString& key : keys) {
        m_aiGenerated.remove(key);
    }

    if (!saveAiTranslations())
        qWarning() << "AI translation cleared in memory but the AI file was NOT saved for"
                   << fallback << "- it will reappear on restart";
    m_translationVersion++;
    emit translationsChanged();
}

void TranslationManager::clearAllAiTranslations()
{
    if (m_currentLanguage == "en") return;

    qsizetype aiCacheCount = m_aiTranslations.size();
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
    // Verify the main file can be written BEFORE deleting the AI cache. This removed the file
    // first and then discarded saveTranslations()' verdict, so with a corrupt language file the
    // AI cache was destroyed irreversibly while the change it was part of never persisted — the
    // user saw the clear applied, restarted, and had the old translations back with the AI
    // column permanently empty.
    if (!saveTranslations()) {
        qWarning() << "Refusing to clear AI translations - the main file could not be saved";
        loadAiTranslations();   // restore the in-memory state we just cleared
        loadTranslations();
        emit translationsChanged();
        return;
    }
    QFile::remove(aiPath);
    // (No second save here: the guard above already persisted the fully-mutated state — the
    // map edits all happen before it — so a repeat write was pure redundancy.)
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

bool TranslationManager::saveAiTranslations()
{
    if (m_currentLanguage == "en") {
        return true;   // nothing to store for the base language; not a failure
    }

    QString aiPath = translationsDir() + "/" + m_currentLanguage + "_ai.json";

    if (m_aiTranslations.isEmpty()) {
        QFile::remove(aiPath);   // deliberate: an empty set is represented by absence
        return true;
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

    return writeJsonFile(aiPath, QJsonDocument(root), tr("the AI translations"));
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

bool TranslationManager::saveUserOverrides()
{
    if (m_currentLanguage == "en") {
        return true;   // English has no overrides file; not a failure
    }

    QString overridesPath = translationsDir() + "/" + m_currentLanguage + "_overrides.json";

    if (m_userOverrides.isEmpty()) {
        QFile::remove(overridesPath);   // deliberate: an empty set is represented by absence
        return true;
    }

    QJsonObject root;
    QJsonArray overrides;
    for (const QString& key : m_userOverrides) {
        overrides.append(key);
    }
    root["overrides"] = overrides;

    return writeJsonFile(overridesPath, QJsonDocument(root), tr("your customised strings"));
}

void TranslationManager::checkForLanguageUpdate()
{
    // Once per launch, matching the single-shot this replaced. Set before any early return so
    // that a connection which drops and comes back does not re-run it — reachabilityChanged
    // can fire repeatedly on a flapping link, which the fixed delay never had to survive.
    if (m_launchUpdateCheckDone) {
        return;
    }
    m_launchUpdateCheckDone = true;

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
    QString url = QString("%1/v1/translations/languages/%2").arg(TRANSLATION_API_BASE, m_currentLanguage);
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

        // Merge new translations, preserving user overrides. Tolerable discard: this is the
        // silent launch-time check — a refusal has already warned and set lastError, and there
        // is no operation in flight to abort; the user can Update manually from the language
        // page, where the refusal IS surfaced.
        (void)mergeLanguageUpdate(newTranslations);
    });
}

bool TranslationManager::mergeLanguageUpdate(const QJsonObject& newTranslations)
{
    // Refuse when the local file failed to LOAD. m_translations being empty is ambiguous —
    // it means either "nothing translated yet" or "I could not read what was there" — and
    // merging into the second case then saving replaces the user's translations with the
    // server's copy. That is the destructive replace this whole area exists to prevent, and it
    // reaches it by a different door than the one that was guarded: the file-level merge got a
    // read guard, but this in-memory path is the one both UI buttons actually take, because
    // they set currentLanguage to the language they are about to download.
    if (m_translationsLoadFailed) {
        // Retry the load before refusing. The flag is only recomputed inside loadTranslations(),
        // which runs from the constructor, a genuine language CHANGE, and import — none of which
        // fire when the user repairs the file and presses Update again, because
        // setCurrentLanguage(sameLanguage) is a no-op. Without this the refusal told the user to
        // "fix the file and retry" and then ignored them until an app restart, which is a worse
        // failure than the one it was guarding: advice that does not work.
        qWarning() << "Local" << m_currentLanguage << "file previously failed to load - retrying"
                   << "before deciding whether to merge";
        loadTranslations();
    }
    if (m_translationsLoadFailed) {
        qWarning() << "Refusing to merge into" << m_currentLanguage
                   << "- its local file still cannot be read, so an empty in-memory map is not"
                   << "evidence of an empty language. Repair or delete the file, then retry.";
        m_lastError = tr("The existing %1 file could not be read, so the update was not applied "
                         "(your local translations are untouched).").arg(m_currentLanguage);
        emit lastErrorChanged();
        return false;
    }

    int added = 0;
    int updated = 0;
    int preserved = 0;
    int skippedBadPlaceholders = 0;

    for (auto it = newTranslations.constBegin(); it != newTranslations.constEnd(); ++it) {
        const QString& key = it.key();
        const QString& newValue = it.value().toString();

        // Same placeholder rule as the AI path, for the same reason and with a wider blast
        // radius: this data comes from the community server, which anyone can upload to
        // unauthenticated. Without the check a single bad contribution propagates to every user
        // of that language on their next update.
        //
        // Not hypothetical. When this rule was first written it immediately found seven
        // already-shipped translations that had lost their placeholders, including a screen
        // reader label — "Background image %1 of %2" translated as just "Hintergrundbild", so a
        // VoiceOver user in German, French, Arabic or Danish heard no position at all and could
        // not navigate the list. That damage arrived through exactly this path.
        const QString source = m_stringRegistry.value(key);
        if (!source.isEmpty() && placeholderSet(newValue) != placeholderSet(source)) {
            qWarning().noquote() << "Skipping community translation for" << key
                                 << "- placeholders do not match. source=" << source
                                 << "incoming=" << newValue;
            skippedBadPlaceholders++;
            continue;
        }

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
        qDebug() << "Language update merged:" << added << "new," << updated << "updated,"
                 << preserved << "preserved user overrides,"
                 << skippedBadPlaceholders << "skipped for placeholder mismatch";
        // Honour the save. Returning true after a refused write defeats this function's own
        // caller, which was rewritten in this branch specifically to act on the bool — the
        // caller then writes metadata, emits languageDownloaded(true), and the UI offers to
        // AI-translate over data that was never persisted.
        if (!saveTranslations())
            return false;
        recalculateUntranslatedCount();
        m_translationVersion++;
        emit translationsChanged();
    } else {
        qDebug() << "Language is up to date";
    }
    return true;
}

// --- Batch Translate and Upload All Languages ---

// The model each provider falls back to when the user has not chosen one. Kept in step with
// aiprovider.cpp's catalogs by tst_aiproviders, which asserts each of these is that provider's
// FIRST catalog entry — the codebase's existing definition of "recommended".
//
// Deliberately a literal rather than a call into AIProvider: decenza_testlib compiles
// translationmanager.cpp but not the AI stack, so reaching for the catalog at runtime would
// drag the provider classes into forty-odd test targets to read one string. The test carries
// the coupling instead of the link line.
//
// This is what had gone stale. All three cloud providers hard-coded a model and ignored both
// the user's choice and the catalog: Anthropic on claude-3-5-haiku-20241022 (RETIRED
// 2026-02-19, so every Anthropic translation was 404ing), OpenAI on gpt-4o-mini, Gemini on
// gemini-2.0-flash — the latter two not even offered by the model picker. Ollama read its
// setting and never went stale, which is the argument for reading settings.
QString TranslationManager::fallbackTranslationModel(const QString& providerId)
{
    if (providerId == QLatin1String("openai"))    return QStringLiteral("gpt-5.4");
    if (providerId == QLatin1String("anthropic")) return QStringLiteral("claude-sonnet-4-6");
    if (providerId == QLatin1String("gemini"))    return QStringLiteral("gemini-2.5-flash");
    return {};   // ollama has no catalog — its model is user-supplied
}

// The model to translate with: whatever the user configured for this provider, else the
// fallback above. Translation is bulk (2400+ strings, 25 to a request), so a configured
// model can be expensive — that is the user's call to make, and a surprising bill is more
// visible than a silently wrong model.
QString TranslationManager::translationModelFor(const QString& provider,
                                                const QString& fallback) const
{
    const QString configured = m_settings->ai()->providerModel(provider).trimmed();
    if (!configured.isEmpty())
        return configured;
    const QString catalogued = fallbackTranslationModel(provider);
    return catalogued.isEmpty() ? fallback : catalogued;
}

// Human-readable name of the selected provider, for error text the user actually reads.
QString TranslationManager::selectedProviderLabel() const
{
    const QString id = m_settings->ai()->aiProvider();
    if (id == QLatin1String("anthropic")) return QStringLiteral("Claude");
    if (id == QLatin1String("openai"))    return QStringLiteral("OpenAI");
    if (id == QLatin1String("gemini"))    return QStringLiteral("Gemini");
    if (id == QLatin1String("ollama"))    return QStringLiteral("Ollama");
    return id.isEmpty() ? QStringLiteral("No AI provider") : id;
}

QStringList TranslationManager::getConfiguredProviders() const
{
    // ONLY the user's selected provider. No substitution, silent or otherwise.
    //
    // This used to hard-order "Claude first (best quality), then OpenAI" and fall through to
    // whatever else had a key. That fallback is exactly what hid a dead provider for months:
    // the Anthropic model ID had been retired, every Anthropic request 404'd, and the batch
    // quietly finished on OpenAI — so the only users who could notice were those with no
    // second key, who got nothing at all and no reason why.
    //
    // Substituting a provider the user did not choose is not resilience when it is silent.
    // A translation run that cannot use the configured provider now stops and says so.
    QStringList providers;
    const QString selected = m_settings->ai()->aiProvider();
    const bool configured =
        (selected == QLatin1String("anthropic") && !m_settings->ai()->anthropicApiKey().isEmpty())
        || (selected == QLatin1String("openai") && !m_settings->ai()->openaiApiKey().isEmpty())
        || (selected == QLatin1String("gemini") && !m_settings->ai()->geminiApiKey().isEmpty())
        || (selected == QLatin1String("ollama") && !m_settings->ai()->ollamaEndpoint().isEmpty());
    if (configured)
        providers << selected;
    // Gemini and Ollama are no longer "excluded": nothing is auto-discovered any more, so if
    // the user selected one, that is what runs.
    return providers;
}

QString TranslationManager::getActiveProvider() const
{
    // During batch processing, use the override provider (bypasses QSettings cache)
    if (m_batchProcessing && !m_batchCurrentProvider.isEmpty()) {
        return m_batchCurrentProvider;
    }
    // Otherwise use the normal settings value
    return m_settings->ai()->aiProvider();
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
        m_lastError = QStringLiteral("%1 is selected as the AI provider but is not configured. "
                                     "Add its API key in Settings, or select a provider that has one.")
                          .arg(selectedProviderLabel());
        emit lastErrorChanged();
        emit batchTranslateUploadFinished(false, m_lastError);
        return;
    }

    // Save original provider AND language to restore later — the batch switches both.
    m_originalProvider = m_settings->ai()->aiProvider();
    m_originalLanguage = m_currentLanguage;

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
    m_batchFailedUploads.clear();   // a previous run's failures must not be reported by this one
    qDebug() << "=== BATCH TRANSLATE+UPLOAD START ===";
    qDebug() << "Languages:" << allLanguages.size() << allLanguages;
    qDebug() << "AI Providers:" << m_batchProviderQueue.size() << m_batchProviderQueue;

    // Start with first provider, queue all languages for it
    QString firstProvider = m_batchProviderQueue.takeFirst();
    m_batchCurrentProvider = firstProvider;  // Bypass QSettings cache
    m_settings->ai()->setAiProvider(firstProvider);  // Still set for UI consistency
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
            // No provider queue to reset: the selected provider is the only one used, and a
            // failure with it stops the batch rather than moving on.
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
                // Nothing to translate — but that is NOT a reason to skip the upload, which is
                // what this branch used to do (the comment above it has always promised "or
                // just upload"; the branch was never written). Being fully translated locally
                // is exactly the state a language is in AFTER someone runs the AI pass, so the
                // languages most worth publishing were the ones silently passed over. Observed:
                // a batch uploaded ar and fr, both of which had gaps, and skipped de at 100%,
                // leaving the server on a copy 2200 strings poorer.
                qDebug() << "Batch:" << nextLang << "is fully translated — uploading as-is";
                submitTranslation();
            } else {
                qDebug() << "Batch:" << nextLang << "has" << untranslated << "untranslated strings, translating...";
                autoTranslate();
            }
        } else {
            // All done - restore original provider AND language, then clear batch state.
            //
            // The language was missing here: the batch switches currentLanguage per language
            // and only ever put the PROVIDER back, so it ended wherever the queue finished and
            // the user's UI silently changed under them. Restoring it is the same courtesy the
            // provider already got.
            m_batchCurrentProvider.clear();
            m_settings->ai()->setAiProvider(m_originalProvider);
            if (!m_originalLanguage.isEmpty() && m_originalLanguage != m_currentLanguage) {
                qDebug() << "Batch: restoring language to" << m_originalLanguage;
                setCurrentLanguage(m_originalLanguage);
            }
            m_batchProcessing = false;
            disconnect(*autoConn);
            disconnect(*submitConn);
            delete autoConn;
            delete submitConn;
            qDebug() << "=== BATCH TRANSLATE+UPLOAD COMPLETE ===";
            qDebug() << "Restored provider:" << m_originalProvider;

            // Report what actually reached the server, not merely that the queue drained.
            // Uploads fail for ordinary reasons — the hourly rate limit above all — and this
            // used to finish "complete" regardless, so a user could publish 10 of 12 languages
            // and be told all 12 went. Name the ones that did not.
            if (m_batchFailedUploads.isEmpty()) {
                emit batchTranslateUploadFinished(true, "Batch processing complete");
            } else {
                const QString failed = m_batchFailedUploads.join(QStringLiteral("; "));
                qWarning() << "Batch: uploads FAILED for" << failed;
                emit batchTranslateUploadFinished(
                    false, QStringLiteral("Uploaded all but %1 language(s). Failed: %2")
                               .arg(m_batchFailedUploads.size()).arg(failed));
            }
            m_batchFailedUploads.clear();
        }
    };

    *autoConn = connect(this, &TranslationManager::autoTranslateFinished, this, [this, processNext, autoConn, submitConn](bool success, const QString& message) {
        if (!m_batchProcessing) return;

        qDebug() << "Batch: autoTranslateFinished for" << m_currentLanguage
                 << "success:" << success << "message:" << message
                 << "provider:" << m_batchCurrentProvider;

        if (success) {
            // Check if there were actual translations made (not "all already translated")
            if (message.contains("already translated")) {
                // Same reasoning as the untranslated == 0 branch: nothing NEW was translated,
                // but the local set can still be far ahead of the server's, and this is the
                // only thing that would ever push it.
                qDebug() << "Batch: nothing new for" << m_currentLanguage << "— uploading as-is";
                submitTranslation();
            } else {
                // Translation done with changes, now upload
                qDebug() << "Batch: Uploading" << m_currentLanguage << "...";
                submitTranslation();
            }
        } else if (!m_autoTranslateFatal) {
            // This language failed, but the provider is fine — one reply came back as prose or
            // truncated JSON. Record it and carry on with the rest.
            //
            // Routing parse failures into autoTranslateFinished(false) without this made the
            // terminal branch below swallow the whole run: one unusable reply out of a
            // twelve-language batch abandoned the other eleven. The terminal branch was written
            // for "the provider is unusable and everything after it fails the same way", which
            // is true of a transport error and false of a bad completion.
            qWarning().noquote() << "Batch: translation of" << m_currentLanguage
                                 << "failed but the provider is usable — continuing." << message;
            m_batchFailedUploads << QStringLiteral("%1 (%2)").arg(m_currentLanguage, message);
            (*processNext)();
        } else {
            // Failure is terminal. Previously this walked to the next configured provider and,
            // once they were exhausted, moved to the next LANGUAGE — so a batch could report
            // "complete" having translated nothing, on a provider the user never picked.
            m_batchProcessing = false;
            m_batchCurrentProvider.clear();
            m_batchLanguageQueue.clear();
            m_batchProviderQueue.clear();
            m_settings->ai()->setAiProvider(m_originalProvider);
            if (!m_originalLanguage.isEmpty() && m_originalLanguage != m_currentLanguage)
                setCurrentLanguage(m_originalLanguage);
            m_lastError = QStringLiteral("Translation failed on %1 for %2: %3")
                              .arg(selectedProviderLabel(), m_currentLanguage, message);

            // Languages that were translated but never reached the server must still be named.
            // Reporting only the translation failure repeats, on this branch, exactly the
            // "credit for work not done" bug the upload accounting was added to fix — and this
            // is the branch MORE likely to run, since translation and upload draw on the same
            // hourly quota and fail together.
            if (!m_batchFailedUploads.isEmpty()) {
                m_lastError += QStringLiteral(" Uploads had already failed for: %1.")
                                   .arg(m_batchFailedUploads.join(QStringLiteral("; ")));
                m_batchFailedUploads.clear();
            }
            emit lastErrorChanged();
            qWarning().noquote() << "Batch:" << m_lastError
                                 << "— stopping. The selected provider is the only one used;"
                                 << "nothing was silently retried elsewhere.";

            // Tear down the same connections the completion path does. Without this the
            // lambdas stay attached to `this`: the NEXT batch sets m_batchProcessing back to
            // true, the stale handlers pass their guard and run alongside the fresh ones, and
            // processNext() fires twice per completion — taking two languages off the queue
            // each time and silently skipping about half of them. This terminal branch did not
            // exist before this change, so the leak is new to it.
            disconnect(*autoConn);
            disconnect(*submitConn);
            delete autoConn;
            delete submitConn;

            emit batchTranslateUploadFinished(false, m_lastError);
            return;
        }
    });

    *submitConn = connect(this, &TranslationManager::translationSubmitted, this, [this, processNext](bool success, const QString& message) {
        if (!m_batchProcessing) return;

        qDebug() << "Batch: Upload" << (success ? "SUCCEEDED" : "FAILED")
                 << "for" << m_currentLanguage << "-" << message;

        // Record the failure rather than only logging it. A failed upload is not a reason to
        // abandon the remaining languages — unlike a translation failure, which means the
        // provider is unusable and everything after it would fail the same way — so the batch
        // continues, but the final result must name what did not make it.
        if (!success)
            m_batchFailedUploads << QStringLiteral("%1 (%2)").arg(m_currentLanguage, message);

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
        qDebug() << "Batch:" << firstLang << "is fully translated — uploading as-is";
        submitTranslation();
    } else {
        qDebug() << "Batch:" << firstLang << "has" << untranslated << "untranslated strings, translating...";
        autoTranslate();
    }
}
