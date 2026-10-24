#pragma once

#include "steamservice.h"
#include "rep_steamservice_replica.h"

#include <QtQml/QQmlEngine>
#include <QtQml/QJSEngine>
#include <QtQmlIntegration/qqmlintegration.h>

#include <type_traits>

struct SteamServiceForeign
{
    Q_GADGET
    QML_FOREIGN(SteamService)
    QML_SINGLETON
    QML_NAMED_ELEMENT(SteamService)

public:
    static SteamService* create(QQmlEngine* engine, QJSEngine*)
    {
        static_assert(!std::is_default_constructible_v<SteamService>,
                      "The QML factory must create the engine-owned service instance.");
        return new SteamService(SteamService::publishedNode(), engine);
    }
};

struct SteamStateReplicaForeign
{
    Q_GADGET
    QML_FOREIGN(SteamStateReplica)
    QML_NAMED_ELEMENT(SteamStateReplica)
    QML_UNCREATABLE("SteamStateReplica is owned by SteamService")
};

#ifdef __EMSCRIPTEN__
// Native already registers this model through its published singleton wrapper.
struct SteamGraphModelForeign
{
    Q_GADGET
    QML_FOREIGN(SteamDataModel)
    QML_NAMED_ELEMENT(SteamGraphModel)
    QML_UNCREATABLE("The graph is owned by SteamService")
};
#endif
