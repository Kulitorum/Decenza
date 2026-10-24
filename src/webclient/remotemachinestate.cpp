#include "remotemachinestate.h"
#include <QQmlEngine>
#include <QJSEngine>

RemoteMachineState::RemoteMachineState(QObject* parent) : RemoteMachineUiSource(parent)
{
    connect(this, &RemoteMachineUiSource::machineUiChanged, this, [this] {
        const double current = scaleWeight();
        if (current == m_lastScaleWeight)
            return;
        m_lastScaleWeight = current;
        emit scaleWeightChanged();
    });
}

RemoteMachineState* RemoteMachineState::create(QQmlEngine* engine, QJSEngine*)
{
    auto* instance = new RemoteMachineState(engine);
    QJSEngine::setObjectOwnership(instance, QJSEngine::CppOwnership);
    return instance;
}

