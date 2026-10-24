#pragma once

#include "settingspreferencesservice.h"
#include <QtQml/QQmlEngine>
#include <QtQml/QJSEngine>
#include <type_traits>

struct SettingsPreferencesServiceForeign
{
    Q_GADGET
    QML_FOREIGN(SettingsPreferencesService)
    QML_SINGLETON
    QML_NAMED_ELEMENT(SettingsPreferencesService)
public:
    static SettingsPreferencesService* create(QQmlEngine* engine, QJSEngine* jsEngine)
    {
        static_assert(!std::is_default_constructible_v<SettingsPreferencesService>);
        return SettingsPreferencesService::create(engine, jsEngine);
    }
};
