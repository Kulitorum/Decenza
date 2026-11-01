#include "languageservicesource.h"
#include "languageoperationpolicy.h"
#include "importservicesource.h"

#include "core/translationmanager.h"
#include "core/diagnosticlogging.h"
#include "core/translationcatalog.h"
#include "servicepayload.h"

#include <QMetaType>
#include <QDir>
#include <QThread>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <QJsonObject>
#include <cmath>
#include <QMetaObject>
#include <QUuid>
#include <QStringList>

#include <limits>
#include <memory>
#include <utility>

#include "servicevalue.h"

namespace {
QString reasonForStart(ServiceRequestIds::Start start)
{
    switch (start) {
    case ServiceRequestIds::Start::InvalidId: return QStringLiteral("invalidRequest");
    case ServiceRequestIds::Start::DuplicateId: return QStringLiteral("duplicateRequest");
    case ServiceRequestIds::Start::LimitReached: return QStringLiteral("busy");
    case ServiceRequestIds::Start::Accepted: break;
    }
    return {};
}

constexpr int kMaximumPageSize = 50;
constexpr qsizetype kMaximumPageBytes = 512 * 1024;


} // namespace

NativeLanguageServiceSource::NativeLanguageServiceSource(TranslationManager* manager,
                                                         QObject* parent)
    : LanguageStateSimpleSource(parent), m_manager(manager),
      m_sourceId(QUuid::createUuid().toString(QUuid::WithoutBraces))
{
    if (!manager) {
        setSnapshot({});
        setReady(false);
        return;
    }
    connect(manager, &TranslationManager::currentLanguageChanged, this, [this] {
        if (m_applyingPreparedFile) return;
        const QString previousLanguage = m_rowCurrentLanguage;
        rebuildLanguageRows(previousLanguage);
        publishSnapshot();
    });
    connect(manager, &TranslationManager::availableLanguagesChanged, this, [this] {
        if (m_applyingPreparedFile) return;
        rebuildLanguageRows();
        publishSnapshot();
    });
    connect(manager, &TranslationManager::translationsChanged, this, [this] {
        if (m_applyingPreparedFile) return;
        m_uniqueCountsCached = false;
        // The active language's percentage is cached so ordinary translation edits
        // do not reopen every installed language file on each keystroke.
        if (m_manager && !m_languageRows.isEmpty()) {
            const QString current = m_manager->currentLanguage();
            for (QVariant& rowValue : m_languageRows) {
                QVariantMap row = rowValue.toMap();
                if (row.value(QStringLiteral("code")).toString() == current) {
                    row.insert(QStringLiteral("percent"), m_manager->getTranslationPercent(current));
                    rowValue = row;
                    break;
                }
            }
        }
        publishSnapshot();
    });
    connect(manager, &TranslationManager::catalogRevisionChanged, this, [this] {
        if (m_applyingPreparedFile) return;
        m_uniqueCountsCached = false;
        queueSnapshotPublication();
    });
    connect(manager, &TranslationManager::untranslatedCountChanged, this, [this] {
        if (m_applyingPreparedFile) return;
        m_uniqueCountsCached = false;
        const QString current = m_manager->currentLanguage();
        for (QVariant& value : m_languageRows) {
            QVariantMap row = value.toMap();
            if (row.value(QStringLiteral("code")).toString() == current) {
                row.insert(QStringLiteral("percent"), m_manager->getTranslationPercent(current));
                value = row;
                break;
            }
        }
        queueSnapshotPublication();
    });
    connect(manager, &TranslationManager::totalStringCountChanged, this, [this] {
        if (m_applyingPreparedFile) return;
        m_uniqueCountsCached = false;
        queueSnapshotPublication();
    });
    connect(manager, &TranslationManager::downloadingChanged, this, [this] { publishSnapshot(); });
    connect(manager, &TranslationManager::uploadingChanged, this, [this] { publishSnapshot(); });
    connect(manager, &TranslationManager::retryStatusChanged, this, [this] { publishSnapshot(); });
    connect(manager, &TranslationManager::scanningChanged, this, [this] { publishSnapshot(); });
    connect(manager, &TranslationManager::scanProgressChanged, this, [this] {
        queueSnapshotPublication();
    });
    connect(manager, &TranslationManager::autoTranslatingChanged, this, [this] { publishSnapshot(); });
    connect(manager, &TranslationManager::autoTranslateProgressChanged, this, [this] {
        queueSnapshotPublication();
    });
    connect(manager, &TranslationManager::lastTranslatedTextChanged, this, [this] { publishSnapshot(); });
    connect(manager, &TranslationManager::languageListDownloaded, this, [this](bool success) {
        rebuildLanguageRows();
        completeAsync(QStringLiteral("languageListDownloaded"), {}, success,
                      success ? QString() : QStringLiteral("downloadFailed"));
    });
    connect(manager, &TranslationManager::languageDownloaded, this,
            [this](const QString& code, bool success, const QString&) {
        rebuildLanguageRows(code);
        completeAsync(QStringLiteral("languageDownloaded"), code, success,
                      success ? QString() : QStringLiteral("downloadFailed"),
                      {{QStringLiteral("code"), code}});
    });
    connect(manager, &TranslationManager::translationSubmitted, this,
            [this](bool success, const QString&) {
        completeAsync(QStringLiteral("translationSubmitted"), {}, success,
                      success ? QString() : QStringLiteral("submissionFailed"));
    });
    connect(manager, &TranslationManager::scanFinished, this, [this](int count) {
        m_uniqueCountsCached = false;
        completeAsync(QStringLiteral("scanFinished"), {}, true, {},
                      {{QStringLiteral("stringsFound"), count}});
    });
    connect(manager, &TranslationManager::autoTranslateFinished, this,
            [this](bool success, const QString&) {
        completeAsync(QStringLiteral("autoTranslateFinished"), {}, success,
                      success ? QString() : m_cancelRequested ? QStringLiteral("cancelled")
                                                             : QStringLiteral("translationFailed"));
        m_cancelRequested = false;
    });
    connect(manager, &TranslationManager::batchTranslateUploadFinished, this,
            [this](bool success, const QString&) {
        m_batchTranslateUploadActive = false;
        completeAsync(QStringLiteral("batchTranslateUploadFinished"), {}, success,
                      success ? QString() : QStringLiteral("batchFailed"));
        publishSnapshot();
    });
    connect(manager, &QObject::destroyed, this, [this] {
        m_manager = nullptr;
        setReady(false);
        setSnapshot({});
    });
    rebuildLanguageRows();
    publishSnapshot();
}

void NativeLanguageServiceSource::setImportService(NativeImportServiceSource* imports)
{
    m_imports = imports;
}

void NativeLanguageServiceSource::setFileOperationAllowed(std::function<bool()> allowed)
{
    m_fileOperationAllowed = std::move(allowed);
}

void NativeLanguageServiceSource::refresh()
{
    publishSnapshot();
}

void NativeLanguageServiceSource::queueSnapshotPublication()
{
    if (m_snapshotPublishQueued)
        return;
    m_snapshotPublishQueued = true;
    QMetaObject::invokeMethod(this, [this] {
        m_snapshotPublishQueued = false;
        publishSnapshot();
    }, Qt::QueuedConnection);
}

void NativeLanguageServiceSource::publishSnapshot()
{
    if (m_applyingPreparedFile) return;
    auto* manager = m_manager.data();
    if (!manager) {
        setReady(false);
        setSnapshot({});
        return;
    }

    if (!m_uniqueCountsCached) {
        m_uniqueStringCount = manager->uniqueStringCount();
        m_uniqueUntranslatedCount = manager->uniqueUntranslatedCount();
        m_uniqueCountsCached = true;
    }

    setSnapshot(QVariantMap{
        {QStringLiteral("currentLanguage"), manager->currentLanguage()},
        {QStringLiteral("sourceId"), m_sourceId},
        {QStringLiteral("languages"), m_languageRows},
        {QStringLiteral("untranslatedCount"), manager->untranslatedCount()},
        {QStringLiteral("totalStringCount"), manager->totalStringCount()},
        {QStringLiteral("uniqueStringCount"), m_uniqueStringCount},
        {QStringLiteral("uniqueUntranslatedCount"), m_uniqueUntranslatedCount},
        {QStringLiteral("downloading"), manager->isDownloading()},
        {QStringLiteral("uploading"), manager->isUploading()},
        {QStringLiteral("retryStatus"), manager->retryStatus()},
        {QStringLiteral("scanning"), manager->isScanning()},
        {QStringLiteral("scanProgress"), manager->scanProgress()},
        {QStringLiteral("scanTotal"), manager->scanTotal()},
        {QStringLiteral("autoTranslating"), manager->isAutoTranslating()},
        {QStringLiteral("batchTranslateUploadActive"),
            m_batchTranslateUploadActive || manager->isBatchTranslateUploadActive()},
        {QStringLiteral("autoTranslateProgress"), manager->autoTranslateProgress()},
        {QStringLiteral("autoTranslateTotal"), manager->autoTranslateTotal()},
        {QStringLiteral("lastTranslatedText"), manager->lastTranslatedText()},
        {QStringLiteral("canAutoTranslate"), manager->canAutoTranslate()},
        {QStringLiteral("catalogRevision"), QVariant::fromValue<qlonglong>(manager->catalogRevision())},
        {QStringLiteral("translationVersion"), manager->translationVersion()}});
    setReady(true);
}

void NativeLanguageServiceSource::rebuildLanguageRows(const QString& refreshPercentFor)
{
    if (!m_manager) {
        m_languageRows.clear();
        return;
    }
    QHash<QString, int> cachedPercents;
    for (const QVariant& value : m_languageRows) {
        const QVariantMap row = value.toMap();
        cachedPercents.insert(row.value(QStringLiteral("code")).toString(),
                              row.value(QStringLiteral("percent")).toInt());
    }
    m_languageRows.clear();
    const QStringList codes = m_manager->availableLanguages();
    m_languageRows.reserve(codes.size());
    const QString currentLanguage = m_manager->currentLanguage();
    for (const QString& code : codes) {
        const bool refreshPercent = code == currentLanguage || code == refreshPercentFor
            || !cachedPercents.contains(code);
        m_languageRows.append(QVariantMap{
            {QStringLiteral("code"), code},
            {QStringLiteral("displayName"), m_manager->getLanguageDisplayName(code)},
            {QStringLiteral("nativeName"), m_manager->getLanguageNativeName(code)},
            {QStringLiteral("isRemote"), m_manager->isRemoteLanguage(code)},
            {QStringLiteral("communityAvailable"), m_manager->communityLanguageAvailable(code)},
            {QStringLiteral("percent"), refreshPercent
                 ? m_manager->getTranslationPercent(code) : cachedPercents.value(code)}});
    }
    m_rowCurrentLanguage = currentLanguage;
}


bool NativeLanguageServiceSource::boundedString(const QVariantMap& arguments,
                                                const QString& key, int maximum,
                                                QString* result) const
{
    const QVariant value = arguments.value(key);
    if (value.metaType().id() != QMetaType::QString)
        return false;
    *result = value.toString();
    return result->size() <= maximum;
}

QVariantList NativeLanguageServiceSource::boundedStringPage(int offset, int limit,
                                                            QString* error) const
{
    if (!m_manager || offset < 0 || limit < 1 || limit > kMaximumPageSize)
        return {};
    const QVariantList rows = m_manager->getGroupedStringsPage(offset, limit);
    qsizetype bytes = 0;
    for (const QVariant& value : rows) {
        const QVariantMap row = value.toMap();
        for (const QString& key : {QStringLiteral("key"), QStringLiteral("fallback"),
                                   QStringLiteral("translation"), QStringLiteral("aiTranslation")}) {
            const QString text = row.value(key).toString();
            bytes += text.toUtf8().size();
            if (bytes > kMaximumPageBytes) {
                *error = QStringLiteral("pageTooLarge");
                return {};
            }
        }
    }
    return rows;
}

QVariantMap NativeLanguageServiceSource::catalogPage(qint64 revision, int offset, QString* error)
{
    if (revision != static_cast<qint64>(m_manager->catalogRevision())) {
        *error = QStringLiteral("staleRevision");
        return {};
    }
    if (m_catalogRevision != revision) {
        // Reuse the canonical bounded exporter once per revision, rather than
        // rebuilding the whole catalog for each browser page.
        const QJsonObject catalog = m_manager->exportTranslationCatalog(error);
        if (!error->isEmpty())
            return {};
        const QJsonObject entries = catalog.value(QStringLiteral("entries")).toObject();
        m_catalogRows.clear();
        m_catalogRows.reserve(entries.size());
        for (auto it = entries.constBegin(); it != entries.constEnd(); ++it) {
            QVariantMap row = it.value().toObject().toVariantMap();
            row.insert(QStringLiteral("key"), it.key());
            m_catalogRows.append(row);
        }
        m_catalogLanguage = catalog.value(QStringLiteral("language")).toString();
        m_catalogRevision = revision;
    }
    if (offset > m_catalogRows.size()) {
        *error = QStringLiteral("invalidOffset");
        return {};
    }
    QVariantMap result{{QStringLiteral("revision"), revision},
                       {QStringLiteral("language"), m_catalogLanguage},
                       {QStringLiteral("offset"), offset},
                       {QStringLiteral("total"), m_catalogRows.size()}};
    QVariantList rows;
    result.insert(QStringLiteral("rows"), rows);
    qint64 bytes = ServicePayload::serializedSize(result, kMaximumPageBytes);
    constexpr int maximumRows = 128;
    for (qsizetype i = offset; i < m_catalogRows.size() && rows.size() < maximumRows; ++i) {
        const QVariant& row = m_catalogRows.at(i);
        const qint64 rowBytes = ServicePayload::serializedSize(row, kMaximumPageBytes - bytes);
        if (rowBytes < 0)
            break;
        bytes += rowBytes;
        rows.append(row);
    }
    result.insert(QStringLiteral("rows"), rows);
    if ((rows.isEmpty() && offset < m_catalogRows.size())
        || !ServicePayload::fits(result, kMaximumPageBytes)) {
        *error = QStringLiteral("pageTooLarge");
        return {};
    }
    return result;
}

void NativeLanguageServiceSource::completeAsync(const QString& event,
                                                const QString& identity,
                                                bool success, const QString& reason,
                                                const QVariantMap& result)
{
    const QString key = event + QLatin1Char(':') + identity;
    const QString requestId = m_asyncRequestByCode.take(key);
    if (requestId.isEmpty())
        return;
    const QString operation = m_asyncOperationByRequest.take(requestId);
    m_requests.finish(requestId);
    publishSnapshot();
    emit operationCompleted(requestId, operation, success, result, reason);
}

void NativeLanguageServiceSource::finish(const QString& requestId,
                                         const QString& operation, bool success,
                                         const QVariantMap& result,
                                         const QString& reason)
{
    if (!m_requests.finish(requestId))
        return;
    if (!success)
        DIAG_DEBUG(APP, "LanguageService") << "Operation refused:" << operation << reason;
    publishSnapshot();
    emit operationCompleted(requestId, operation, success, result, reason);
}

void NativeLanguageServiceSource::completeFileOperation(const QString& requestId,
    const QString& operation, bool success, const QVariantMap& result, const QString& reason)
{
    if (m_activeFileRequest != requestId) return;
    m_fileWorker = nullptr;
    m_activeFileRequest.clear();
    m_activeFileOperation.clear();
    m_fileCancelled = false;
    finish(requestId, operation, success, result, reason);
}

QString NativeLanguageServiceSource::stringContextError(const QVariantMap& arguments) const
{
    QString language;
    QString sourceId;
    double revision = -1;
    if (!boundedString(arguments, QStringLiteral("sourceId"), 64, &sourceId)
        || !boundedString(arguments, QStringLiteral("language"), 64, &language)
        || !ServiceValue::finiteNumber(arguments.value(QStringLiteral("revision")), &revision)
        || revision < 0 || std::floor(revision) != revision
        || revision > static_cast<double>(TranslationCatalog::MaximumExactJsonInteger))
        return QStringLiteral("invalidArguments");
    if (sourceId != m_sourceId || language != m_manager->currentLanguage()
        || static_cast<quint64>(revision) != m_manager->catalogRevision())
        return QStringLiteral("staleContext");
    return {};
}

void NativeLanguageServiceSource::execute(QString requestId, QString operation,
                                          QVariantMap arguments)
{
    const ServiceRequestIds::Start start = m_requests.begin(requestId);
    if (start != ServiceRequestIds::Start::Accepted) {
        // The duplicate ID belongs to the original in-flight command. Emitting a
        // second completion with that ID would make its client forget the first.
        if (start == ServiceRequestIds::Start::DuplicateId)
            return;
        emit operationCompleted(requestId, operation, false, {}, reasonForStart(start));
        return;
    }
    auto* manager = m_manager.data();
    if (!manager) {
        finish(requestId, operation, false, {}, QStringLiteral("unavailable"));
        return;
    }
    const auto async = [this, &requestId, &operation](const QString& event,
                                                      const QString& identity = {}) {
        const QString key = event + QLatin1Char(':') + identity;
        if (m_asyncRequestByCode.contains(key)) {
            finish(requestId, operation, false, {}, QStringLiteral("busy"));
            return false;
        }
        m_asyncRequestByCode.insert(key, requestId);
        m_asyncOperationByRequest.insert(requestId, operation);
        return true;
    };
    const bool contextOperation = operation == QLatin1String("requestStrings")
        || operation == QLatin1String("setGroupTranslation")
        || operation == QLatin1String("clearAiTranslation")
        || operation == QLatin1String("copyAiToFinal")
        || operation == QLatin1String("clearAllAiTranslations")
        || operation == QLatin1String("autoTranslate")
        || operation == QLatin1String("retranslateAll")
        || operation == QLatin1String("importTranslation")
        || operation == QLatin1String("exportTranslation");
    if (contextOperation) {
        const QString contextError = stringContextError(arguments);
        if (!contextError.isEmpty()) {
            finish(requestId, operation, false, {}, contextError);
            return;
        }
    }
    if (manager->isAutoTranslating()
        && (operation == QLatin1String("setGroupTranslation")
            || operation == QLatin1String("clearAiTranslation")
            || operation == QLatin1String("copyAiToFinal")
            || operation == QLatin1String("clearAllAiTranslations")
            || operation == QLatin1String("importTranslation")
            || operation == QLatin1String("downloadLanguage"))) {
        finish(requestId, operation, false, {}, QStringLiteral("busy"));
        return;
    }
    if ((operation == QLatin1String("importTranslation") || operation == QLatin1String("exportTranslation"))
        && (!m_fileOperationAllowed || !m_fileOperationAllowed())) {
        finish(requestId, operation, false, {}, QStringLiteral("machineBusy"));
        return;
    }
    if ((operation == QLatin1String("importTranslation") || operation == QLatin1String("exportTranslation"))
        && !m_activeFileRequest.isEmpty()) {
        finish(requestId, operation, false, {}, QStringLiteral("busy"));
        return;
    }
    QString code;
    QString displayName;
    QString nativeName;
    QString fallback;
    QString translation;
    if (operation == QLatin1String("refresh")) {
        if (!arguments.isEmpty()) { finish(requestId, operation, false, {}, QStringLiteral("invalidArguments")); return; }
        refresh();
        finish(requestId, operation, true);
    } else if (operation == QLatin1String("scanStrings")) {
        if (!arguments.isEmpty()) { finish(requestId, operation, false, {}, QStringLiteral("invalidArguments")); return; }
        if (manager->isScanning()) { finish(requestId, operation, false, {}, QStringLiteral("busy")); return; }
        if (!async(QStringLiteral("scanFinished"))) return;
        manager->scanAllStrings();
        publishSnapshot();
    } else if (operation == QLatin1String("refreshLanguageList")) {
        if (!arguments.isEmpty()) { finish(requestId, operation, false, {}, QStringLiteral("invalidArguments")); return; }
        if (manager->isDownloading()) { finish(requestId, operation, false, {}, QStringLiteral("busy")); return; }
        if (!async(QStringLiteral("languageListDownloaded"))) return;
        manager->downloadLanguageList();
        publishSnapshot();
    } else if (operation == QLatin1String("selectLanguage")) {
        if (!ServiceValue::hasExactKeys(arguments, {QStringLiteral("code")})
            || !boundedString(arguments, QStringLiteral("code"), 64, &code)
            || !manager->availableLanguages().contains(code)) {
            finish(requestId, operation, false, {}, QStringLiteral("invalidLanguage")); return;
        }
        if (manager->isDownloading() || manager->isAutoTranslating()) {
            finish(requestId, operation, false, {}, QStringLiteral("busy")); return;
        }
        manager->setCurrentLanguage(code);
        if (manager->needsCommunityDownload(code)) {
            // Selecting a community-only language is immediately reflected in the
            // host, then the canonical download must finish before this request.
            if (!async(QStringLiteral("languageDownloaded"), code)) return;
            manager->downloadLanguage(code);
        } else {
            finish(requestId, operation, true, {{QStringLiteral("code"), code}});
        }
    } else if (operation == QLatin1String("downloadLanguage")) {
        if (!ServiceValue::hasExactKeys(arguments, {QStringLiteral("code")})
            || !boundedString(arguments, QStringLiteral("code"), 64, &code)
            || !TranslationManager::isValidLanguageCode(code) || code == QLatin1String("en")
            || manager->isRemoteLanguage(code) || !manager->communityLanguageAvailable(code)
            || manager->isDownloading()) {
            finish(requestId, operation, false, {}, QStringLiteral("invalidLanguageOrBusy")); return;
        }
        if (!async(QStringLiteral("languageDownloaded"), code)) return;
        manager->downloadLanguage(code);
        publishSnapshot();
    } else if (operation == QLatin1String("addLanguage")) {
        if (!ServiceValue::hasExactKeys(arguments, {QStringLiteral("code"), QStringLiteral("displayName"), QStringLiteral("nativeName")})
            || !boundedString(arguments, QStringLiteral("code"), 64, &code)
            || !boundedString(arguments, QStringLiteral("displayName"), 128, &displayName)
            || !boundedString(arguments, QStringLiteral("nativeName"), 128, &nativeName)
            || !TranslationManager::isValidLanguageCode(code) || displayName.trimmed().isEmpty()
            || manager->availableLanguages().contains(code)) {
            finish(requestId, operation, false, {}, QStringLiteral("invalidLanguage")); return;
        }
        const bool added = manager->addLanguage(code, displayName, nativeName);
        finish(requestId, operation, added, {}, added ? QString() : QStringLiteral("writeFailed"));
    } else if (operation == QLatin1String("deleteLanguage")) {
        if (!ServiceValue::hasExactKeys(arguments, {QStringLiteral("code")})
            || !boundedString(arguments, QStringLiteral("code"), 64, &code)
            || code == QLatin1String("en") || code != manager->currentLanguage()) {
            finish(requestId, operation, false, {}, QStringLiteral("invalidLanguage")); return;
        }
        const bool removed = manager->deleteLanguage(code);
        finish(requestId, operation, removed, {}, removed ? QString() : QStringLiteral("writeFailed"));
    } else if (operation == QLatin1String("submitTranslation")) {
        if (!arguments.isEmpty()) { finish(requestId, operation, false, {}, QStringLiteral("invalidArguments")); return; }
        if (manager->isUploading()) { finish(requestId, operation, false, {}, QStringLiteral("busy")); return; }
        if (!async(QStringLiteral("translationSubmitted"))) return;
        manager->submitTranslation();
        publishSnapshot();
    } else if (operation == QLatin1String("importTranslation")) {
        QString transferId;
        if (!ServiceValue::hasExactKeys(arguments, {QStringLiteral("transferId"), QStringLiteral("sourceId"),
                                  QStringLiteral("language"), QStringLiteral("revision")})
            || !boundedString(arguments, QStringLiteral("transferId"), 128, &transferId)
            || transferId.isEmpty()) {
            finish(requestId, operation, false, {}, QStringLiteral("invalidArguments")); return;
        }
        if (!m_imports) { finish(requestId, operation, false, {}, QStringLiteral("unavailable")); return; }
        auto transfer = m_imports->takeCompletedTransfer(transferId, QStringLiteral("languageJson"));
        if (!transfer || !transfer->isValid()) {
            finish(requestId, operation, false, {}, QStringLiteral("invalidTransfer")); return;
        }
        const QDir directory(transfer->path());
        const QStringList files = directory.entryList(QDir::Files | QDir::NoDotAndDotDot);
        if (files.size() != 1) { finish(requestId, operation, false, {}, QStringLiteral("invalidTransfer")); return; }
        const auto snapshot = manager->captureFileSnapshot();
        const QString path = directory.filePath(files.first());
        auto ownedTransfer = std::shared_ptr<QTemporaryDir>(std::move(transfer));
        auto prepared = std::make_shared<TranslationManager::PreparedImport>();
        m_activeFileRequest = requestId;
        m_activeFileOperation = operation;
        m_fileCancelled = false;
#ifdef DECENZA_TESTING
        m_lastFileCallbackNanos = 0;
#endif
        auto* worker = QThread::create([snapshot, path, ownedTransfer, prepared,
                                       language = arguments.value(QStringLiteral("language")).toString()] {
            Q_UNUSED(ownedTransfer);
            *prepared = TranslationManager::prepareTranslationImport(snapshot, path, language);
        });
        connect(worker, &QThread::finished, this, [this, prepared, requestId, operation, arguments] {
            if (m_activeFileRequest != requestId) return;
#ifdef DECENZA_TESTING
            QElapsedTimer callbackTimer;
            callbackTimer.start();
#endif
            QPointer<NativeLanguageServiceSource> guard(this);
            QString error = m_fileCancelled ? QStringLiteral("cancelled")
                : !m_manager ? QStringLiteral("unavailable") : stringContextError(arguments);
            if (error.isEmpty() && (!m_fileOperationAllowed || !m_fileOperationAllowed()))
                error = QStringLiteral("machineBusy");
            QVariantMap result;
            if (error.isEmpty()) {
                m_applyingPreparedFile = true;
                result = m_manager->commitTranslationImport(*prepared);
                if (!guard) return;
                m_applyingPreparedFile = false;
                m_uniqueCountsCached = false;
                if (m_manager && result.value(QStringLiteral("translationsSaved")).toBool()) {
                    m_uniqueStringCount = prepared->uniqueStringCount;
                    m_uniqueUntranslatedCount = prepared->uniqueUntranslatedCount;
                    m_uniqueCountsCached = m_manager->currentLanguage() == prepared->language
                        && m_manager->catalogRevision() == prepared->baseRevision + 1;
                }
                rebuildLanguageRows();
                error = result.value(QStringLiteral("reason")).toString();
            }
            completeFileOperation(requestId, operation, result.value(QStringLiteral("success")).toBool(), result, error);
#ifdef DECENZA_TESTING
            if (guard) guard->m_lastFileCallbackNanos = callbackTimer.nsecsElapsed();
#endif
        }, Qt::QueuedConnection);
        connect(worker, &QThread::finished, worker, &QObject::deleteLater);
        m_fileWorker = worker;
        worker->start();
    } else if (operation == QLatin1String("exportTranslation")) {
        if (!ServiceValue::hasExactKeys(arguments, {QStringLiteral("sourceId"), QStringLiteral("language"), QStringLiteral("revision")})) {
            finish(requestId, operation, false, {}, QStringLiteral("invalidArguments")); return;
        }
        if (m_bundles.hasBundle()) { finish(requestId, operation, false, {}, QStringLiteral("busy")); return; }
        auto directory = std::make_shared<QTemporaryDir>();
        if (!directory->isValid()) { finish(requestId, operation, false, {}, QStringLiteral("temporaryStorageUnavailable")); return; }
        const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const QString fileName = id + QStringLiteral(".json");
        const auto snapshot = manager->captureFileSnapshot();
        auto preparedError = std::make_shared<QString>();
        m_activeFileRequest = requestId;
        m_activeFileOperation = operation;
        m_fileCancelled = false;
#ifdef DECENZA_TESTING
        m_lastFileCallbackNanos = 0;
#endif
        auto* worker = QThread::create([snapshot, directory, fileName, preparedError] {
            *preparedError = TranslationManager::writeTranslationExport(snapshot, directory->filePath(fileName));
        });
        connect(worker, &QThread::finished, this,
                [this, preparedError, directory, id, fileName, requestId, operation, arguments] {
            if (m_activeFileRequest != requestId) return;
#ifdef DECENZA_TESTING
            QElapsedTimer callbackTimer;
            callbackTimer.start();
#endif
            QPointer<NativeLanguageServiceSource> guard(this);
            QString error = m_fileCancelled ? QStringLiteral("cancelled")
                : !m_manager ? QStringLiteral("unavailable") : stringContextError(arguments);
            if (error.isEmpty() && (!m_fileOperationAllowed || !m_fileOperationAllowed()))
                error = QStringLiteral("machineBusy");
            if (error.isEmpty()) error = *preparedError;
            QVariantMap bundle;
            if (error.isEmpty()) {
                bundle = m_bundles.adopt(directory, id, fileName,
                    QStringLiteral("translations-") + m_manager->currentLanguage() + QStringLiteral(".json"),
                    TranslationCatalog::MaximumPayloadBytes, QStringLiteral("application/json"),
                    QStringLiteral("JSON files (*.json)"));
                if (bundle.isEmpty()) error = QStringLiteral("exportFailed");
            }
            completeFileOperation(requestId, operation, error.isEmpty(), bundle, error);
#ifdef DECENZA_TESTING
            if (guard) guard->m_lastFileCallbackNanos = callbackTimer.nsecsElapsed();
#endif
        }, Qt::QueuedConnection);
        connect(worker, &QThread::finished, worker, &QObject::deleteLater);
        m_fileWorker = worker;
        worker->start();
    } else if (operation == QLatin1String("cancelFileOperation")) {
        QString target;
        if (!ServiceValue::hasExactKeys(arguments, {QStringLiteral("requestId")})
            || !boundedString(arguments, QStringLiteral("requestId"), 128, &target) || target.isEmpty()) {
            finish(requestId, operation, false, {}, QStringLiteral("invalidArguments")); return;
        }
        const bool cancelled = target == m_activeFileRequest && !m_applyingPreparedFile;
        if (cancelled && !m_fileCancelled) {
            m_fileCancelled = true;
            finish(target, m_activeFileOperation, false, {}, QStringLiteral("cancelled"));
        }
        finish(requestId, operation, true, {{QStringLiteral("cancelled"), cancelled}});
    } else if (operation == QLatin1String("readBundleChunk")) {
        QString error;
        const QVariantMap chunk = m_bundles.readChunk(arguments, &error);
        finish(requestId, operation, error.isEmpty(), chunk, error);
    } else if (operation == QLatin1String("releaseBundle")) {
        QString error;
        const bool released = m_bundles.release(arguments, &error);
        finish(requestId, operation, released, {}, error);
    } else if (operation == QLatin1String("requestCatalogPage")) {
        int offset = -1;
        double revision = -1;
        if (!ServiceValue::hasExactKeys(arguments, {QStringLiteral("revision"), QStringLiteral("offset")})
            || !ServiceValue::finiteNumber(arguments.value(QStringLiteral("revision")), &revision)
            || std::floor(revision) != revision || revision < 0
            || revision > static_cast<double>(TranslationCatalog::MaximumExactJsonInteger)
            || !ServiceValue::boundedInteger(arguments.value(QStringLiteral("offset")), 0,
                                             TranslationCatalog::MaximumEntries, &offset)) {
            finish(requestId, operation, false, {}, QStringLiteral("invalidArguments"));
            return;
        }
        QString error;
        const QVariantMap page = catalogPage(static_cast<qint64>(revision), offset, &error);
        finish(requestId, operation, error.isEmpty(), page, error);
    } else if (operation == QLatin1String("requestStrings")) {
        int offset = -1;
        int limit = -1;
        if (!ServiceValue::hasExactKeys(arguments, {QStringLiteral("offset"), QStringLiteral("limit"), QStringLiteral("language"), QStringLiteral("revision"), QStringLiteral("sourceId")})
            || !ServiceValue::boundedInteger(arguments.value(QStringLiteral("offset")), 0,
                                             std::numeric_limits<int>::max(), &offset)
            || !ServiceValue::boundedInteger(arguments.value(QStringLiteral("limit")), 1,
                                             kMaximumPageSize, &limit)) {
            finish(requestId, operation, false, {}, QStringLiteral("invalidArguments")); return;
        }
        QString pageError;
        const QVariantList rows = boundedStringPage(offset, limit, &pageError);
        if (!pageError.isEmpty()) {
            finish(requestId, operation, false, {}, pageError);
            return;
        }
        finish(requestId, operation, true,
               {{QStringLiteral("offset"), offset}, {QStringLiteral("rows"), rows},
                {QStringLiteral("total"), manager->groupedStringCount()},
                {QStringLiteral("language"), manager->currentLanguage()},
                {QStringLiteral("sourceId"), m_sourceId},
                {QStringLiteral("revision"), manager->catalogRevision()}});
    } else if (operation == QLatin1String("setGroupTranslation")) {
        QString fallbackText;
        QString translationText;
        if (!ServiceValue::hasExactKeys(arguments, {QStringLiteral("fallback"), QStringLiteral("translation"), QStringLiteral("language"), QStringLiteral("revision"), QStringLiteral("sourceId")})
            || !boundedString(arguments, QStringLiteral("fallback"), 16384, &fallbackText)
            || !boundedString(arguments, QStringLiteral("translation"), 16384, &translationText)) {
            finish(requestId, operation, false, {}, QStringLiteral("invalidArguments")); return;
        }
        const bool saved = manager->setGroupTranslation(fallbackText, translationText);
        finish(requestId, operation, saved, {}, saved ? QString() : QStringLiteral("writeFailed"));
    } else if (operation == QLatin1String("clearAiTranslation")) {
        if (!ServiceValue::hasExactKeys(arguments, {QStringLiteral("fallback"), QStringLiteral("language"), QStringLiteral("revision"), QStringLiteral("sourceId")})
            || !boundedString(arguments, QStringLiteral("fallback"), 16384, &fallback)) {
            finish(requestId, operation, false, {}, QStringLiteral("invalidArguments")); return;
        }
        const bool saved = manager->clearAiTranslation(fallback);
        finish(requestId, operation, saved, {}, saved ? QString() : QStringLiteral("writeFailed"));
    } else if (operation == QLatin1String("copyAiToFinal")) {
        if (!ServiceValue::hasExactKeys(arguments, {QStringLiteral("fallback"), QStringLiteral("language"), QStringLiteral("revision"), QStringLiteral("sourceId")})
            || !boundedString(arguments, QStringLiteral("fallback"), 16384, &fallback)) {
            finish(requestId, operation, false, {}, QStringLiteral("invalidArguments")); return;
        }
        const bool saved = manager->copyAiToFinal(fallback);
        finish(requestId, operation, saved, {}, saved ? QString() : QStringLiteral("writeFailedOrUnavailable"));
    } else if (operation == QLatin1String("clearAllAiTranslations")) {
        if (!ServiceValue::hasExactKeys(arguments, {QStringLiteral("language"), QStringLiteral("revision"), QStringLiteral("sourceId")})) {
            finish(requestId, operation, false, {}, QStringLiteral("invalidArguments")); return;
        }
        const bool saved = manager->clearAllAiTranslations();
        finish(requestId, operation, saved, {}, saved ? QString() : QStringLiteral("writeFailedOrUnavailable"));
    } else if (operation == QLatin1String("autoTranslate") || operation == QLatin1String("retranslateAll")) {
        if (!ServiceValue::hasExactKeys(arguments, {QStringLiteral("language"), QStringLiteral("revision"), QStringLiteral("sourceId")})) {
            finish(requestId, operation, false, {}, QStringLiteral("invalidArguments")); return;
        }
        if (!manager->canAutoTranslate() || manager->isAutoTranslating()) {
            finish(requestId, operation, false, {}, QStringLiteral("unavailableOrBusy")); return;
        }
        if (!async(QStringLiteral("autoTranslateFinished"))) return;
        if (operation == QLatin1String("retranslateAll")) manager->retranslateAll();
        else manager->autoTranslate();
        publishSnapshot();
    } else if (operation == QLatin1String("cancelAutoTranslate")) {
        if (!arguments.isEmpty() || !manager->isAutoTranslating()) {
            finish(requestId, operation, false, {}, QStringLiteral("notRunning")); return;
        }
        m_cancelRequested = true;
        manager->cancelAutoTranslate();
        finish(requestId, operation, true);
    } else if (operation == QLatin1String("batchTranslateUpload")) {
        if (!arguments.isEmpty()) {
            finish(requestId, operation, false, {}, QStringLiteral("invalidArguments")); return;
        }
        if (!LanguageOperationPolicy::batchTranslateUploadAllowed(
                manager->isBatchTranslateUploadActive(), manager->isAutoTranslating(),
                manager->isUploading())) {
            finish(requestId, operation, false, {}, QStringLiteral("busy")); return;
        }
        if (!async(QStringLiteral("batchTranslateUploadFinished"))) return;
        m_batchTranslateUploadActive = true;
        publishSnapshot();
        manager->translateAndUploadAllLanguages();
    } else {
        finish(requestId, operation, false, {}, QStringLiteral("unsupportedOperation"));
    }
}
