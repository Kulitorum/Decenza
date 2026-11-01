#include "remotemachinestate.h"
#include <QQmlEngine>
#include <QJSEngine>

RemoteMachineState::RemoteMachineState(QObject* parent) : RemoteMachineUiSource(parent)
{}

RemoteMachineState* RemoteMachineState::create(QQmlEngine* engine, QJSEngine*)
{
    auto* instance = new RemoteMachineState(engine);
    QJSEngine::setObjectOwnership(instance, QJSEngine::CppOwnership);
    return instance;
}

