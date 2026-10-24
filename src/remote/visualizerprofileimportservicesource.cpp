#include "visualizerprofileimportservicesource.h"
#include "servicevalue.h"

#include "network/visualizerimporter.h"
#include "servicepayload.h"

#include <QMetaObject>
#include <QRegularExpression>
#include <QSet>
#include <QUuid>
#include <utility>

namespace {
constexpr int kMaxPending = 8;
constexpr qsizetype kMaxShotRows = 500;
constexpr qint64 kMaxSnapshotBytes = 1024 * 1024;
constexpr qsizetype kMaxNameLength = 120;
const QRegularExpression kShotIdPattern(
    QStringLiteral("^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$"));


bool exactString(const QVariantMap& args, QLatin1StringView key, qsizetype maximum,
                 QString* value, bool trim = false)
{
    const auto it = args.constFind(key);
    if (it == args.cend() || it->metaType().id() != QMetaType::QString)
        return false;
    QString text = it->toString();
    if (trim)
        text = text.trimmed();
    if (text.isEmpty() || text.size() > maximum
        || text.contains(QRegularExpression(QStringLiteral("[\\x00-\\x1F\\x7F]"))))
        return false;
    *value = text;
    return true;
}

bool isResolution(const QString& operation)
{
    return operation == QLatin1String("saveOverwrite")
        || operation == QLatin1String("saveAsNew")
        || operation == QLatin1String("saveWithNewName")
        || operation == QLatin1String("cancelPending");
}

bool isSingleImport(const QString& operation)
{
    return operation == QLatin1String("importFromShareCode")
        || operation == QLatin1String("importFromShotId")
        || operation == QLatin1String("importFromShotIdWithName");
}
}

NativeVisualizerProfileImportServiceSource::NativeVisualizerProfileImportServiceSource(
    VisualizerImporter* importer, QObject* parent)
    : VisualizerProfileImportStateSimpleSource(parent), m_importer(importer)
{
    if (importer) {
        connect(importer, &VisualizerImporter::importingChanged, this, [this] {
            setImporting(m_importer && m_importer->isImporting());
        });
        connect(importer, &VisualizerImporter::fetchingChanged, this, [this] {
            setFetching(m_importer && m_importer->isFetching());
            completeFetchIfStopped();
        });
        connect(importer, &VisualizerImporter::sharedShotsChanged, this, [this] {
            m_fetchDataArrived = !m_fetchRequestId.isEmpty();
            projectSharedShots();
        });
        connect(importer, &VisualizerImporter::lastErrorChanged, this, [this] {
            setLastError(m_importer && !m_importer->lastError().isEmpty()
                             ? QStringLiteral("requestFailed") : QString());
        });
        connect(importer, &VisualizerImporter::importSuccess, this,
                [this](const QString& title) {
            if (m_activeImportOperation == QLatin1String("importSelectedShots"))
                return;
            if (isSingleImport(m_activeImportOperation))
                finishImport(true, title);
        });
        connect(importer, &VisualizerImporter::importFailed, this,
                [this](const QString&) {
            if (!m_fetchRequestId.isEmpty()) {
                const QString id = std::exchange(m_fetchRequestId, {});
                const QString operation = std::exchange(m_fetchOperation, {});
                m_fetchDataArrived = false;
                finish(id, operation, false, {}, QStringLiteral("fetchFailed"));
            } else if (isSingleImport(m_activeImportOperation)) {
                finishImport(false, {});
            }
            // Batch imports report one terminal batchImportComplete signal. Their
            // per-shot fetch/save signals must never consume the batch request.
        });
        connect(importer, &VisualizerImporter::duplicateFound, this,
                [this](const QString& title, const QString&) {
            if (!isSingleImport(m_activeImportOperation) || m_activeImportId.isEmpty())
                return;
            m_duplicateRequestId = m_activeImportId;
            emit duplicateFound(m_activeImportId, title.left(160));
        });
        connect(importer, &VisualizerImporter::batchImportComplete, this,
                [this](int imported, int skipped, int failed) {
            if (m_activeImportOperation != QLatin1String("importSelectedShots"))
                return;
            const QString id = std::exchange(m_activeImportId, {});
            const QString operation = std::exchange(m_activeImportOperation, {});
            finish(id, operation, failed == 0,
                   {{QStringLiteral("imported"), imported},
                    {QStringLiteral("skipped"), skipped},
                    {QStringLiteral("failed"), failed}},
                   failed == 0 ? QString() : QStringLiteral("someProfilesFailed"));
        });
    }

    setReady(bool(importer));
    refreshSnapshot();
}

void NativeVisualizerProfileImportServiceSource::refresh()
{
    // The list is projected only when the importer says it changed. A refresh
    // must not trigger a second network request or rebuild a large list.
    refreshSnapshot();
}

void NativeVisualizerProfileImportServiceSource::refreshSnapshot()
{
    setImporting(m_importer && m_importer->isImporting());
    setFetching(m_importer && m_importer->isFetching());
    setLastError(m_importer && !m_importer->lastError().isEmpty()
                     ? QStringLiteral("requestFailed") : QString());
    setReady(bool(m_importer));
}

void NativeVisualizerProfileImportServiceSource::projectSharedShots()
{
    QVariantList projectedRows;
    bool truncated = false;
    if (m_importer) {
        const QVariantList rows = m_importer->sharedShots();
        projectedRows.reserve(qMin(rows.size(), kMaxShotRows));
        static const char* const allowedKeys[] = {
            "id", "profile_title", "duration", "user_name", "bean_brand", "bean_type",
            "bean_weight", "grinder_model", "grinder_setting", "start_time", "source",
            "exists", "identical", "invalid", "invalidReason"
        };
        for (const QVariant& value : rows) {
            const QVariantMap row = value.toMap();
            QVariantMap projected;
            for (const char* key : allowedKeys) {
                const QLatin1StringView qKey(key);
                if (row.contains(qKey))
                    projected.insert(qKey, row.value(qKey));
            }
            if (!kShotIdPattern.match(projected.value(QStringLiteral("id")).toString()).hasMatch())
                continue;
            if (projectedRows.size() >= kMaxShotRows) {
                truncated = true;
                break;
            }
            projectedRows.append(projected);
        }
        if (!ServicePayload::fits(projectedRows, kMaxSnapshotBytes)) {
            qsizetype low = 0;
            qsizetype high = projectedRows.size();
            while (low < high) {
                const qsizetype middle = (low + high + 1) / 2;
                if (ServicePayload::fits(projectedRows.mid(0, middle), kMaxSnapshotBytes))
                    low = middle;
                else
                    high = middle - 1;
            }
            projectedRows = projectedRows.mid(0, low);
            truncated = true;
        }
        truncated = truncated || projectedRows.size() < rows.size();
    }

    m_projectedSharedShots = std::move(projectedRows);
    m_sharedShotsTruncated = truncated;
    setSharedShots(m_projectedSharedShots);
    setSharedShotsTruncated(m_sharedShotsTruncated);
}

void NativeVisualizerProfileImportServiceSource::completeFetchIfStopped()
{
    if (m_fetchRequestId.isEmpty() || !m_importer || m_importer->isFetching())
        return;
    const QString expectedId = m_fetchRequestId;
    QMetaObject::invokeMethod(this, [this, expectedId] {
        if (m_fetchRequestId != expectedId || !m_importer || m_importer->isFetching())
            return;
        const QString id = std::exchange(m_fetchRequestId, {});
        const QString operation = std::exchange(m_fetchOperation, {});
        const bool success = m_fetchDataArrived;
        m_fetchDataArrived = false;
        finish(id, operation, success,
               success ? QVariantMap{{QStringLiteral("entries"), m_projectedSharedShots}} : QVariantMap{},
               success ? QString() : QStringLiteral("fetchFailed"));
    }, Qt::QueuedConnection);
}

QString NativeVisualizerProfileImportServiceSource::startReason(ServiceRequestIds::Start start) const
{
    switch (start) {
    case ServiceRequestIds::Start::Accepted: return {};
    case ServiceRequestIds::Start::DuplicateId: return QStringLiteral("duplicateRequestId");
    case ServiceRequestIds::Start::LimitReached: return QStringLiteral("tooManyRequests");
    case ServiceRequestIds::Start::InvalidId: return QStringLiteral("invalidRequestId");
    }
    return QStringLiteral("invalidRequestId");
}

void NativeVisualizerProfileImportServiceSource::finish(const QString& id, const QString& operation,
                                                          bool success, const QVariantMap& result,
                                                          const QString& reason)
{
    if (id.isEmpty() || !m_requests.finish(id))
        return;
    if (id == m_duplicateRequestId)
        m_duplicateRequestId.clear();
    if (id == m_activeImportId) {
        m_activeImportId.clear();
        m_activeImportOperation.clear();
    }
    emit operationCompleted(id, operation, success, result, reason);
}

void NativeVisualizerProfileImportServiceSource::finishImport(bool success, const QString& title)
{
    if (!isSingleImport(m_activeImportOperation) || m_activeImportId.isEmpty())
        return;
    const QString id = std::exchange(m_activeImportId, {});
    const QString operation = std::exchange(m_activeImportOperation, {});
    finish(id, operation, success,
           success ? QVariantMap{{QStringLiteral("title"), title.left(160)}} : QVariantMap{},
           success ? QString() : QStringLiteral("importFailed"));
}

void NativeVisualizerProfileImportServiceSource::reject(const QString& id,
                                                          const QString& operation,
                                                          const QString& reason,
                                                          bool tracked)
{
    if (tracked)
        finish(id, operation, false, {}, reason);
    else if (!id.isEmpty())
        QMetaObject::invokeMethod(this, [this, id, operation, reason] {
            emit operationCompleted(id, operation, false, {}, reason);
        }, Qt::QueuedConnection);
}

void NativeVisualizerProfileImportServiceSource::rejectResolution(const QString& id,
                                                                    const QString& operation,
                                                                    const QString& reason)
{
    if (!id.isEmpty())
        emit requestRejected(id, operation, reason);
}

void NativeVisualizerProfileImportServiceSource::execute(QString id, QString operation,
                                                            QVariantMap args)
{
    const bool resolution = isResolution(operation);
    if (resolution) {
        if (id.isEmpty() || id != m_duplicateRequestId || id != m_activeImportId)
            return;
        if ((operation == QLatin1String("saveWithNewName")
             && !ServiceValue::hasExactKeys(args, {QLatin1StringView("name")}))
            || (operation != QLatin1String("saveWithNewName") && !args.isEmpty())) {
            rejectResolution(id, operation, QStringLiteral("invalidArguments"));
            return;
        }
        if (operation == QLatin1String("saveWithNewName")) {
            QString name;
            if (!exactString(args, QLatin1StringView("name"), kMaxNameLength, &name, true)) {
                rejectResolution(id, operation, QStringLiteral("invalidName"));
                return;
            }
            m_importer->saveWithNewName(name);
        } else if (operation == QLatin1String("saveOverwrite")) {
            m_importer->saveOverwrite();
        } else if (operation == QLatin1String("saveAsNew")) {
            m_importer->saveAsNew();
        } else {
            const QString originalOperation = std::exchange(m_activeImportOperation, {});
            m_activeImportId.clear();
            m_duplicateRequestId.clear();
            m_importer->cancelPending();
            finish(id, originalOperation, false, {}, QStringLiteral("cancelled"));
        }
        return;
    }

    const auto start = m_requests.begin(id);
    const QString startError = startReason(start);
    if (!startError.isEmpty()) {
        if (start != ServiceRequestIds::Start::DuplicateId)
            reject(id, operation, startError, false);
        return;
    }
    if (!m_importer) {
        reject(id, operation, QStringLiteral("unavailable"), true);
        return;
    }

    QString validationError;
    QString shareCode;
    QString shotId;
    QString name;
    QStringList shotIds;
    bool overwrite = false;
    if (operation == QLatin1String("fetchSharedShots")) {
        if (!args.isEmpty()) validationError = QStringLiteral("invalidArguments");
    } else if (operation == QLatin1String("importFromShareCode")) {
        if (!ServiceValue::hasExactKeys(args, {QLatin1StringView("shareCode")}))
            validationError = QStringLiteral("invalidArguments");
        else if (!exactString(args, QLatin1StringView("shareCode"), 4, &shareCode, true)
                 || shareCode.size() != 4)
            validationError = QStringLiteral("invalidShareCode");
    } else if (operation == QLatin1String("importFromShotId")) {
        if (!ServiceValue::hasExactKeys(args, {QLatin1StringView("shotId")}))
            validationError = QStringLiteral("invalidArguments");
        else if (!exactString(args, QLatin1StringView("shotId"), 36, &shotId)
                 || !kShotIdPattern.match(shotId).hasMatch())
            validationError = QStringLiteral("invalidShotId");
    } else if (operation == QLatin1String("importFromShotIdWithName")) {
        if (!ServiceValue::hasExactKeys(args, {QLatin1StringView("shotId"), QLatin1StringView("name")}))
            validationError = QStringLiteral("invalidArguments");
        else if (!exactString(args, QLatin1StringView("shotId"), 36, &shotId)
                 || !kShotIdPattern.match(shotId).hasMatch())
            validationError = QStringLiteral("invalidShotId");
        else if (!exactString(args, QLatin1StringView("name"), kMaxNameLength, &name, true))
            validationError = QStringLiteral("invalidName");
    } else if (operation == QLatin1String("importSelectedShots")) {
        if (!ServiceValue::hasExactKeys(args, {QLatin1StringView("shotIds"), QLatin1StringView("overwrite")})
            || args.value(QStringLiteral("shotIds")).metaType().id() != QMetaType::QStringList
            || args.value(QStringLiteral("overwrite")).metaType().id() != QMetaType::Bool) {
            validationError = QStringLiteral("invalidArguments");
        } else {
            shotIds = args.value(QStringLiteral("shotIds")).toStringList();
            overwrite = args.value(QStringLiteral("overwrite")).toBool();
            if (shotIds.isEmpty() || shotIds.size() > 100) {
                validationError = QStringLiteral("invalidShotCount");
            } else {
                QSet<QString> publishedIds;
                for (const QVariant& row : std::as_const(m_projectedSharedShots))
                    publishedIds.insert(row.toMap().value(QStringLiteral("id")).toString());
                QSet<QString> requestedIds;
                for (const QString& selectedId : std::as_const(shotIds)) {
                    if (!kShotIdPattern.match(selectedId).hasMatch()
                        || !publishedIds.contains(selectedId)
                        || requestedIds.contains(selectedId)) {
                        validationError = QStringLiteral("invalidShotId");
                        break;
                    }
                    requestedIds.insert(selectedId);
                }
            }
        }
    } else {
        validationError = QStringLiteral("unsupportedOperation");
    }

    if (!validationError.isEmpty()) {
        reject(id, operation, validationError, true);
        return;
    }
    if (m_requests.pendingCount() > kMaxPending || !m_activeImportId.isEmpty()
        || !m_fetchRequestId.isEmpty()) {
        reject(id, operation, QStringLiteral("busy"), true);
        return;
    }

    if (operation == QLatin1String("fetchSharedShots")) {
        m_fetchRequestId = id;
        m_fetchOperation = operation;
        m_fetchDataArrived = false;
        m_importer->fetchSharedShots();
        if (!m_importer->isFetching()) {
            // Missing credentials and other immediate failures emit importFailed
            // synchronously. Handle the state here if that signal preceded this slot.
            if (m_fetchRequestId == id) {
                const QString error = m_importer->lastError();
                m_fetchRequestId.clear();
                m_fetchOperation.clear();
                finish(id, operation, false, {}, error.isEmpty()
                    ? QStringLiteral("fetchFailed") : QStringLiteral("credentialsMissing"));
            }
        }
        return;
    }

    m_activeImportId = id;
    m_activeImportOperation = operation;
    if (operation == QLatin1String("importFromShareCode"))
        m_importer->importFromShareCode(shareCode);
    else if (operation == QLatin1String("importFromShotId"))
        m_importer->importFromShotId(shotId);
    else if (operation == QLatin1String("importFromShotIdWithName"))
        m_importer->importFromShotIdWithName(shotId, name);
    else
        m_importer->importSelectedShots(shotIds, overwrite);

    if (!m_importer->isImporting() && !m_activeImportId.isEmpty()) {
        const QString reason = m_importer->lastError().isEmpty()
            ? QStringLiteral("importFailed") : QStringLiteral("importFailed");
        if (operation == QLatin1String("importSelectedShots")) {
            // Empty input is rejected above; an immediate refusal can still be
            // reported as a batch terminal event by the canonical importer.
            reject(id, operation, reason, true);
        } else {
            finishImport(false, {});
        }
    }
}
