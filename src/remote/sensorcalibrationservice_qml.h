#pragma once

#include "sensorcalibrationservice.h"

#include <QtQml/QQmlEngine>
#include <QtQml/QJSEngine>
#include <QtQmlIntegration/qqmlintegration.h>
#include <type_traits>

struct SensorCalibrationServiceForeign
{
    Q_GADGET
    QML_FOREIGN(SensorCalibrationService)
    QML_SINGLETON
    QML_NAMED_ELEMENT(SensorCalibrationService)
public:
    static SensorCalibrationService* create(QQmlEngine* engine, QJSEngine*)
    {
        static_assert(!std::is_default_constructible_v<SensorCalibrationService>);
        return SensorCalibrationService::create(engine);
    }
};
