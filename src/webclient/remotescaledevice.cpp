#include "remotescaledevice.h"
#include <QQmlEngine>
#include <QJSEngine>

RemoteScaleDevice::RemoteScaleDevice(QObject* parent) : RemoteMachineUiSource(parent) {}

RemoteScaleDevice* RemoteScaleDevice::create(QQmlEngine* engine, QJSEngine*)
{
    auto* instance = new RemoteScaleDevice(engine);
    QJSEngine::setObjectOwnership(instance, QJSEngine::CppOwnership);
    return instance;
}
