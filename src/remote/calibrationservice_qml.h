#pragma once

#include "calibrationservice.h"

#include <QtQml/QQmlEngine>
#include <QtQml/QJSEngine>
#include <QtQmlIntegration/qqmlintegration.h>
#include <type_traits>

struct CalibrationServiceForeign
{
    Q_GADGET
    QML_FOREIGN(CalibrationService)
    QML_NAMED_ELEMENT(CalibrationService)
    QML_SINGLETON
public:
    static CalibrationService* create(QQmlEngine* engine, QJSEngine*)
    {
        static_assert(!std::is_default_constructible_v<CalibrationService>);
        return CalibrationService::create(engine);
    }
};
