#include "remotede1device.h"
#include <QQmlEngine>
#include <QJSEngine>

RemoteDE1Device::RemoteDE1Device(QObject* parent) : RemoteMachineUiSource(parent) {}

RemoteDE1Device* RemoteDE1Device::create(QQmlEngine* engine, QJSEngine*)
{
    auto* instance = new RemoteDE1Device(engine);
    QJSEngine::setObjectOwnership(instance, QJSEngine::CppOwnership);
    return instance;
}
