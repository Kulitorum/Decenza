#pragma once

#include <QObject>
#include <QPointer>
#include <type_traits>
#include <QtQmlIntegration/qqmlintegration.h>

#include "remotemachineuisource.h"
#include "remote/equipmentservice.h"

class QQmlEngine;
class QJSEngine;

// The Wasm target exports the native QML name and binds its equipment service
// to the same authenticated QtRO connection used by the other host services.
class RemoteMainController final : public RemoteMachineUiSource
{
    Q_OBJECT
    QML_NAMED_ELEMENT(MainController)
    QML_SINGLETON

    Q_PROPERTY(EquipmentService* equipmentStorage READ equipmentStorage WRITE setEquipmentStorage NOTIFY equipmentStorageChanged FINAL)
    Q_PROPERTY(bool steamHeaterOn READ steamHeaterOn NOTIFY machineUiChanged FINAL)
    Q_PROPERTY(bool temperatureIsRealOverride READ temperatureIsRealOverride NOTIFY machineUiChanged FINAL)
    Q_PROPERTY(bool yieldIsRealOverride READ yieldIsRealOverride NOTIFY machineUiChanged FINAL)
    Q_PROPERTY(double activeBaselineYieldG READ activeBaselineYieldG NOTIFY machineUiChanged FINAL)
    Q_PROPERTY(double activeRecipeTempOffsetC READ activeRecipeTempOffsetC NOTIFY machineUiChanged FINAL)
    Q_PROPERTY(double filteredGoalPressure READ filteredGoalPressure NOTIFY machineUiChanged FINAL)
    Q_PROPERTY(double filteredGoalFlow READ filteredGoalFlow NOTIFY machineUiChanged FINAL)

public:
    static RemoteMainController* create(QQmlEngine* engine, QJSEngine* jsEngine);

    EquipmentService* equipmentStorage() const { return m_equipmentStorage.data(); }
    void setEquipmentStorage(EquipmentService* service);
    bool steamHeaterOn() const { return machineUi().steamHeaterOn; }
    bool temperatureIsRealOverride() const { return machineUi().temperatureIsRealOverride; }
    bool yieldIsRealOverride() const { return machineUi().yieldIsRealOverride; }
    double activeBaselineYieldG() const { return machineUi().activeBaselineYieldG; }
    double activeRecipeTempOffsetC() const { return machineUi().activeRecipeTempOffsetC; }
    double filteredGoalPressure() const { return machineUi().filteredGoalPressure; }
    double filteredGoalFlow() const { return machineUi().filteredGoalFlow; }

private:
    explicit RemoteMainController(QObject* parent);
    QPointer<EquipmentService> m_equipmentStorage;

signals:
    void equipmentStorageChanged();
};

static_assert(!std::is_default_constructible_v<RemoteMainController>,
              "RemoteMainController is a QML_SINGLETON with a create() factory: Qt must not "
              "default-construct it instead.");
