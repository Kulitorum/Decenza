#pragma once

#include "aisettingsservice.h"
#include <QtQml/QQmlEngine>
#include <QtQml/QJSEngine>
#include <type_traits>

struct AISettingsServiceForeign
{
    Q_GADGET
    QML_FOREIGN(AISettingsService)
    QML_SINGLETON
    QML_NAMED_ELEMENT(AISettingsService)
public:
    static AISettingsService* create(QQmlEngine* engine, QJSEngine* jsEngine)
    {
        static_assert(!std::is_default_constructible_v<AISettingsService>);
        return AISettingsService::create(engine, jsEngine);
    }
};
