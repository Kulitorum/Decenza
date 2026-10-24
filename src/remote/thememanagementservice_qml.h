#pragma once

#include "thememanagementservice.h"
#include <QtQml/QQmlEngine>
#include <QtQml/QJSEngine>
#include <type_traits>

struct ThemeManagementServiceForeign
{
    Q_GADGET
    QML_FOREIGN(ThemeManagementService)
    QML_SINGLETON
    QML_NAMED_ELEMENT(ThemeManagementService)
public:
    static ThemeManagementService* create(QQmlEngine* engine, QJSEngine*)
    {
        static_assert(!std::is_default_constructible_v<ThemeManagementService>);
        return ThemeManagementService::create(engine);
    }
};
