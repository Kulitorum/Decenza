#pragma once

#include "remotemachineuisource.h"
#include <type_traits>

class QQmlEngine;
class QJSEngine;

class RemoteBLEManager final : public RemoteMachineUiSource
{
    Q_OBJECT
    QML_NAMED_ELEMENT(BLEManager)
    QML_SINGLETON

    Q_PROPERTY(bool scaleConnectionFailed READ scaleConnectionFailed NOTIFY machineUiChanged FINAL)

public:
    static RemoteBLEManager* create(QQmlEngine* engine, QJSEngine* jsEngine);
    bool scaleConnectionFailed() const { return machineUi().bleScaleConnectionFailed; }

private:
    explicit RemoteBLEManager(QObject* parent);
};

static_assert(!std::is_default_constructible_v<RemoteBLEManager>,
              "The QML singleton must use its engine-owned factory.");
