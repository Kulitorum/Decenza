#pragma once

#include "liveshotgraphservice.h"
#include "rep_liveshotgraphservice_replica.h"

#include <QtQml/QQmlEngine>
#include <QtQml/QJSEngine>
#include <QtQmlIntegration/qqmlintegration.h>
#include <type_traits>

struct LiveShotGraphServiceForeign
{
    Q_GADGET
    QML_FOREIGN(LiveShotGraphService)
    QML_SINGLETON
    QML_NAMED_ELEMENT(LiveShotGraph)
public:
    static LiveShotGraphService* create(QQmlEngine* engine, QJSEngine*)
    {
        static_assert(!std::is_default_constructible_v<LiveShotGraphService>,
                      "The QML factory must create the engine-owned service instance.");
        return new LiveShotGraphService(LiveShotGraphService::publishedNode(), engine);
    }
};

struct LiveShotGraphStateReplicaForeign
{
    Q_GADGET
    QML_FOREIGN(LiveShotGraphStateReplica)
    QML_NAMED_ELEMENT(LiveShotGraphStateReplica)
    QML_UNCREATABLE("LiveShotGraphStateReplica is owned by LiveShotGraph")
};
