#pragma once

#include "settingsconnectionsservice.h"

#include <QtQml/QQmlEngine>
#include <QtQml/QJSEngine>
#include <type_traits>

struct SettingsConnectionsServiceForeign
{
    Q_GADGET
    QML_FOREIGN(SettingsConnectionsService)
    QML_SINGLETON
    QML_NAMED_ELEMENT(SettingsConnectionsService)
public:
    static SettingsConnectionsService* create(QQmlEngine* engine, QJSEngine*)
    {
        static_assert(!std::is_default_constructible_v<SettingsConnectionsService>);
        return new SettingsConnectionsService(
            SettingsConnectionsService::publishedNode(), engine);
    }
};

struct ConnectionsStateReplicaForeign
{
    Q_GADGET
    QML_FOREIGN(ConnectionsStateReplica)
    QML_NAMED_ELEMENT(ConnectionsStateReplica)
    QML_UNCREATABLE("ConnectionsStateReplica is owned by SettingsConnectionsService")
};
