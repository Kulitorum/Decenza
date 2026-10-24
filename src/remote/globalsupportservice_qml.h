#pragma once

#include "globalsupportservice.h"

#include <QtQml/QQmlEngine>
#include <QtQml/QJSEngine>
#include <type_traits>

struct GlobalSupportServiceForeign
{
    Q_GADGET
    QML_FOREIGN(GlobalSupportService)
    QML_SINGLETON
    QML_NAMED_ELEMENT(GlobalSupportService)
public:
    static GlobalSupportService* create(QQmlEngine* engine, QJSEngine*)
    {
        static_assert(!std::is_default_constructible_v<GlobalSupportService>);
        return new GlobalSupportService(GlobalSupportService::publishedNode(), engine);
    }
};

struct GlobalSupportStateReplicaForeign
{
    Q_GADGET
    QML_FOREIGN(GlobalSupportStateReplica)
    QML_NAMED_ELEMENT(GlobalSupportStateReplica)
    QML_UNCREATABLE("GlobalSupportStateReplica is owned by GlobalSupportService")
};
