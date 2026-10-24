#include "remoteusbscalemanager.h"
#include <QQmlEngine>
#include <QJSEngine>

RemoteUsbScaleManager::RemoteUsbScaleManager(QObject* parent) : RemoteMachineUiSource(parent) {}

RemoteUsbScaleManager* RemoteUsbScaleManager::create(QQmlEngine* engine, QJSEngine*)
{
    auto* instance = new RemoteUsbScaleManager(engine);
    QJSEngine::setObjectOwnership(instance, QJSEngine::CppOwnership);
    return instance;
}
