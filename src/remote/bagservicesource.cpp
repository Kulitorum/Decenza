#include "bagservicesource.h"
#include "servicevalue.h"

#include "core/settings_dye.h"
#include "core/yieldspec.h"
#include "history/coffeebagstorage.h"

#include <QMetaObject>
#include <QMetaType>
#include <QSet>
#include <cmath>
#include <utility>
#include <limits>

namespace {
const QSet<QString>& bagStringFields()
{
    static const QSet<QString> fields{
        QStringLiteral("roasterName"), QStringLiteral("coffeeName"), QStringLiteral("roastDate"),
        QStringLiteral("roastLevel"), QStringLiteral("kind"), QStringLiteral("beanBaseId"),
        QStringLiteral("beanBaseData"), QStringLiteral("frozenDate"), QStringLiteral("defrostDate"),
        QStringLiteral("storageHint"), QStringLiteral("openedDate"), QStringLiteral("notes"),
        QStringLiteral("grinderSetting"), QStringLiteral("yieldMode"),
        QStringLiteral("visualizerBagId"), QStringLiteral("visualizerRoasterId"),
        QStringLiteral("visualizerSeen")
    };
    return fields;
}

bool validPatch(const QVariantMap& fields, bool create)
{
    if (fields.isEmpty() || fields.size() > 40)
        return false;
    static const QSet<QString> updateOnly{
        QStringLiteral("startWeightG"), QStringLiteral("inInventory"),
        QStringLiteral("equipmentId"), QStringLiteral("rpm"),
        QStringLiteral("doseWeightG"), QStringLiteral("yieldValue"),
        QStringLiteral("visualizerSyncPending"), QStringLiteral("beanBaseDataPatch")
    };
    qsizetype totalStringBytes = 0;
    for (auto it = fields.cbegin(); it != fields.cend(); ++it) {
        const QString& key = it.key();
        const QVariant& value = it.value();
        if (bagStringFields().contains(key)) {
            if (!create && key == QLatin1String("kind"))
                return false;
            if (value.metaType().id() != QMetaType::QString || value.toString().size() > 16384)
                return false;
            totalStringBytes += value.toString().toUtf8().size();
        } else if (updateOnly.contains(key)) {
            if (key == QLatin1String("inInventory") || key == QLatin1String("visualizerSyncPending")) {
                if (value.metaType().id() != QMetaType::Bool)
                    return false;
            } else if (key == QLatin1String("equipmentId") || key == QLatin1String("rpm")) {
                if (!ServiceValue::finiteNumber(value, nullptr) || std::floor(value.toDouble()) != value.toDouble()
                    || std::abs(value.toDouble()) > 9007199254740991.0)
                    return false;
            } else if (key == QLatin1String("beanBaseDataPatch")) {
                if (create || value.metaType().id() != QMetaType::QVariantMap || value.toMap().size() > 128)
                    return false;
                const QVariantMap patch = value.toMap();
                for (auto patchIt = patch.cbegin(); patchIt != patch.cend(); ++patchIt) {
                    if (patchIt.key().isEmpty() || patchIt.key().size() > 256
                        || patchIt.value().toString().size() > 16384)
                        return false;
                    totalStringBytes += patchIt.key().toUtf8().size() + patchIt.value().toString().toUtf8().size();
                }
            } else if (!ServiceValue::finiteNumber(value, nullptr)) {
                return false;
            }
        } else {
            return false;
        }
        if (totalStringBytes > 256 * 1024)
            return false;
    }
    return true;
}

bool numericType(QMetaType type)
{
    switch (type.id()) {
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
    case QMetaType::Double:
    case QMetaType::Float:
        return true;
    default:
        return false;
    }
}

bool normalizeExpectedEditable(const QVariantMap& expected, QVariantMap* normalized)
{
    if (!normalized || expected.isEmpty() || expected.size() > 40)
        return false;
    const QVariantMap canonicalFields = CoffeeBag{}.toVariantMap();
    for (auto it = expected.cbegin(); it != expected.cend(); ++it) {
        const auto canonical = canonicalFields.constFind(it.key());
        if (canonical == canonicalFields.cend())
            return false;
        QVariant value = it.value();
        if (numericType(canonical->metaType())) {
            if (!numericType(value.metaType()))
                return false;
            bool ok = false;
            const double number = value.toDouble(&ok);
            if (!ok || !std::isfinite(number))
                return false;
            const int targetType = canonical->metaType().id();
            if (targetType == QMetaType::Int || targetType == QMetaType::UInt
                || targetType == QMetaType::LongLong || targetType == QMetaType::ULongLong) {
                if (std::floor(number) != number
                    || std::abs(number) > 9007199254740991.0)
                    return false;
            }
            if (canonical->metaType() != value.metaType()
                && !value.convert(canonical->metaType()))
                return false;
            bool convertedOk = false;
            const double convertedNumber = value.toDouble(&convertedOk);
            if (!convertedOk || !std::isfinite(convertedNumber) || convertedNumber != number)
                return false;
        } else if (canonical->metaType() != value.metaType()) {
            return false;
        }
        normalized->insert(it.key(), value);
    }
    return true;
}

QString operationError(const QString& reason)
{
    if (reason == QLatin1String("validationFailed"))
        return QStringLiteral("invalidFields");
    return reason;
}
} // namespace

NativeBagServiceSource::NativeBagServiceSource(CoffeeBagStorage* storage, SettingsDye* dye,
                                               QObject* parent)
    : BagStateSimpleSource(parent)
    , m_storage(storage)
    , m_dye(dye)
{
    if (m_storage) {
        connect(m_storage, &CoffeeBagStorage::bagsChanged, this,
                &NativeBagServiceSource::refreshAfterStorageChange);
        connect(m_storage, &CoffeeBagStorage::bagRestocked, this, &BagStateSimpleSource::bagRestocked);
        connect(m_storage, &CoffeeBagStorage::bagFinished, this, &BagStateSimpleSource::bagFinished);
        connect(m_storage, &CoffeeBagStorage::bagPulledFromVisualizer,
                this, &BagStateSimpleSource::bagPulledFromVisualizer);
        connect(m_storage, &CoffeeBagStorage::bagVisualizerFieldsChanged,
                this, &BagStateSimpleSource::bagVisualizerFieldsChanged);
    }
    if (m_dye) {
        connect(m_dye, &SettingsDye::activeBagIdChanged,
                this, &NativeBagServiceSource::updateActiveBagId);
    }
    updateActiveBagId();
    setInventory({});
    setFinishedBags({});
    setInventoryReady(false);
    setFinishedBagsReady(false);
    setLoading(false);
    refresh(QString());
}

void NativeBagServiceSource::updateActiveBagId()
{
    setActiveBagId(m_dye ? m_dye->activeBagId() : -1);
    QVariantList rows;
    rows.reserve(m_inventory.size());
    const qint64 activeId = m_dye ? m_dye->activeBagId() : -1;
    for (const QVariant& rowValue : m_inventory) {
        QVariantMap row = rowValue.toMap();
        row.insert(QStringLiteral("isActive"), row.value(QStringLiteral("id")).toLongLong() == activeId);
        rows.append(row);
    }
    if (rows != m_inventory) {
        m_inventory = rows;
        setInventory(m_inventory);
    }
}

void NativeBagServiceSource::refreshAfterStorageChange()
{
    emit bagsChanged();
    if (m_inventoryReadInFlight) {
        m_inventoryDirty = true;
        return;
    }
    refresh(QString());
}

void NativeBagServiceSource::refresh(QString requestId)
{
    if (!requestId.isEmpty()) {
        const RequestStart start = m_requests.begin(requestId);
        if (start != RequestStart::Accepted) {
            if (start != RequestStart::DuplicateId) {
                const QString reason = start == RequestStart::LimitReached
                    ? QStringLiteral("tooManyRequests") : QStringLiteral("invalidRequestId");
                QMetaObject::invokeMethod(this, [this, requestId, reason] {
                    emit inventoryResult(requestId, false, {}, reason);
                }, Qt::QueuedConnection);
            }
            return;
        }
        m_inventoryRequestIds.append(requestId);
    }
    if (!m_storage) {
        setErrorMessage(QStringLiteral("Bag storage is unavailable"));
        setInventoryReady(false);
        setLoading(false);
        const auto requests = std::exchange(m_inventoryRequestIds, {});
        for (const QString& id : requests) {
            m_requests.finish(id);
            emit inventoryResult(id, false, {}, errorMessage());
        }
        return;
    }
    setErrorMessage(QString());
    setLoading(true);
    startInventoryRead();
}

void NativeBagServiceSource::startInventoryRead()
{
    if (!m_storage || m_inventoryReadInFlight)
        return;
    m_inventoryReadInFlight = true;
    m_storage->requestInventoryWithCompletion(this,
        [this](bool success, const QVariantList& rows, const QString& reason) {
            m_inventoryReadInFlight = false;
            if (m_inventoryDirty) {
                m_inventoryDirty = false;
                startInventoryRead();
                return;
            }
            if (!success) {
                setErrorMessage(reason.isEmpty() ? QStringLiteral("Bag inventory is unavailable") : reason);
                setInventoryReady(false);
                setLoading(false);
                const auto requests = std::exchange(m_inventoryRequestIds, {});
                for (const QString& id : requests) {
                    m_requests.finish(id);
                    emit inventoryResult(id, false, {}, errorMessage());
                }
                return;
            }
            m_inventory = rows;
            updateActiveBagId();
            setInventory(m_inventory);
            setInventoryReady(true);
            setErrorMessage(QString());
            setLoading(false);
            const auto requests = std::exchange(m_inventoryRequestIds, {});
            for (const QString& id : requests) {
                m_requests.finish(id);
                emit inventoryResult(id, true, m_inventory, QString());
            }
        });
}

void NativeBagServiceSource::refreshFinished(QString requestId)
{
    const RequestStart start = m_requests.begin(requestId);
    if (start != RequestStart::Accepted) {
        if (start != RequestStart::DuplicateId) {
            const QString reason = start == RequestStart::LimitReached
                ? QStringLiteral("tooManyRequests") : QStringLiteral("invalidRequestId");
            QMetaObject::invokeMethod(this, [this, requestId, reason] {
                emit finishedBagsResult(requestId, false, {}, reason);
            }, Qt::QueuedConnection);
        }
        return;
    }
    if (!m_storage) {
        m_requests.finish(requestId);
        emit finishedBagsResult(requestId, false, {}, QStringLiteral("storageUnavailable"));
        return;
    }
    m_storage->requestInventoryWithCompletion(this,
        [this, requestId](bool success, const QVariantList& rows, const QString& reason) {
            if (!m_requests.finish(requestId))
                return;
            setFinishedBagsReady(success);
            if (success) {
                setFinishedBags(rows);
                emit finishedBagsResult(requestId, true, rows, QString());
            } else {
                emit finishedBagsResult(requestId, false, {}, reason);
            }
        }, true);
}

void NativeBagServiceSource::refreshFinishedCount(QString requestId)
{
    const RequestStart start = m_requests.begin(requestId);
    if (start != RequestStart::Accepted) {
        if (start != RequestStart::DuplicateId) {
            const QString reason = start == RequestStart::LimitReached
                ? QStringLiteral("tooManyRequests") : QStringLiteral("invalidRequestId");
            QMetaObject::invokeMethod(this, [this, requestId, reason] {
                emit finishedBagCountResult(requestId, false, 0, reason);
            }, Qt::QueuedConnection);
        }
        return;
    }
    if (!m_storage) {
        m_requests.finish(requestId);
        emit finishedBagCountResult(requestId, false, 0, QStringLiteral("storageUnavailable"));
        return;
    }
    m_storage->requestFinishedBagCountWithCompletion(this,
        [this, requestId](bool success, int count, const QString& reason) {
            if (m_requests.finish(requestId))
                emit finishedBagCountResult(requestId, success, count, reason);
        });
}

void NativeBagServiceSource::requestBag(QString requestId, qint64 bagId)
{
    const RequestStart start = m_requests.begin(requestId);
    if (start != RequestStart::Accepted) {
        if (start != RequestStart::DuplicateId && !requestId.isEmpty()) {
            const QString reason = start == RequestStart::LimitReached
                ? QStringLiteral("tooManyRequests") : QStringLiteral("invalidRequestId");
            QMetaObject::invokeMethod(this, [this, requestId, bagId, reason] {
                emit bagResult(requestId, bagId, false, {}, reason);
            }, Qt::QueuedConnection);
        }
        return;
    }
    if (bagId <= 0 || !m_storage) {
        m_requests.finish(requestId);
        emit bagResult(requestId, bagId, false, {}, bagId <= 0
            ? QStringLiteral("notFound") : QStringLiteral("storageUnavailable"));
        return;
    }
    m_storage->requestBagWithCompletion(bagId, this,
        [this, requestId](qint64 resultId, bool success, const QVariantMap& bag, const QString& reason) {
            if (!m_requests.finish(requestId))
                return;
            emit bagResult(requestId, resultId, success, success ? bag : QVariantMap(), reason);
        });
}

void NativeBagServiceSource::finishOperation(const QString& requestId, const QString& operation,
                                              qint64 bagId, bool success,
                                              const QVariantMap& result, const QString& reason)
{
    if (!m_requests.finish(requestId))
        return;
    emit operationCompleted(requestId, operation, bagId, success, result,
                            operationError(reason));
}

void NativeBagServiceSource::execute(QString requestId, QString operation, qint64 bagId,
                                     QVariantMap fields, bool propagateBeanBase,
                                     QVariantMap expectedEditable, qint64 expectedActiveBagId)
{
    const RequestStart start = m_requests.begin(requestId);
    if (start != RequestStart::Accepted) {
        if (start != RequestStart::DuplicateId && !requestId.isEmpty()) {
            const QString reason = start == RequestStart::LimitReached
                ? QStringLiteral("tooManyRequests") : QStringLiteral("invalidRequestId");
            QMetaObject::invokeMethod(this, [this, requestId, operation, bagId, reason] {
                emit operationCompleted(requestId, operation, bagId, false, {}, reason);
            }, Qt::QueuedConnection);
        }
        return;
    }
    auto reject = [this, requestId, operation, bagId](const QString& reason) {
        finishOperation(requestId, operation, bagId, false, {}, reason);
    };
    if (!m_storage) {
        reject(QStringLiteral("storageUnavailable"));
        return;
    }
    if (operation == QLatin1String("activate")) {
        if (!m_dye || (bagId != -1 && (bagId <= 0 || bagId > std::numeric_limits<int>::max()))
            || !fields.isEmpty() || propagateBeanBase || !expectedEditable.isEmpty()
            || expectedActiveBagId != -1) {
            reject(QStringLiteral("invalidRequest"));
            return;
        }
        if (bagId == -1) {
            m_dye->setActiveBagId(-1);
            finishOperation(requestId, operation, bagId, true, {}, {});
            return;
        }
        m_storage->requestBagWithCompletion(bagId, this,
            [this, requestId, operation](qint64 id, bool success, const QVariantMap& bag, const QString& reason) {
                if (!success || bag.isEmpty() || !m_dye) {
                    finishOperation(requestId, operation, id, false, {},
                        reason.isEmpty() ? QStringLiteral("notFound") : reason);
                    return;
                }
                m_dye->setActiveBagId(static_cast<int>(id));
                finishOperation(requestId, operation, id, true, bag, {});
            });
        return;
    }
    if (operation == QLatin1String("create")) {
        if (bagId > 0 || propagateBeanBase || !expectedEditable.isEmpty() || expectedActiveBagId != -1
            || !validPatch(fields, true)
            || !CoffeeBag::writeError({}, fields, QDate::currentDate()).isEmpty()) {
            reject(QStringLiteral("invalidFields"));
            return;
        }
        m_storage->requestCreateBagWithCompletion(fields, this,
            [this, requestId, operation](qint64 resultId, bool success,
                                         const QVariantMap& bag, const QString& reason) {
                finishOperation(requestId, operation, resultId, success, bag, reason);
            });
        return;
    }
    if (operation == QLatin1String("update") || operation == QLatin1String("markEmpty")
        || operation == QLatin1String("restore")) {
        if (bagId <= 0 || (operation != QLatin1String("update") &&
            (!fields.isEmpty() || propagateBeanBase || !expectedEditable.isEmpty() || expectedActiveBagId != -1))) {
            reject(QStringLiteral("invalidRequest"));
            return;
        }
        if (expectedActiveBagId != -1 && (operation != QLatin1String("update")
            || expectedActiveBagId != bagId || !m_dye || m_dye->activeBagId() != expectedActiveBagId)) {
            reject(QStringLiteral("staleData"));
            return;
        }
        QVariantMap normalizedExpected;
        if ((!expectedEditable.isEmpty()
             && !normalizeExpectedEditable(expectedEditable, &normalizedExpected))
            || (expectedActiveBagId != -1 && expectedEditable.isEmpty())) {
            reject(QStringLiteral("invalidFields"));
            return;
        }
        const bool writesYieldSpec = fields.contains(QStringLiteral("yieldValue"))
            || fields.contains(QStringLiteral("yieldMode"));
        if (writesYieldSpec && !expectedEditable.isEmpty()) {
            // A compare-and-set yield update must carry both displayed fields
            // and their captured values. When the caller supplies an active
            // bag identity, the check above binds the write to that bag.
            if (operation != QLatin1String("update") || !fields.contains(QStringLiteral("yieldValue"))
                || !fields.contains(QStringLiteral("yieldMode"))
                || !normalizedExpected.contains(QStringLiteral("yieldValue"))
                || !normalizedExpected.contains(QStringLiteral("yieldMode"))) {
                reject(QStringLiteral("invalidFields"));
                return;
            }
        }
        // Validate update values before any normalization; malformed or
        // non-finite input must not turn into an apparently valid zero anchor.
        const bool updatePatchValidated = operation == QLatin1String("update");
        if (updatePatchValidated && !validPatch(fields, false)) {
            reject(QStringLiteral("invalidFields"));
            return;
        }
        if (writesYieldSpec && !expectedEditable.isEmpty()) {
            const double value = fields.value(QStringLiteral("yieldValue")).toDouble();
            const QString mode = YieldSpec::normalizedMode(fields.value(QStringLiteral("yieldMode")).toString());
            const bool hasValue = YieldSpec::isSet(mode) && value > 0;
            fields.insert(QStringLiteral("yieldValue"), hasValue ? YieldSpec::clampValue(mode, value) : 0.0);
            fields.insert(QStringLiteral("yieldMode"), hasValue ? mode : YieldSpec::modeNone());
        }
        if (operation == QLatin1String("markEmpty"))
            fields.insert(QStringLiteral("inInventory"), false);
        else if (operation == QLatin1String("restore"))
            fields.insert(QStringLiteral("inInventory"), true);
        if (!updatePatchValidated && !validPatch(fields, false)) {
            reject(QStringLiteral("invalidFields"));
            return;
        }
        if (propagateBeanBase && !fields.contains(QStringLiteral("beanBaseId"))
            && !fields.contains(QStringLiteral("beanBaseData"))
            && !fields.contains(QStringLiteral("beanBaseDataPatch"))) {
            reject(QStringLiteral("invalidRequest"));
            return;
        }
        m_storage->requestUpdateBagWithCompletion(bagId, fields, propagateBeanBase, this,
            [this, requestId, operation](qint64 resultId, bool success,
                                         const QString& refusal, const QString& reason) {
                finishOperation(requestId, operation, resultId, success,
                                {{QStringLiteral("refusal"), refusal}}, reason);
            }, normalizedExpected);
        return;
    }
    if (operation == QLatin1String("delete")) {
        if (bagId <= 0 || !fields.isEmpty() || propagateBeanBase || !expectedEditable.isEmpty()
            || expectedActiveBagId != -1) {
            reject(QStringLiteral("invalidRequest"));
            return;
        }
        m_storage->requestDeleteBagWithCompletion(bagId, this,
            [this, requestId, operation](qint64 resultId, bool success, const QString& reason) {
                finishOperation(requestId, operation, resultId, success, {}, reason);
            });
        return;
    }
    if (operation == QLatin1String("markAiPageSearched")) {
        if (bagId <= 0 || !fields.isEmpty() || propagateBeanBase || !expectedEditable.isEmpty()
            || expectedActiveBagId != -1) {
            reject(QStringLiteral("invalidRequest"));
            return;
        }
        m_storage->requestMarkAiPageSearchedWithCompletion(bagId, this,
            [this, requestId, operation](qint64 resultId, bool success, const QString& reason) {
                finishOperation(requestId, operation, resultId, success, {}, reason);
            });
        return;
    }
    if (operation == QLatin1String("touchLastUsed")) {
        if (bagId <= 0 || !fields.isEmpty() || propagateBeanBase || !expectedEditable.isEmpty()
            || expectedActiveBagId != -1) {
            reject(QStringLiteral("invalidRequest"));
            return;
        }
        m_storage->requestTouchLastUsedWithCompletion(bagId, this,
            [this, requestId, operation](qint64 resultId, bool success, const QString& reason) {
                finishOperation(requestId, operation, resultId, success, {}, reason);
            });
        return;
    }
    reject(QStringLiteral("invalidOperation"));
}
