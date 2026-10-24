#pragma once

#include "updateservice.h"
#include <QtQml/QQmlEngine>
#include <QtQml/QJSEngine>
#include <type_traits>

struct UpdateServiceForeign
{
    Q_GADGET
    QML_FOREIGN(UpdateService)
    QML_SINGLETON
    QML_NAMED_ELEMENT(UpdateService)
public:
    static UpdateService* create(QQmlEngine* engine, QJSEngine*)
    {
        static_assert(!std::is_default_constructible_v<UpdateService>);
        return new UpdateService(UpdateService::publishedNode(), engine);
    }
};

struct UpdateStateReplicaForeign
{
    Q_GADGET
    QML_FOREIGN(UpdateStateReplica)
    QML_NAMED_ELEMENT(UpdateStateReplica)
    QML_UNCREATABLE("UpdateStateReplica is owned by UpdateService")
};
