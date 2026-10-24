#pragma once

#include "flowcalibrationservice.h"

#include <QtQml/QQmlEngine>
#include <QtQml/QJSEngine>
#include <QtQmlIntegration/qqmlintegration.h>
#include <type_traits>

struct FlowCalibrationServiceForeign
{
    Q_GADGET
    QML_FOREIGN(FlowCalibrationService)
    QML_SINGLETON
    QML_NAMED_ELEMENT(FlowCalibrationService)
public:
    static FlowCalibrationService* create(QQmlEngine* engine, QJSEngine*)
    {
        static_assert(!std::is_default_constructible_v<FlowCalibrationService>);
        return FlowCalibrationService::create(engine);
    }
};
