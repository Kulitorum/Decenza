#pragma once

#include "remotemachineuisource.h"
#include <type_traits>

class QQmlEngine;
class QJSEngine;

class RemoteUsbScaleManager final : public RemoteMachineUiSource
{
    Q_OBJECT
    QML_NAMED_ELEMENT(UsbScaleManager)
    QML_SINGLETON

    Q_PROPERTY(bool scaleConnected READ scaleConnected NOTIFY scaleConnectedChanged FINAL)

public:
    static RemoteUsbScaleManager* create(QQmlEngine* engine, QJSEngine* jsEngine);
    bool scaleConnected() const { return machineUi().usbScaleConnected; }

signals:
    void scaleConnectedChanged();

private:
    explicit RemoteUsbScaleManager(QObject* parent);
};

static_assert(!std::is_default_constructible_v<RemoteUsbScaleManager>,
              "The QML singleton must use its engine-owned factory.");
