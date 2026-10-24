#include "equipmentuioperations.h"
#include <optional>

void EquipmentUiOperations::createPackage(const QVariantMap& fields, bool applyToActiveBag,
                                          const QString& requestId)
{
    if (!m_storage || (applyToActiveBag && !m_dye)) {
        emit operationCompleted(requestId, false, -1, QStringLiteral("unavailable"));
        return;
    }
    const QPointer<SettingsDye> dye = m_dye;
    m_storage->requestCreatePackageWithCompletion(fields, this,
        [this, dye, applyToActiveBag, requestId](qint64 id, const QVariantMap& package) {
            if (id > 0 && applyToActiveBag && dye)
                dye->switchToEquipment(package);
            emit operationCompleted(requestId, id > 0, id, package.value(QStringLiteral("error")).toString());
        });
}

void EquipmentUiOperations::updatePackage(qint64 id, const QVariantMap& fields,
                                          const QVariantMap& expectedEditable, const QString& requestId)
{
    if (!m_storage) {
        emit operationCompleted(requestId, false, id, QStringLiteral("unavailable"));
        return;
    }
    const QPointer<SettingsDye> dye = m_dye;
    const bool wasActive = dye && dye->activeEquipmentId() == id;
    m_storage->requestUpdatePackageWithCompletion(id, fields, this,
        [this, dye, wasActive, requestId](qint64 requestedId, qint64 resultId, bool success, const QString& reason) {
            if (success && wasActive && resultId > 0 && dye && dye->activeEquipmentId() == requestedId)
                dye->setActiveEquipmentId(resultId);
            emit operationCompleted(requestId, success, resultId, reason);
        }, std::nullopt, expectedEditable);
}

void EquipmentUiOperations::activatePackage(qint64 id, bool applyToActiveBag, const QString& requestId)
{
    if (!m_storage || (applyToActiveBag && !m_dye) || id <= 0) {
        emit operationCompleted(requestId, false, id, QStringLiteral("unavailable"));
        return;
    }
    if (!applyToActiveBag) {
        emit operationCompleted(requestId, true, id, {});
        return;
    }
    const QPointer<SettingsDye> dye = m_dye;
    m_storage->requestPackageWithCompletion(id, this,
        [this, dye, requestId](qint64 resultId, bool success, const QVariantMap& package, const QString&) {
            if (!success || package.isEmpty() || !dye) {
                emit operationCompleted(requestId, false, resultId,
                    success ? QStringLiteral("notFound") : QStringLiteral("readFailed"));
                return;
            }
            dye->switchToEquipment(package);
            emit operationCompleted(requestId, true, resultId, {});
        });
}

void EquipmentUiOperations::removePackage(qint64 id, const QString& requestId)
{
    if (!m_storage) {
        emit operationCompleted(requestId, false, id, QStringLiteral("unavailable"));
        return;
    }
    m_storage->requestUpdatePackageWithCompletion(id, {{QStringLiteral("inInventory"), false}}, this,
        [this, requestId](qint64, qint64 resultId, bool success, const QString& reason) {
            emit operationCompleted(requestId, success, resultId, reason);
        });
}

void EquipmentUiOperations::restorePackage(qint64 id, const QString& requestId)
{
    if (!m_storage) {
        emit operationCompleted(requestId, false, id, QStringLiteral("unavailable"));
        return;
    }
    m_storage->requestRestorePackageWithCompletion(id, this,
        [this, requestId](qint64, qint64 resultId, bool success, const QString& reason) {
            emit operationCompleted(requestId, success, resultId, reason);
        });
}

void EquipmentUiOperations::deletePackage(qint64 id, const QString& requestId)
{
    if (!m_storage) {
        emit operationCompleted(requestId, false, id, QStringLiteral("unavailable"));
        return;
    }
    m_storage->requestDeletePackageWithCompletion(id, this,
        [this, requestId](qint64 resultId, bool success) {
            emit operationCompleted(requestId, success, resultId, success ? QString() : QStringLiteral("refused"));
        });
}
