#pragma once

#include "bagservice.h"

#include <QtQml/QJSEngine>
#include <QtQml/QQmlEngine>
#include <type_traits>

struct BagServiceForeign
{
    Q_GADGET
    QML_FOREIGN(BagService)
    QML_SINGLETON
    QML_NAMED_ELEMENT(BagService)
public:
    static BagService* create(QQmlEngine* engine, QJSEngine*)
    {
        static_assert(!std::is_default_constructible_v<BagService>);
        return new BagService(BagService::publishedNode(), engine);
    }
};
