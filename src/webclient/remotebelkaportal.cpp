#include "remotebelkaportal.h"

#include "remotesessionbackend.h"

#include <QQmlEngine>
#include <QJSEngine>

RemoteBelkaPortal::RemoteBelkaPortal(QObject* parent)
    : RemoteMachineUiSource(parent)
{}

RemoteBelkaPortal* RemoteBelkaPortal::create(QQmlEngine* engine, QJSEngine*)
{
    auto* instance = new RemoteBelkaPortal(engine);
    QJSEngine::setObjectOwnership(instance, QJSEngine::CppOwnership);
    return instance;
}

