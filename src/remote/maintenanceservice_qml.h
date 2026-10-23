#pragma once

#include "maintenanceservice.h"

#include <QtQml/QQmlEngine>
#include <QtQml/QJSEngine>
#include <QtQmlIntegration/qqmlintegration.h>
#include <type_traits>

struct MaintenanceServiceForeign
{
    Q_GADGET
    QML_FOREIGN(MaintenanceService)
    QML_NAMED_ELEMENT(MaintenanceService)
    QML_SINGLETON
public:
    static MaintenanceService* create(QQmlEngine* engine, QJSEngine*)
    {
        static_assert(!std::is_default_constructible_v<MaintenanceService>);
        return MaintenanceService::create(engine);
    }
};
