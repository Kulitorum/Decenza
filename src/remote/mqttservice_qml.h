#pragma once

#include "mqttservice.h"
#include <QtQml/qqml.h>

struct MqttServiceForeign
{
    Q_GADGET
    QML_FOREIGN(MqttService)
    QML_NAMED_ELEMENT(MqttService)
    QML_SINGLETON
public:
    static MqttService* create(QQmlEngine* engine, QJSEngine* jsEngine)
    {
        return MqttService::create(engine, jsEngine);
    }
};
