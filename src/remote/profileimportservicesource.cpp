#include "profileimportservicesource.h"

#include "importservicesource.h"
#include "profile/profileimporter.h"
#include "servicepayload.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QRegularExpression>
#include <QSet>
#include <QTemporaryDir>
#include <QUuid>
#include <utility>

namespace {
constexpr qsizetype kMaxRows = 2000;
constexpr qint64 kMaxSnapshotBytes = 1024 * 1024;
constexpr qsizetype kMaxProfileIdLength = 64;
constexpr qsizetype kMaxTransferIdLength = 64;
constexpr qsizetype kMaxNameLength = 120;

bool hasOnlyKeys(const QVariantMap& args, std::initializer_list<QLatin1StringView> keys)
{
    if (args.size() != qsizetype(keys.size()))
        return false;
    for (auto it = args.cbegin(); it != args.cend(); ++it) {
        bool found = false;
        for (QLatin1StringView key : keys) {
            if (it.key() == key) {
                found = true;
                break;
            }
        }
        if (!found)
            return false;
    }
    return true;
}

bool exactString(const QVariantMap& args, QLatin1StringView key, qsizetype maxLength,
                 QString* value)
{
    const auto it = args.constFind(key);
    if (it == args.cend() || it->metaType().id() != QMetaType::QString)
        return false;
    const QString text = it->toString();
    if (text.isEmpty() || text.size() > maxLength)
        return false;
    if (text.contains(QRegularExpression(QStringLiteral("[\\x00-\\x1F\\x7F]"))))
        return false;
    *value = text;
    return true;
}

QString reasonFor(ServiceRequestIds::Start start)
{
    switch (start) {
    case ServiceRequestIds::Start::Accepted: return {};
    case ServiceRequestIds::Start::InvalidId: return QStringLiteral("invalidRequest");
    case ServiceRequestIds::Start::DuplicateId: return QStringLiteral("duplicateRequest");
    case ServiceRequestIds::Start::LimitReached: return QStringLiteral("tooManyRequests");
    }
    return QStringLiteral("invalidRequest");
}

bool importOperation(const QString& operation)
{
    return operation == QLatin1String("importProfile")
        || operation == QLatin1String("forceImport")
        || operation == QLatin1String("importTransfer");
}
}

NativeProfileImportServiceSource::NativeProfileImportServiceSource(
    ProfileImporter* importer, NativeImportServiceSource* transfer, QObject* parent)
    : ProfileImportStateSimpleSource(parent), m_importer(importer), m_importService(transfer)
{
    if (importer) {
        connect(importer, &ProfileImporter::isScanningChanged,
                this, &NativeProfileImportServiceSource::refreshSnapshot);
        connect(importer, &ProfileImporter::isImportingChanged,
                this, &NativeProfileImportServiceSource::refreshSnapshot);
        connect(importer, &ProfileImporter::availableProfilesChanged, this, [this] {
            projectRows();
            refreshSnapshot();
        });
        connect(importer, &ProfileImporter::statusMessageChanged,
                this, &NativeProfileImportServiceSource::refreshSnapshot);
        connect(importer, &ProfileImporter::detectedPathChanged,
                this, &NativeProfileImportServiceSource::refreshSnapshot);
        connect(importer, &ProfileImporter::scanComplete, this, [this](int count) {
            if (!m_activeId.isEmpty()
                && (m_activeOperation == QLatin1String("scanProfiles")
                    || m_activeOperation == QLatin1String("scanTransfer"))) {
                finishActive(true, {{QStringLiteral("found"), count}});
                return;
            }
            if (m_activeOperation != QLatin1String("importTransfer") || !m_importAfterScan)
                return;

            m_importAfterScan = false;
            const QVariantList rows = m_importer->availableProfiles();
            if (rows.isEmpty()) {
                finishActive(false, {}, QStringLiteral("noValidProfiles"));
                return;
            }
            const QString path = rows.first().toMap().value(QStringLiteral("sourcePath")).toString();
            if (path.isEmpty()) {
                finishActive(false, {}, QStringLiteral("noValidProfiles"));
                return;
            }
            m_importer->importProfile(path);
        });
        connect(importer, &ProfileImporter::importSuccess, this, [this](const QString& title) {
            if (!m_activeId.isEmpty() && importOperation(m_activeOperation))
                finishActive(true, {{QStringLiteral("title"), title.left(160)}});
            refreshSnapshot();
        });
        connect(importer, &ProfileImporter::importFailed, this, [this](const QString&) {
            if (!m_activeId.isEmpty() && importOperation(m_activeOperation))
                finishActive(false, {}, QStringLiteral("importFailed"));
            refreshSnapshot();
        });
        connect(importer, &ProfileImporter::duplicateFound, this,
                [this](const QString& title, const QString&) {
            if (m_activeId.isEmpty() || !importOperation(m_activeOperation))
                return;
            m_duplicateId = m_activeId;
            emit duplicateFound(m_activeId, title.left(160));
        });
        connect(importer, &ProfileImporter::batchImportComplete, this,
                [this](int imported, int skipped, int failed) {
            if (m_activeId.isEmpty()
                || (m_activeOperation != QLatin1String("importAllNew")
                    && m_activeOperation != QLatin1String("updateAllDifferent")))
                return;
            finishActive(failed == 0,
                         {{QStringLiteral("imported"), imported},
                          {QStringLiteral("skipped"), skipped},
                          {QStringLiteral("failed"), failed}},
                         failed == 0 ? QString() : QStringLiteral("someProfilesFailed"));
        });
        connect(importer, &ProfileImporter::progressChanged,
                this, &NativeProfileImportServiceSource::refreshSnapshot);
    }
    projectRows();
    refreshSnapshot();
}

NativeProfileImportServiceSource::~NativeProfileImportServiceSource() = default;

void NativeProfileImportServiceSource::refresh()
{
    // Refresh is a read-only snapshot operation. Scanning is explicit so a second
    // client cannot silently replace a scan or consume another request's completion.
    refreshSnapshot();
}

QVariantMap NativeProfileImportServiceSource::makeSnapshot()
{
    QString detectedFolder;
    if (m_scanTransfer) {
        detectedFolder = QStringLiteral("Selected folder");
    } else if (m_importer && !m_importer->detectedPath().isEmpty()) {
        detectedFolder = QFileInfo(m_importer->detectedPath()).fileName();
    }

    QString status = m_importer ? m_importer->statusMessage() : QString();
    if (m_importer && !m_importer->detectedPath().isEmpty())
        status.replace(m_importer->detectedPath(), QFileInfo(m_importer->detectedPath()).fileName());
    if (m_scanTransfer)
        status.replace(m_scanTransfer->path(), QStringLiteral("selected folder"));
    status.replace(QRegularExpression(QStringLiteral(R"((?:[A-Za-z]:[\\/]|/)[^\s\"<>]+)")),
                   QStringLiteral("[local file]"));
    status = status.left(512);

    return {
        {QStringLiteral("isScanning"), m_importer && m_importer->isScanning()},
        {QStringLiteral("isImporting"), m_importer && m_importer->isImporting()},
        {QStringLiteral("statusMessage"), status},
        {QStringLiteral("detectedFolder"), detectedFolder},
        {QStringLiteral("totalProfiles"), m_importer ? m_importer->totalProfiles() : 0},
        {QStringLiteral("processedProfiles"), m_importer ? m_importer->processedProfiles() : 0},
        {QStringLiteral("totalAvailableProfiles"), m_totalAvailableProfiles},
        {QStringLiteral("profilesTruncated"), m_profilesTruncated}
    };
}

void NativeProfileImportServiceSource::refreshSnapshot()
{
    setStatus(makeSnapshot());
    setReady(bool(m_importer && m_importService));
}

void NativeProfileImportServiceSource::projectRows()
{
    QVariantList sourceRows = m_importer ? m_importer->availableProfiles() : QVariantList{};
    m_totalAvailableProfiles = sourceRows.size();
    QSet<QString> livePaths;
    for (const QVariant& value : sourceRows) {
        const QString path = value.toMap().value(QStringLiteral("sourcePath")).toString();
        if (path.isEmpty())
            continue;
        livePaths.insert(path);
        if (!m_idsByPath.contains(path)) {
            const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            m_idsByPath.insert(path, id);
            m_pathsById.insert(id, path);
        }
    }
    for (auto it = m_idsByPath.begin(); it != m_idsByPath.end();) {
        if (!livePaths.contains(it.key())) {
            m_pathsById.remove(it.value());
            it = m_idsByPath.erase(it);
        } else {
            ++it;
        }
    }

    QVariantList rows;
    rows.reserve(qMin(m_totalAvailableProfiles, kMaxRows));
    for (const QVariant& value : sourceRows) {
        const QVariantMap row = value.toMap();
        const QString path = row.value(QStringLiteral("sourcePath")).toString();
        const QString id = m_idsByPath.value(path);
        if (id.isEmpty())
            continue;
        rows.append(QVariantMap{
            {QStringLiteral("id"), id},
            {QStringLiteral("filename"), row.value(QStringLiteral("filename")).toString().left(255)},
            {QStringLiteral("title"), row.value(QStringLiteral("title")).toString().left(160)},
            {QStringLiteral("author"), row.value(QStringLiteral("author")).toString().left(120)},
            {QStringLiteral("frameCount"), row.value(QStringLiteral("frameCount"))},
            {QStringLiteral("format"), row.value(QStringLiteral("format"))},
            {QStringLiteral("beverageType"), row.value(QStringLiteral("beverageType"))},
            {QStringLiteral("exists"), row.value(QStringLiteral("exists"))},
            {QStringLiteral("identical"), row.value(QStringLiteral("identical"))},
            {QStringLiteral("status"), row.value(QStringLiteral("status"))}
        });
        if (rows.size() >= kMaxRows)
            break;
    }

    m_profilesTruncated = m_totalAvailableProfiles > rows.size();
    if (!ServicePayload::fits(rows, kMaxSnapshotBytes)) {
        qsizetype low = 0;
        qsizetype high = rows.size();
        while (low < high) {
            const qsizetype middle = (low + high + 1) / 2;
            if (ServicePayload::fits(rows.mid(0, middle), kMaxSnapshotBytes))
                low = middle;
            else
                high = middle - 1;
        }
        rows = rows.mid(0, low);
        m_profilesTruncated = true;
    }

    m_projectedRows = std::move(rows);
    setAvailableProfiles(m_projectedRows);
    setStatus(makeSnapshot());
}

void NativeProfileImportServiceSource::finish(const QString& requestId, const QString& operation,
                                               bool success, const QVariantMap& result,
                                               const QString& reason)
{
    if (!m_requests.finish(requestId))
        return;
    emit operationCompleted(requestId, operation, success, result, reason);
    refreshSnapshot();
}

void NativeProfileImportServiceSource::finishActive(bool success, const QVariantMap& result,
                                                      const QString& reason)
{
    if (m_activeId.isEmpty())
        return;
    const QString id = std::exchange(m_activeId, {});
    const QString operation = std::exchange(m_activeOperation, {});
    m_duplicateId.clear();
    m_importAfterScan = false;
    finish(id, operation, success, result, reason);
}

void NativeProfileImportServiceSource::finishReason(QString requestId, QString operation,
                                                     const QString& reason)
{
    const auto start = m_requests.begin(requestId);
    if (start != ServiceRequestIds::Start::Accepted) {
        if (start != ServiceRequestIds::Start::DuplicateId) {
            QMetaObject::invokeMethod(this, [this, requestId, operation, reason] {
                emit operationCompleted(requestId, operation, false, {}, reason);
            }, Qt::QueuedConnection);
        }
        return;
    }
    finish(requestId, operation, false, {}, reason);
}

void NativeProfileImportServiceSource::execute(QString requestId, QString operation,
                                                QVariantMap arguments)
{
    const bool resolution = operation == QLatin1String("saveOverwrite")
        || operation == QLatin1String("saveAsNew")
        || operation == QLatin1String("saveWithNewName")
        || operation == QLatin1String("cancelImport");
    if (resolution) {
        if (requestId.isEmpty() || requestId != m_duplicateId)
            return;
        if (!m_importer) {
            emit requestRejected(requestId, operation, QStringLiteral("unavailable"));
            return;
        }
        QString newName;
        if (operation == QLatin1String("saveWithNewName")) {
            if (!hasOnlyKeys(arguments, {QLatin1StringView("name")})
                || !exactString(arguments, QLatin1StringView("name"), kMaxNameLength, &newName)
                || newName.trimmed().isEmpty()) {
                emit requestRejected(requestId, operation, QStringLiteral("invalidName"));
                return;
            }
        } else if (!arguments.isEmpty()) {
            emit requestRejected(requestId, operation, QStringLiteral("invalidArguments"));
            return;
        }

        if (operation == QLatin1String("saveOverwrite")) {
            m_importer->saveOverwrite();
        } else if (operation == QLatin1String("saveAsNew")) {
            m_importer->saveAsNew();
        } else if (operation == QLatin1String("saveWithNewName")) {
            m_importer->saveWithNewName(newName.trimmed());
        } else {
            m_importer->cancelImport();
            finishActive(false, {}, QStringLiteral("cancelled"));
        }
        return;
    }

    if (!m_importer) {
        finishReason(requestId, operation, QStringLiteral("unavailable"));
        return;
    }

    QString profileId;
    QString transferId;
    bool validArguments = false;
    if (operation == QLatin1String("scanProfiles")
        || operation == QLatin1String("importAllNew")
        || operation == QLatin1String("updateAllDifferent")) {
        validArguments = arguments.isEmpty();
    } else if (operation == QLatin1String("scanTransfer")
               || operation == QLatin1String("importTransfer")) {
        validArguments = hasOnlyKeys(arguments, {QLatin1StringView("transferId")})
            && exactString(arguments, QLatin1StringView("transferId"), kMaxTransferIdLength,
                           &transferId);
    } else if (operation == QLatin1String("importProfile")
               || operation == QLatin1String("forceImport")) {
        validArguments = hasOnlyKeys(arguments, {QLatin1StringView("profileId")})
            && exactString(arguments, QLatin1StringView("profileId"), kMaxProfileIdLength,
                           &profileId);
    }
    if (!validArguments) {
        finishReason(requestId, operation, QStringLiteral("invalidArguments"));
        return;
    }

    const auto start = m_requests.begin(requestId);
    const QString rejection = reasonFor(start);
    if (!rejection.isEmpty()) {
        if (start != ServiceRequestIds::Start::DuplicateId) {
            QMetaObject::invokeMethod(this, [this, requestId, operation, rejection] {
                emit operationCompleted(requestId, operation, false, {}, rejection);
            }, Qt::QueuedConnection);
        }
        return;
    }
    if (!m_activeId.isEmpty() || m_importer->isImporting() || m_importer->isScanning()) {
        finish(requestId, operation, false, {}, QStringLiteral("busy"));
        return;
    }

    m_activeId = requestId;
    m_activeOperation = operation;
    if (operation == QLatin1String("scanProfiles")) {
        if (m_scanTransfer)
            m_importer->scanProfilesFromPath(m_scanTransfer->path());
        else
            m_importer->scanProfiles();
    } else if (operation == QLatin1String("scanTransfer")
               || operation == QLatin1String("importTransfer")) {
        if (!m_importService) {
            finishActive(false, {}, QStringLiteral("transferUnavailable"));
            return;
        }
        auto transfer = m_importService->takeCompletedTransfer(transferId,
                                                                QStringLiteral("profileFiles"));
        if (!transfer) {
            finishActive(false, {}, QStringLiteral("invalidTransfer"));
            return;
        }
        const QString root = transfer->path();
        QDir directory(root);
        if (!directory.mkpath(QStringLiteral("profiles"))
            || !directory.mkpath(QStringLiteral("profiles_v2"))) {
            finishActive(false, {}, QStringLiteral("temporaryStorageUnavailable"));
            return;
        }
        const QStringList files = directory.entryList(QDir::Files | QDir::NoDotAndDotDot);
        if (files.isEmpty()) {
            finishActive(false, {}, QStringLiteral("emptySelection"));
            return;
        }
        for (const QString& file : files) {
            const QString extension = QFileInfo(file).suffix().toLower();
            if (extension != QLatin1String("tcl") && extension != QLatin1String("json")) {
                finishActive(false, {}, QStringLiteral("invalidProfileFile"));
                return;
            }
            const QString subdirectory = extension == QLatin1String("tcl")
                ? QStringLiteral("profiles") : QStringLiteral("profiles_v2");
            if (!QFile::rename(directory.filePath(file),
                               directory.filePath(subdirectory + QLatin1Char('/') + file))) {
                finishActive(false, {}, QStringLiteral("temporaryStorageUnavailable"));
                return;
            }
        }
        m_scanTransfer = std::move(transfer);
        m_importAfterScan = operation == QLatin1String("importTransfer");
        m_importer->scanProfilesFromPath(root);
    } else if (operation == QLatin1String("importProfile")
               || operation == QLatin1String("forceImport")) {
        const QString path = m_pathsById.value(profileId);
        if (path.isEmpty()) {
            finishActive(false, {}, QStringLiteral("unknownProfile"));
            return;
        }
        if (operation == QLatin1String("forceImport"))
            m_importer->forceImportProfile(path);
        else
            m_importer->importProfile(path);
    } else if (operation == QLatin1String("importAllNew")) {
        m_importer->importAllNew();
    } else if (operation == QLatin1String("updateAllDifferent")) {
        m_importer->updateAllDifferent();
    }
    refreshSnapshot();
}
