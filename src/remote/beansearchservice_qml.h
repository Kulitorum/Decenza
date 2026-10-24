#pragma once

#include "beansearchservice.h"

#include <QtQml/QJSEngine>
#include <QtQml/QQmlEngine>
#include <type_traits>

struct BeanSearchServiceForeign
{
    Q_GADGET
    QML_FOREIGN(BeanSearchService)
    QML_SINGLETON
    QML_NAMED_ELEMENT(BeanSearchService)
public:
    static BeanSearchService* create(QQmlEngine* engine, QJSEngine*)
    {
        static_assert(!std::is_default_constructible_v<BeanSearchService>);
        return new BeanSearchService(BeanSearchService::publishedNode(), engine);
    }
};
