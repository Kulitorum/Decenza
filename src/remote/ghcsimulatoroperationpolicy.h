#pragma once

#include <QString>
#include <QVariantList>
#include <QVariantMap>

class GHCSimulatorOperationPolicy final
{
public:
    static bool supported(const QString& operation)
    {
        return operation == QLatin1String("pressEspresso")
            || operation == QLatin1String("pressSteam")
            || operation == QLatin1String("pressHotWater")
            || operation == QLatin1String("pressFlush")
            || operation == QLatin1String("pressStop");
    }

    static bool simulatorAvailable(bool simulationMode, bool simulatorAttached)
    {
        return simulationMode && simulatorAttached;
    }

    static bool canExecute(const QString& operation, bool simulationMode,
                           bool simulatorAttached)
    {
        return supported(operation) && simulatorAvailable(simulationMode, simulatorAttached);
    }

    static QVariantMap snapshot(bool simulationMode, bool simulatorAttached,
                                const QVariantList& ledColors, bool stopPressed,
                                int activeFunction)
    {
        return {{QStringLiteral("available"), simulatorAvailable(simulationMode, simulatorAttached)},
                {QStringLiteral("ledColors"), ledColors},
                {QStringLiteral("stopPressed"), stopPressed},
                {QStringLiteral("activeFunction"), activeFunction}};
    }
};
