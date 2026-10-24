#pragma once

#include "datamigrationservice.h"
#include <QtQml/QQmlEngine>
#include <QtQml/QJSEngine>
#include <QtQmlIntegration/qqmlintegration.h>
#include <type_traits>

struct DataMigrationServiceForeign
{
    Q_GADGET
    QML_FOREIGN(DataMigrationService)
    QML_SINGLETON
    QML_NAMED_ELEMENT(DataMigrationService)
public:
    static DataMigrationService* create(QQmlEngine* engine, QJSEngine* jsEngine)
    {
        static_assert(!std::is_default_constructible_v<DataMigrationService>);
        return DataMigrationService::create(engine, jsEngine);
    }
};

struct DataMigrationStateReplicaForeign
{
    Q_GADGET
    QML_FOREIGN(DataMigrationStateReplica)
    QML_NAMED_ELEMENT(DataMigrationStateReplica)
    QML_UNCREATABLE("DataMigrationStateReplica is owned by DataMigrationService")
};
