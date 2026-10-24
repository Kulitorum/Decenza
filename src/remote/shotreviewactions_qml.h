#pragma once

#include "shotreviewactions.h"
#include "rep_shotreviewactions_replica.h"

#include <QtQml/QQmlEngine>
#include <QtQml/QJSEngine>
#include <QtQmlIntegration/qqmlintegration.h>
#include <type_traits>

struct ShotReviewActionsForeign
{
    Q_GADGET
    QML_FOREIGN(ShotReviewActions)
    QML_SINGLETON
    QML_NAMED_ELEMENT(ShotReviewActions)
public:
    static ShotReviewActions* create(QQmlEngine* engine, QJSEngine*)
    {
        static_assert(!std::is_default_constructible_v<ShotReviewActions>);
        return new ShotReviewActions(ShotReviewActions::publishedNode(), engine);
    }
};

struct ShotReviewActionStateReplicaForeign
{
    Q_GADGET
    QML_FOREIGN(ShotReviewActionStateReplica)
    QML_NAMED_ELEMENT(ShotReviewActionStateReplica)
    QML_UNCREATABLE("ShotReviewActionStateReplica is owned by ShotReviewActions")
};
