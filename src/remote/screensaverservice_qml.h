#pragma once

#include "screensaverservice.h"
#include "rep_screensaverservice_replica.h"

#include <QtQml/QQmlEngine>
#include <QtQml/QJSEngine>
#include <type_traits>

struct ScreensaverServiceForeign
{
    Q_GADGET
    QML_FOREIGN(ScreensaverService)
    QML_SINGLETON
    QML_NAMED_ELEMENT(ScreensaverService)
public:
    static ScreensaverService* create(QQmlEngine* engine, QJSEngine*)
    {
        static_assert(!std::is_default_constructible_v<ScreensaverService>);
        return new ScreensaverService(ScreensaverService::publishedNode(), engine);
    }
};

struct ScreensaverStateReplicaForeign
{
    Q_GADGET
    QML_FOREIGN(ScreensaverStateReplica)
    QML_NAMED_ELEMENT(ScreensaverStateReplica)
    QML_UNCREATABLE("ScreensaverStateReplica is owned by ScreensaverService")
};
