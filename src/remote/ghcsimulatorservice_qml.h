#pragma once

#include "ghcsimulatorservice.h"

#include <QtQml/QQmlEngine>
#include <QtQml/QJSEngine>
#include <type_traits>

struct GHCSimulatorServiceForeign
{
    Q_GADGET
    QML_FOREIGN(GHCSimulatorService)
    QML_SINGLETON
    QML_NAMED_ELEMENT(GHCSimulatorService)
public:
    static GHCSimulatorService* create(QQmlEngine* engine, QJSEngine*)
    {
        static_assert(!std::is_default_constructible_v<GHCSimulatorService>);
        return GHCSimulatorService::create(engine, nullptr);
    }
};
