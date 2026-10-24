#include "remoteblemanager.h"
#include <QQmlEngine>
#include <QJSEngine>

RemoteBLEManager::RemoteBLEManager(QObject* parent) : RemoteMachineUiSource(parent) {}

RemoteBLEManager* RemoteBLEManager::create(QQmlEngine* engine, QJSEngine*)
{
    auto* instance = new RemoteBLEManager(engine);
    QJSEngine::setObjectOwnership(instance, QJSEngine::CppOwnership);
    return instance;
}

