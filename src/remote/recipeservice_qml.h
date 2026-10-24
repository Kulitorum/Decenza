#pragma once
#include "recipeservice.h"
#include <QtQml/QQmlEngine>
#include <QtQml/QJSEngine>
#include <type_traits>

struct RecipeServiceForeign
{
    Q_GADGET
    QML_FOREIGN(RecipeService)
    QML_SINGLETON
    QML_NAMED_ELEMENT(RecipeService)
public:
    static RecipeService* create(QQmlEngine* engine, QJSEngine*)
    {
        static_assert(!std::is_default_constructible_v<RecipeService>);
        return new RecipeService(RecipeService::publishedNode(), engine);
    }
};
