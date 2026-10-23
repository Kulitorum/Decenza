#pragma once

#include "hoststatusservice.h"
#include <QtQml/QQmlEngine>
#include <QtQml/QJSEngine>
#include <type_traits>

struct HostStatusServiceForeign
{
    Q_GADGET
    QML_FOREIGN(HostStatusService)
    QML_SINGLETON
    QML_NAMED_ELEMENT(HostStatusService)
public:
    static HostStatusService* create(QQmlEngine* engine, QJSEngine*)
    {
        static_assert(!std::is_default_constructible_v<HostStatusService>);
        return new HostStatusService(HostStatusService::publishedNode(), engine);
    }
};

struct HostStatusStateReplicaForeign
{
    Q_GADGET
    QML_FOREIGN(HostStatusStateReplica)
    QML_NAMED_ELEMENT(HostStatusStateReplica)
    QML_UNCREATABLE("HostStatusStateReplica is owned by HostStatusService")
};
