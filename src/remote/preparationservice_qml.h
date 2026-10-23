#pragma once

#include "preparationservice.h"

#include <QtQml/QQmlEngine>
#include <QtQml/QJSEngine>
#include <type_traits>

struct PreparationServiceForeign
{
    Q_GADGET
    QML_FOREIGN(PreparationService)
    QML_SINGLETON
    QML_NAMED_ELEMENT(PreparationService)
public:
    static PreparationService* create(QQmlEngine* engine, QJSEngine*)
    {
        static_assert(!std::is_default_constructible_v<PreparationService>);
        return new PreparationService(PreparationService::publishedNode(), engine);
    }
};
