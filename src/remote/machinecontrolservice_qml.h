#pragma once

#include "machinecontrolservice.h"

#include <QtQml/QQmlEngine>
#include <QtQml/QJSEngine>
#include <type_traits>

struct MachineControlServiceForeign
{
    Q_GADGET
    QML_FOREIGN(MachineControlService)
    QML_SINGLETON
    QML_NAMED_ELEMENT(MachineControlService)
public:
    static MachineControlService* create(QQmlEngine* engine, QJSEngine*)
    {
        static_assert(!std::is_default_constructible_v<MachineControlService>);
        return new MachineControlService(MachineControlService::publishedNode(), engine);
    }
};

struct MachineControlStateReplicaForeign
{
    Q_GADGET
    QML_FOREIGN(MachineControlStateReplica)
    QML_NAMED_ELEMENT(MachineControlStateReplica)
    QML_UNCREATABLE("MachineControlStateReplica is owned by MachineControlService")
};
