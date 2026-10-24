#pragma once

#include "recipecontextservice.h"
#include <QtQml/QQmlEngine>
#include <QtQml/QJSEngine>
#include <type_traits>

struct RecipeContextServiceForeign
{
    Q_GADGET
    QML_FOREIGN(RecipeContextService)
    QML_SINGLETON
    QML_NAMED_ELEMENT(RecipeContext)
public:
    static RecipeContextService* create(QQmlEngine* engine, QJSEngine* jsEngine)
    {
        static_assert(!std::is_default_constructible_v<RecipeContextService>);
        return RecipeContextService::create(engine, jsEngine);
    }
};
