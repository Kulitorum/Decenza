#include "remotemaincontroller.h"

#include <QJSEngine>
#include <QQmlEngine>

RemoteMainController::RemoteMainController(QObject* parent)
    : RemoteMachineUiSource(parent)
{
}

void RemoteMainController::setEquipmentStorage(EquipmentService* service)
{
    if (m_equipmentStorage == service)
        return;
    m_equipmentStorage = service;
    emit equipmentStorageChanged();
}

RemoteMainController* RemoteMainController::create(QQmlEngine* engine, QJSEngine*)
{
    auto* controller = new RemoteMainController(engine);
    QJSEngine::setObjectOwnership(controller, QJSEngine::CppOwnership);
    return controller;
}
