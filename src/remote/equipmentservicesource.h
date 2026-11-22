#pragma once

#include "rep_equipmentservice_source.h"
#include "serviceoperation.h"

#include <QPointer>
#include <QSet>

#include <functional>

class EquipmentStorage;
class EquipmentUiOperations;
class SettingsDye;
class ShotHistoryStorage;

class NativeEquipmentServiceSource final : public EquipmentStateSimpleSource
{
    Q_OBJECT
public:
    NativeEquipmentServiceSource(EquipmentStorage* storage, SettingsDye* dye,
                                 ShotHistoryStorage* history, QObject* parent = nullptr);

    void refresh() override;
    void execute(QString requestId, QString operation, qint64 packageId,
                 QVariantMap fields, QVariantMap expectedEditable,
                 bool makeActive) override;
    void requestPackage(QString requestId, qint64 packageId) override;
    void requestArchived(QString requestId) override;
    void requestGrindContext(QString requestId, QString kind, QVariantMap query) override;
    // Resolves a profile title to its KB id as the profile catalog does (title, editor
    // type, shape). Without one no profile is on the chart: matching the title alone
    // put a renamed D-Flow profile on the wrong row.
    void setProfileKbResolver(std::function<QString(const QString&)> resolver)
    {
        m_profileKbId = std::move(resolver);
    }

private:
    using RequestStart = ServiceRequestIds::Start;
    void finishOperation(const QString& requestId, bool success, qint64 resultId,
                         const QString& reason);
    void publishActivePackage();
    void publishCreateSeed();
    void publishOptions();
    void startInventoryRead();
    void writeGrindAnchor(const QString& requestId, qint64 id,
                          const QString& side, qint64 expectedPackageId, bool clear,
                          bool replacePair);

    QPointer<EquipmentStorage> m_storage;
    QPointer<SettingsDye> m_dye;
    QPointer<ShotHistoryStorage> m_history;
    QPointer<EquipmentUiOperations> m_operations;
    std::function<QString(const QString&)> m_profileKbId;
    ServiceRequestIds m_requests;
    QVariantList m_packages;
    bool m_inventoryReadInFlight = false;
    bool m_inventoryDirty = false;
};
