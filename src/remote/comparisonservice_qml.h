#pragma once
#include "comparisonservice.h"
#include <QtQml/QQmlEngine>
#include <QtQml/QJSEngine>
#include <type_traits>

struct ComparisonServiceForeign
{
    Q_GADGET
    QML_FOREIGN(ComparisonService)
    QML_SINGLETON
    QML_NAMED_ELEMENT(ComparisonService)
public:
    static ComparisonService* create(QQmlEngine* engine, QJSEngine*)
    {
        static_assert(!std::is_default_constructible_v<ComparisonService>);
        return new ComparisonService(ComparisonService::publishedNode(), engine);
    }
};
