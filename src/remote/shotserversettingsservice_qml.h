#pragma once

#include "shotserversettingsservice.h"
#include <QtQml/QQmlEngine>
#include <QtQml/QJSEngine>
#include <type_traits>

struct ShotServerSettingsServiceForeign
{
    Q_GADGET
    QML_FOREIGN(ShotServerSettingsService)
    QML_SINGLETON
    QML_NAMED_ELEMENT(ShotServerSettingsService)
public:
    static ShotServerSettingsService* create(QQmlEngine* engine, QJSEngine* jsEngine)
    {
        static_assert(!std::is_default_constructible_v<ShotServerSettingsService>);
        return ShotServerSettingsService::create(engine, jsEngine);
    }
};
