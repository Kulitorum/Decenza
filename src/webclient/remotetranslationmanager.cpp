#include "remotetranslationmanager.h"

#include "core/translationcatalog.h"
#include "remote/servicepayload.h"
#include "remote/servicevalue.h"

#include <QJSEngine>
#include <QQmlEngine>
#include <cmath>
#include <limits>
#include <utility>

namespace {
bool catalogRevision(const QVariant& value, qint64* revision)
{
    double number = -1;
    if (!ServiceValue::finiteNumber(value, &number) || number < 0
        || std::floor(number) != number
        || number > static_cast<double>(TranslationCatalog::MaximumExactJsonInteger))
        return false;
    *revision = static_cast<qint64>(number);
    return true;
}
}

RemoteTranslationManager::RemoteTranslationManager(QObject* parent) : QObject(parent) {}

RemoteTranslationManager* RemoteTranslationManager::create(QQmlEngine* engine, QJSEngine* jsEngine)
{
    auto* manager = new RemoteTranslationManager(engine);
    manager->m_jsEngine = jsEngine;
    QJSEngine::setObjectOwnership(manager, QJSEngine::CppOwnership);
    Q_ASSERT(manager->translateFn().isCallable());
    return manager;
}

void RemoteTranslationManager::setLanguageService(LanguageService* service)
{
    if (m_languageService == service)
        return;
    if (m_languageService)
        disconnect(m_languageService, nullptr, this, nullptr);
    m_languageService = service;
    m_requestedRevision = -1;
    m_pendingRequestId.clear();
    clearCatalog(service ? QStringLiteral("waiting") : QStringLiteral("disconnected"));
    if (service) {
        connect(service, &LanguageService::stateChanged, this, &RemoteTranslationManager::updateForService);
        connect(service, &LanguageService::operationCompleted, this, &RemoteTranslationManager::completePage);
        connect(service, &QObject::destroyed, this, [this] {
            m_languageService = nullptr;
            m_requestedRevision = -1;
            m_pendingRequestId.clear();
            clearCatalog(QStringLiteral("disconnected"));
            emit languageServiceChanged();
        });
    }
    emit languageServiceChanged();
    updateForService();
}

void RemoteTranslationManager::updateForService()
{
    if (!m_languageService || !m_languageService->ready()) {
        m_requestedRevision = -1;
        m_pendingRequestId.clear();
        clearCatalog(m_languageService ? QStringLiteral("waiting") : QStringLiteral("disconnected"));
        return;
    }
    const QVariantMap snapshot = m_languageService->snapshot();
    qint64 revision = -1;
    const QString language = snapshot.value(QStringLiteral("currentLanguage")).toString();
    if (!catalogRevision(snapshot.value(QStringLiteral("catalogRevision")), &revision)
        || language.isEmpty() || language.size() > TranslationCatalog::MaximumLanguageLength) {
        m_requestedRevision = -1;
        m_pendingRequestId.clear();
        clearCatalog(QStringLiteral("incompatible"));
        return;
    }
    if (m_requestedRevision == revision && m_requestedLanguage == language)
        return;
    m_requestedRevision = revision;
    m_requestedLanguage = language;
    m_pendingRequestId.clear();
    clearCatalog(QStringLiteral("loading"));
    requestPage();
}

void RemoteTranslationManager::refreshCatalog()
{
    m_requestedRevision = -1;
    updateForService();
}

void RemoteTranslationManager::requestPage()
{
    m_pendingRequestId = m_languageService->requestCatalogPage(m_requestedRevision,
        static_cast<int>(m_pendingTranslations.size())); // Validated against MaximumEntries.
}

bool RemoteTranslationManager::appendPage(const QVariantMap& result)
{
    qint64 revision = -1;
    int offset = -1;
    int total = -1;
    if (!catalogRevision(result.value(QStringLiteral("revision")), &revision)
        || revision != m_requestedRevision
        || result.value(QStringLiteral("language")).metaType().id() != QMetaType::QString
        || result.value(QStringLiteral("language")).toString() != m_requestedLanguage
        || !ServiceValue::boundedInteger(result.value(QStringLiteral("offset")), 0,
                                         TranslationCatalog::MaximumEntries, &offset)
        || offset != m_pendingTranslations.size()
        || !ServiceValue::boundedInteger(result.value(QStringLiteral("total")), 0,
                                         TranslationCatalog::MaximumEntries, &total)
        || (m_expectedTotal >= 0 && total != m_expectedTotal)
        || result.value(QStringLiteral("rows")).metaType().id() != QMetaType::QVariantList
        || !ServicePayload::fits(result, 512 * 1024))
        return false;
    const QVariantList rows = result.value(QStringLiteral("rows")).toList();
    if (rows.size() > 128 || rows.size() + offset > total
        || (rows.isEmpty() && offset < total))
        return false;
    m_expectedTotal = total;
    for (const QVariant& value : rows) {
        if (value.metaType().id() != QMetaType::QVariantMap)
            return false;
        const QVariantMap row = value.toMap();
        const QVariant keyValue = row.value(QStringLiteral("key"));
        const QVariant fallbackValue = row.value(QStringLiteral("fallback"));
        const QVariant translationValue = row.value(QStringLiteral("translation"));
        if (keyValue.metaType().id() != QMetaType::QString
            || fallbackValue.metaType().id() != QMetaType::QString
            || translationValue.metaType().id() != QMetaType::QString)
            return false;
        const QString key = keyValue.toString();
        const QString fallback = fallbackValue.toString();
        const QString translation = translationValue.toString();
        if (key.size() > TranslationCatalog::MaximumKeyLength
            || fallback.size() > TranslationCatalog::MaximumTextLength
            || translation.size() > TranslationCatalog::MaximumTextLength
            || m_pendingTranslations.contains(key))
            return false;
        m_pendingTextUnits += key.size() + fallback.size() + translation.size();
        if (m_pendingTextUnits > TranslationCatalog::MaximumCombinedTextUnits)
            return false;
        m_pendingFallbacks.insert(key, fallback);
        m_pendingTranslations.insert(key, translation);
    }
    return true;
}

void RemoteTranslationManager::completePage(const QString& requestId, const QString& operation,
                                           bool success, const QVariantMap& result,
                                           const QString& reason)
{
    if (requestId != m_pendingRequestId || operation != QLatin1String("requestCatalogPage"))
        return;
    m_pendingRequestId.clear();
    if (!success || !appendPage(result)) {
        clearCatalog(success ? QStringLiteral("incompatible") : reason);
        return;
    }
    if (m_pendingTranslations.size() < m_expectedTotal) {
        requestPage();
        return;
    }
    // Use the same total-catalog limits as the host exporter before publishing
    // any strings. A malformed or interrupted page sequence never becomes usable.
    const auto catalog = TranslationCatalog::exportSnapshot(m_requestedLanguage, m_requestedRevision,
                                                            m_pendingFallbacks, m_pendingTranslations);
    if (!catalog.isValid()) {
        clearCatalog(catalog.error);
        return;
    }
    m_translations = std::move(m_pendingTranslations);
    m_pendingFallbacks.clear();
    m_language = m_requestedLanguage;
    m_catalogReady = true;
    setCatalogStatus(QStringLiteral("ready"));
    bumpTranslationVersion();
    emit translationsChanged();
}

QString RemoteTranslationManager::translateString(const QString& key, const QString& fallback) const
{
    if (key.trimmed().isEmpty() || fallback.trimmed().isEmpty())
        return fallback;
    return TranslationCatalog::resolve(fallback, m_translations.value(key));
}

bool RemoteTranslationManager::hasTranslation(const QString& key) const
{
    return !m_translations.value(key).isEmpty();
}

QJSValue RemoteTranslationManager::translateFn()
{
    if (m_jsEngine && (m_translateFn.isUndefined() || m_translateFn.isNull()))
        m_translateFn = m_jsEngine->newQObject(this).property(QStringLiteral("translateString"));
    return m_translateFn;
}

void RemoteTranslationManager::clearCatalog(const QString& status)
{
    const bool hadCatalog = m_catalogReady || !m_translations.isEmpty() || !m_language.isEmpty();
    m_translations.clear();
    m_pendingTranslations.clear();
    m_pendingFallbacks.clear();
    m_pendingTextUnits = 0;
    m_expectedTotal = -1;
    m_language.clear();
    m_catalogReady = false;
    if (hadCatalog)
        bumpTranslationVersion();
    setCatalogStatus(status);
    if (hadCatalog)
        emit translationsChanged();
}

void RemoteTranslationManager::setCatalogStatus(const QString& status)
{
    if (m_catalogStatus == status)
        return;
    m_catalogStatus = status;
    emit catalogStatusChanged();
}

void RemoteTranslationManager::bumpTranslationVersion()
{
    if (m_translationVersion < std::numeric_limits<qint64>::max())
        ++m_translationVersion;
}
