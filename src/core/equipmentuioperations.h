#pragma once

#include <QObject>
#include <QPointer>
#include <QVariantMap>
#include <QtQmlIntegration/qqmlintegration.h>
#include "../history/equipmentstorage.h"
#include "settings_dye.h"

// Native operation adapter beneath the shared equipment UI. Request-scoped
// completions prevent another caller's storage signals from closing a dialog.
class EquipmentUiOperations : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(EquipmentStorage* storage READ storage WRITE setStorage NOTIFY storageChanged)
    Q_PROPERTY(SettingsDye* dye READ dye WRITE setDye NOTIFY dyeChanged)
public:
    explicit EquipmentUiOperations(QObject* parent = nullptr) : QObject(parent) {}
    EquipmentStorage* storage() const { return m_storage; }
    SettingsDye* dye() const { return m_dye; }
    void setStorage(EquipmentStorage* value) { if (m_storage != value) { m_storage = value; emit storageChanged(); } }
    void setDye(SettingsDye* value) { if (m_dye != value) { m_dye = value; emit dyeChanged(); } }
    Q_INVOKABLE void createPackage(const QVariantMap& fields, bool applyToActiveBag, const QString& requestId);
    Q_INVOKABLE void updatePackage(qint64 id, const QVariantMap& fields,
                                  const QVariantMap& expectedEditable, const QString& requestId);
    Q_INVOKABLE void activatePackage(qint64 id, bool applyToActiveBag, const QString& requestId);
    Q_INVOKABLE void removePackage(qint64 id, const QString& requestId);
    Q_INVOKABLE void restorePackage(qint64 id, const QString& requestId);
    Q_INVOKABLE void deletePackage(qint64 id, const QString& requestId);
signals:
    void storageChanged();
    void dyeChanged();
    void operationCompleted(const QString& requestId, bool success, qint64 resultId, const QString& reason);
private:
    QPointer<EquipmentStorage> m_storage;
    QPointer<SettingsDye> m_dye;
};
