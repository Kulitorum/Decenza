#pragma once

#include "rep_bagservice_source.h"
#include "serviceoperation.h"

#include <QPointer>
#include <QStringList>

class CoffeeBagStorage;
class SettingsDye;

class NativeBagServiceSource final : public BagStateSimpleSource
{
    Q_OBJECT
public:
    NativeBagServiceSource(CoffeeBagStorage* storage, SettingsDye* dye,
                           QObject* parent = nullptr);

    void refresh(QString requestId) override;
    void refreshFinished(QString requestId) override;
    void refreshFinishedCount(QString requestId) override;
    void requestBag(QString requestId, qint64 bagId) override;
    void execute(QString requestId, QString operation, qint64 bagId,
                 QVariantMap fields, bool propagateBeanBase,
                 QVariantMap expectedEditable, qint64 expectedActiveBagId) override;

private:
    using RequestStart = ServiceRequestIds::Start;

    void refreshAfterStorageChange();
    void startInventoryRead();
    void finishOperation(const QString& requestId, const QString& operation,
                         qint64 bagId, bool success, const QVariantMap& result,
                         const QString& reason);
    void updateActiveBagId();

    QPointer<CoffeeBagStorage> m_storage;
    QPointer<SettingsDye> m_dye;
    ServiceRequestIds m_requests;
    QStringList m_inventoryRequestIds;
    QVariantList m_inventory;
    QVariantList m_finishedBags;
    bool m_inventoryReadInFlight = false;
    bool m_inventoryDirty = false;
    bool m_inventoryReady = false;
    bool m_loading = false;
};
