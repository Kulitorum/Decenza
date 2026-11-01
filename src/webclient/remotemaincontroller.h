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
    Q_PROPERTY(bool steamHeaterOn READ steamHeaterOn NOTIFY steamHeaterOnChanged FINAL)
    Q_PROPERTY(bool temperatureIsRealOverride READ temperatureIsRealOverride NOTIFY temperatureIsRealOverrideChanged FINAL)
    Q_PROPERTY(bool yieldIsRealOverride READ yieldIsRealOverride NOTIFY yieldIsRealOverrideChanged FINAL)
    Q_PROPERTY(double activeBaselineYieldG READ activeBaselineYieldG NOTIFY activeBaselineYieldGChanged FINAL)
    Q_PROPERTY(double activeRecipeTempOffsetC READ activeRecipeTempOffsetC NOTIFY activeRecipeTempOffsetCChanged FINAL)
    Q_PROPERTY(double filteredGoalPressure READ filteredGoalPressure NOTIFY filteredGoalPressureChanged FINAL)
    Q_PROPERTY(double filteredGoalFlow READ filteredGoalFlow NOTIFY filteredGoalFlowChanged FINAL)

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
    void steamHeaterOnChanged();
    void temperatureIsRealOverrideChanged();
    void yieldIsRealOverrideChanged();
    void activeBaselineYieldGChanged();
    void activeRecipeTempOffsetCChanged();
    void filteredGoalPressureChanged();
    void filteredGoalFlowChanged();
    void equipmentStorageChanged();
};

static_assert(!std::is_default_constructible_v<RemoteMainController>,
              "RemoteMainController is a QML_SINGLETON with a create() factory: Qt must not "
              "default-construct it instead.");
