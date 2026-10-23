#pragma once

#include "hostsessionservice.h"
#include <QtQml/QQmlEngine>
#include <QtQml/QJSEngine>
#include <type_traits>

struct HostSessionServiceForeign
{
    Q_GADGET
    QML_FOREIGN(HostSessionService)
    QML_SINGLETON
    QML_NAMED_ELEMENT(HostSessionService)
public:
    static HostSessionService* create(QQmlEngine* engine, QJSEngine* jsEngine)
    {
        static_assert(!std::is_default_constructible_v<HostSessionService>);
        return HostSessionService::create(engine, jsEngine);
    }
};
