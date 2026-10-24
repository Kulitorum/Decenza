#pragma once

#include "layoutservice.h"

#include <QtQml/QQmlEngine>
#include <QtQml/QJSEngine>
#include <type_traits>

struct LayoutServiceForeign
{
    Q_GADGET
    QML_FOREIGN(LayoutService)
    QML_SINGLETON
    QML_NAMED_ELEMENT(LayoutService)
public:
    static LayoutService* create(QQmlEngine* engine, QJSEngine*)
    {
        static_assert(!std::is_default_constructible_v<LayoutService>);
        return new LayoutService(LayoutService::publishedNode(), engine);
    }
};
