#include "equipmentservicesource.h"
#include "servicevalue.h"

#include "../ai/dialing_blocks.h"
#include "../core/dbutils.h"
#include "../core/equipmentuioperations.h"
#include "../core/equipmentuisnapshot.h"
#include "../core/puckprep.h"
#include "../core/settings_dye.h"
#include "../history/equipmentstorage.h"
#include "../history/shothistorystorage.h"

#include <QCoreApplication>
#include <QThread>
#include <utility>

namespace {
bool positiveSafeId(const QVariant& value, qint64* result)
{
    double number = 0;
    if (!ServiceValue::finiteNumber(value, &number) || std::floor(number) != number
        || number < 1 || number > 9007199254740991.0)
        return false;
    if (result) *result = static_cast<qint64>(number);
    return true;
}

bool validGrindQuery(const QString& kind, const QVariantMap& query, qint64* packageId)
{
    const bool suggestion = kind == QLatin1String("suggestion");
    if ((!suggestion && kind != QLatin1String("calibration"))
        || query.size() != (suggestion ? 5 : 1)
        || !positiveSafeId(query.value(QStringLiteral("packageId")), packageId))
        return false;
    if (suggestion) {
        for (const QString& key : {QStringLiteral("beanBrand"), QStringLiteral("beanType"),
                                   QStringLiteral("roastDate"), QStringLiteral("profileTitle")}) {
            const QVariant value = query.value(key);
            if (value.metaType().id() != QMetaType::QString
                || value.toString().size() > (key == QLatin1String("roastDate") ? 64 : 4096))
                return false;
        }
    }
    return true;
}

bool isExpectedEditable(const QVariantMap& snapshot)
{
    static const QStringList keys{
        QStringLiteral("name"), QStringLiteral("grinderBrand"),
        QStringLiteral("grinderModel"), QStringLiteral("grinderBurrs"),
        QStringLiteral("basketBrand"), QStringLiteral("basketModel"),
        QStringLiteral("puckPrepCanonical")
    };
    if (snapshot.size() != keys.size())
        return false;
    for (const QString& key : keys) {
        const QVariant value = snapshot.value(key);
        if (!snapshot.contains(key) || value.metaType().id() != QMetaType::QString)
            return false;
    }
    return true;
}

bool validEditableFields(const QVariantMap& fields)
{
    static const QSet<QString> stringFields{
        QStringLiteral("name"), QStringLiteral("grinderBrand"),
        QStringLiteral("grinderModel"), QStringLiteral("grinderBurrs"),
        QStringLiteral("basketBrand"), QStringLiteral("basketModel")
    };
    for (auto it = fields.cbegin(); it != fields.cend(); ++it) {
        if (stringFields.contains(it.key())) {
            if (it.value().metaType().id() != QMetaType::QString)
                return false;
            continue;
        }
        const QString prefix = QStringLiteral("puckPrep_");
        if (it.key().startsWith(prefix)
            && PuckPrep::flagKeys().contains(it.key().mid(prefix.size()))
            && it.value().metaType().id() == QMetaType::Bool)
            continue;
        return false;
    }
    return !fields.isEmpty();
}

QString operationFailure(const QString& reason)
{
    if (reason == QLatin1String("expectedEditableConflict")
        || reason == QLatin1String("expectedNameConflict"))
        return QStringLiteral("conflict");
    return reason;
}
} // namespace

NativeEquipmentServiceSource::NativeEquipmentServiceSource(
    EquipmentStorage* storage, SettingsDye* dye, ShotHistoryStorage* history, QObject* parent)
    : EquipmentStateSimpleSource(parent)
    , m_storage(storage)
    , m_dye(dye)
    , m_history(history)
    , m_operations(new EquipmentUiOperations(this))
{
    m_operations->setStorage(storage);
    m_operations->setDye(dye);
    connect(m_operations, &EquipmentUiOperations::operationCompleted, this,
            [this](const QString& requestId, bool success, qint64 resultId, const QString& reason) {
        finishOperation(requestId, success, resultId, operationFailure(reason));
    });

    if (m_storage) {
        connect(m_storage, &EquipmentStorage::packagesChanged, this, [this] {
            emit packagesInvalidated();
            if (m_inventoryReadInFlight) {
                m_inventoryDirty = true;
            } else {
                refresh();
            }
        });
    }
    if (m_dye) {
        connect(m_dye, &SettingsDye::activeEquipmentIdChanged,
                this, &NativeEquipmentServiceSource::publishActivePackage);
        for (auto signal : {&SettingsDye::dyeGrinderBrandChanged,
                            &SettingsDye::dyeGrinderModelChanged,
                            &SettingsDye::dyeGrinderBurrsChanged,
                            &SettingsDye::dyeBasketChanged,
                            &SettingsDye::dyePuckPrepChanged}) {
            connect(m_dye, signal, this, &NativeEquipmentServiceSource::publishCreateSeed);
        }
    }
    publishOptions();
    publishCreateSeed();
    publishActivePackage();
    refresh();
}

void NativeEquipmentServiceSource::finishOperation(const QString& requestId, bool success,
                                                    qint64 resultId, const QString& reason)
{
    if (!m_requests.finish(requestId))
        return;
    emit operationCompleted(requestId, success, resultId, reason);
}

void NativeEquipmentServiceSource::publishActivePackage()
{
    const qint64 activeId = m_dye ? m_dye->activeEquipmentId() : -1;
    setActivePackageId(activeId > 0 ? activeId : -1);
    QVariantList updated;
    updated.reserve(m_packages.size());
    for (const QVariant& packageValue : m_packages) {
        QVariantMap package = packageValue.toMap();
        package.insert(QStringLiteral("isActive"),
                       package.value(QStringLiteral("id")).toLongLong() == activeId);
        updated.append(package);
    }
    if (updated != m_packages) {
        m_packages = updated;
        setPackages(m_packages);
    }
}

void NativeEquipmentServiceSource::publishCreateSeed()
{
    setCreateSeed(EquipmentUiSnapshot::createSeed(m_dye));
}

void NativeEquipmentServiceSource::publishOptions()
{
    setOptions(EquipmentUiSnapshot::options(m_dye, m_history));
}

void NativeEquipmentServiceSource::refresh()
{
    publishOptions();
    if (m_inventoryReadInFlight) {
        m_inventoryDirty = true;
        return;
    }
    if (!m_storage) {
        setInventoryReady(false);
        setErrorMessage(QStringLiteral("Equipment storage is unavailable"));
        setLoading(false);
        return;
    }
    // Keep the last complete inventory available while a refresh runs. Other
    // clients may still be performing a valid operation against that snapshot;
    // storage's atomic preconditions remain the authority for writes. A failed
    // read below invalidates readiness until a later successful refresh.
    setErrorMessage(QString());
    setLoading(true);
    startInventoryRead();
}

void NativeEquipmentServiceSource::startInventoryRead()
{
    if (!m_storage || m_inventoryReadInFlight)
        return;
    m_inventoryReadInFlight = true;
    m_storage->requestInventoryWithCompletion(this,
        [this](bool success, const QVariantList& packages, const QString& reason) {
            m_inventoryReadInFlight = false;
            if (m_inventoryDirty) {
                m_inventoryDirty = false;
                startInventoryRead();
                return;
            }
            if (!success) {
                setErrorMessage(reason.isEmpty()
                    ? QStringLiteral("Equipment inventory is unavailable") : reason);
                setLoading(false);
                setInventoryReady(false);
                return;
            }
            m_packages = packages;
            publishActivePackage();
            // Publish the row list and clear its status before marking it ready,
            // so a replica never observes ready=true with the prior snapshot.
            setPackages(m_packages);
            setErrorMessage(QString());
            setLoading(false);
            setInventoryReady(true);
        });
}

void NativeEquipmentServiceSource::execute(QString requestId, QString operation,
                                           qint64 packageId, QVariantMap fields,
                                           QVariantMap expectedEditable,
                                           bool applyToActiveBag)
{
    const RequestStart start = m_requests.begin(requestId);
    if (start == RequestStart::DuplicateId)
        return;
    if (start != RequestStart::Accepted) {
        if (requestId.isEmpty())
            return;
        const QString reason = start == RequestStart::LimitReached
            ? QStringLiteral("tooManyRequests") : QStringLiteral("invalidRequestId");
        QMetaObject::invokeMethod(this, [this, requestId, reason] {
            emit operationCompleted(requestId, false, -1, reason);
        }, Qt::QueuedConnection);
        return;
    }

    auto reject = [this, requestId](qint64 resultId, const QString& reason) {
        finishOperation(requestId, false, resultId, reason);
    };
    if (!inventoryReady()) {
        reject(packageId, QStringLiteral("inventoryUnavailable"));
        return;
    }
    if (!m_operations) {
        reject(packageId, QStringLiteral("unavailable"));
        return;
    }

    const bool noExpected = expectedEditable.isEmpty();
    if (operation == QLatin1String("create")) {
        if (packageId > 0 || !validEditableFields(fields) || !noExpected) {
            reject(-1, QStringLiteral("invalidRequest"));
            return;
        }
    } else if (operation == QLatin1String("update")) {
        if (packageId <= 0 || !validEditableFields(fields)
            || !isExpectedEditable(expectedEditable) || applyToActiveBag) {
            reject(packageId, QStringLiteral("invalidRequest"));
            return;
        }
    } else if (operation == QLatin1String("select")
               || operation == QLatin1String("activate")) {
        if (packageId <= 0 || !fields.isEmpty() || !noExpected
            || (operation == QLatin1String("select") && applyToActiveBag)) {
            reject(packageId, QStringLiteral("invalidRequest"));
            return;
        }
    } else if (operation == QLatin1String("setGrindAnchor")
               || operation == QLatin1String("clearGrindAnchor")) {
        const QString side = fields.value(QStringLiteral("side")).toString();
        const bool clear = operation == QLatin1String("clearGrindAnchor");
        qint64 expectedPackageId = 0;
        const bool validId = clear
            || positiveSafeId(fields.value(QStringLiteral("expectedPackageId")), &expectedPackageId);
        if (packageId <= 0 || !noExpected || applyToActiveBag
            || fields.value(QStringLiteral("side")).metaType().id() != QMetaType::QString
            || (side != QLatin1String("fine") && side != QLatin1String("coarse"))
            || fields.size() != (clear ? 1 : 2)
            || !validId) {
            reject(packageId, QStringLiteral("invalidRequest"));
            return;
        }
        writeGrindAnchor(requestId, packageId, side, expectedPackageId, clear);
        return;
    } else if (operation == QLatin1String("remove")
               || operation == QLatin1String("restore")
               || operation == QLatin1String("delete")) {
        if (packageId <= 0 || !fields.isEmpty() || !noExpected || applyToActiveBag) {
            reject(packageId, QStringLiteral("invalidRequest"));
            return;
        }
    } else {
        reject(packageId, QStringLiteral("invalidOperation"));
        return;
    }

    if (operation == QLatin1String("create"))
        m_operations->createPackage(fields, applyToActiveBag, requestId);
    else if (operation == QLatin1String("update"))
        m_operations->updatePackage(packageId, fields, expectedEditable, requestId);
    else if (operation == QLatin1String("select")
             || operation == QLatin1String("activate"))
        m_operations->activatePackage(packageId, applyToActiveBag, requestId);
    else if (operation == QLatin1String("remove"))
        m_operations->removePackage(packageId, requestId);
    else if (operation == QLatin1String("restore"))
        m_operations->restorePackage(packageId, requestId);
    else
        m_operations->deletePackage(packageId, requestId);
}

void NativeEquipmentServiceSource::requestPackage(QString requestId, qint64 packageId)
{
    const RequestStart start = m_requests.begin(requestId);
    if (start == RequestStart::DuplicateId)
        return;
    if (start != RequestStart::Accepted) {
        if (!requestId.isEmpty()) {
            const QString reason = start == RequestStart::LimitReached
                ? QStringLiteral("tooManyRequests") : QStringLiteral("invalidRequestId");
            QMetaObject::invokeMethod(this, [this, requestId, packageId, reason] {
                emit packageResult(requestId, packageId, {}, reason);
            }, Qt::QueuedConnection);
        }
        return;
    }
    if (packageId <= 0 || !inventoryReady() || !m_storage) {
        const QString reason = packageId <= 0 ? QStringLiteral("notFound")
                                               : QStringLiteral("inventoryUnavailable");
        m_requests.finish(requestId);
        emit packageResult(requestId, packageId, {}, reason);
        return;
    }
    m_storage->requestPackageWithCompletion(packageId, this,
        [this, requestId](qint64 resultId, bool success, const QVariantMap& package,
                          const QString& failureReason) {
            if (!m_requests.finish(requestId))
                return;
            const QString reason = !success ? QStringLiteral("readFailed")
                : package.isEmpty() ? QStringLiteral("notFound") : QString();
            Q_UNUSED(failureReason)
            emit packageResult(requestId, resultId, success ? package : QVariantMap{}, reason);
        });
}

void NativeEquipmentServiceSource::requestArchived(QString requestId)
{
    const RequestStart start = m_requests.begin(requestId);
    if (start == RequestStart::DuplicateId || requestId.isEmpty())
        return;
    if (start != RequestStart::Accepted) {
        emit archivedResult(requestId, {}, start == RequestStart::LimitReached
            ? QStringLiteral("tooManyRequests") : QStringLiteral("invalidRequestId"));
        return;
    }
    if (!m_storage) {
        m_requests.finish(requestId);
        emit archivedResult(requestId, {}, QStringLiteral("unavailable"));
        return;
    }
    m_storage->requestArchivedWithCompletion(this,
        [this, requestId](bool success, const QVariantList& packages, const QString& reason) {
            if (m_requests.finish(requestId))
                emit archivedResult(requestId, packages, success ? QString() : reason);
        });
}

void NativeEquipmentServiceSource::requestGrindContext(
    QString requestId, QString kind, QVariantMap query)
{
    const RequestStart start = m_requests.begin(requestId);
    if (start == RequestStart::DuplicateId || requestId.isEmpty())
        return;
    if (start != RequestStart::Accepted) {
        emit grindContextResult(requestId, {}, start == RequestStart::LimitReached
            ? QStringLiteral("tooManyRequests") : QStringLiteral("invalidRequestId"));
        return;
    }
    qint64 packageId = 0;
    const bool suggestion = kind == QLatin1String("suggestion");
    if (!validGrindQuery(kind, query, &packageId)) {
        m_requests.finish(requestId);
        emit grindContextResult(requestId, {}, QStringLiteral("invalidRequest"));
        return;
    }
    // Equipment and shots share this database; the storage path also keeps
    // equipment-only callers usable before a ShotHistoryStorage is attached.
    const QString dbPath = m_history ? m_history->databasePath()
        : m_storage ? m_storage->databasePath() : QString();
    if (dbPath.isEmpty()) {
        m_requests.finish(requestId);
        emit grindContextResult(requestId, {}, QStringLiteral("dbUnavailable"));
        return;
    }
    const QPointer<NativeEquipmentServiceSource> self(this);
    QThread* thread = QThread::create([self, requestId, dbPath, query, packageId, suggestion] {
        QVariantMap result;
        bool opened = false;
        withTempDb(dbPath, "equipment_grind_read", [&](QSqlDatabase& db) {
            opened = true;
            result = suggestion
                ? DialingBlocks::grindSuggestionFor(db, packageId,
                    query.value(QStringLiteral("beanBrand")).toString(),
                    query.value(QStringLiteral("beanType")).toString(),
                    query.value(QStringLiteral("roastDate")).toString(),
                    query.value(QStringLiteral("profileTitle")).toString())
                : DialingBlocks::grindCalibrationForPackage(db, packageId);
        });
        QMetaObject::invokeMethod(qApp, [self, requestId, result, opened] {
            if (self && self->m_requests.finish(requestId))
                emit self->grindContextResult(requestId, result,
                    opened ? QString() : QStringLiteral("dbUnavailable"));
        }, Qt::QueuedConnection);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void NativeEquipmentServiceSource::writeGrindAnchor(
    const QString& requestId, qint64 id, const QString& side, qint64 expectedPackageId, bool clear)
{
    const QString dbPath = m_history ? m_history->databasePath()
        : m_storage ? m_storage->databasePath() : QString();
    if (dbPath.isEmpty()) {
        finishOperation(requestId, false, id, QStringLiteral("dbUnavailable"));
        return;
    }
    const QPointer<NativeEquipmentServiceSource> self(this);
    QThread* thread = QThread::create([self, requestId, dbPath, id, side, expectedPackageId, clear] {
        QString reason = QStringLiteral("dbUnavailable");
        qint64 packageId = clear ? id : expectedPackageId;
        withTempDb(dbPath, "equipment_grind_write", [&](QSqlDatabase& db) {
            reason = clear
                ? EquipmentStorage::setCalibrationAnchorStatic(db, id, side, 0)
                    ? QString() : QStringLiteral("writeFailed")
                : DialingBlocks::markGrindCalibrationAnchor(db, id, side, &packageId,
                                                           expectedPackageId);
        });
        QMetaObject::invokeMethod(qApp, [self, requestId, packageId, reason] {
            if (!self) return;
            self->finishOperation(requestId, reason.isEmpty(), packageId, reason);
            if (reason.isEmpty()) emit self->grindCalibrationChanged(packageId);
        }, Qt::QueuedConnection);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}
