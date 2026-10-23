#pragma once
#include "shotuploadservice.h"
#include <QtQml/QQmlEngine>
#include <QtQml/QJSEngine>
#include <type_traits>

struct ShotUploadServiceForeign
{
    Q_GADGET
    QML_FOREIGN(ShotUploadService)
    QML_SINGLETON
    QML_NAMED_ELEMENT(ShotUploadService)
public:
    static ShotUploadService* create(QQmlEngine* engine, QJSEngine*)
    {
        static_assert(!std::is_default_constructible_v<ShotUploadService>);
        return new ShotUploadService(ShotUploadService::publishedNode(), engine);
    }
};

struct ShotUploadStateReplicaForeign
{
    Q_GADGET
    QML_FOREIGN(ShotUploadStateReplica)
    QML_NAMED_ELEMENT(ShotUploadStateReplica)
    QML_UNCREATABLE("ShotUploadStateReplica is owned by ShotUploadService")
};
