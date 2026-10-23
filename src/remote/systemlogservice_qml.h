#pragma once

#include "systemlogservice.h"

#include <QtQml/QQmlEngine>
#include <QtQml/QJSEngine>
#include <type_traits>

struct SystemLogServiceForeign
{
    Q_GADGET
    QML_FOREIGN(SystemLogService)
    QML_SINGLETON
    QML_NAMED_ELEMENT(SystemLogService)
public:
    static SystemLogService* create(QQmlEngine* engine, QJSEngine*)
    {
        static_assert(!std::is_default_constructible_v<SystemLogService>);
        return new SystemLogService(SystemLogService::publishedNode(), engine);
    }
};

struct SystemLogStateReplicaForeign
{
    Q_GADGET
    QML_FOREIGN(SystemLogStateReplica)
    QML_NAMED_ELEMENT(SystemLogStateReplica)
    QML_UNCREATABLE("SystemLogStateReplica is owned by SystemLogService")
};
